# PCL 下载解锁 DLL（PCL-CE + PCL2 双支持）

一个 `dwmapi.dll`，丢进启动器 exe 同目录即可：

| 启动器 | 被限制的功能 | 限制机制 | 本 DLL 的处理 |
|---|---|---|---|
| **PCL-CE** 2.15.1+（C# / .NET 10） | Minecraft 下载、补全、整合包安装 | 运行时按 Windows 区域信息判定 | Hook 5 个 Win32 区域 API + 注册表 |
| **PCL2** 2.99.0.0（VB.NET / .NET Framework 4.8） | Minecraft 原版下载 | 编译期常量 `RestrictMcDownload`，入口代码被 vbc 删除 | 注入托管 payload，用 Harmony 重建被删的入口 |

两者互不干扰：DLL 进入进程后先看宿主跑的是哪个 CLR（`coreclr.dll` / `clr.dll`），只启用对应的那一套。

---

## 一、PCL-CE：区域限制

判定逻辑在 `PCL.Core/App/Region/RegionPolicy.cs`：

```csharp
public sealed record RegionAssessment(RegionClassification Classification, string Reason)
{
    public bool IsRestricted => Classification is RegionClassification.Mainland or RegionClassification.Unknown;
}
```

`RestrictedFeaturePolicy.Check(...)` 在下载、补全、安装、修复、启动补全等十几处拦截，信号来自 `RegionPolicy.ReadWindowsSignals()`：

1. `GetUserDefaultGeoName` 返回 `CN` / `156` → **Mainland**
2. `HKLM\SOFTWARE\...\Control Panel\DeviceRegion` = 45（OOBE 区域为中国）→ **Mainland**
3. 时区是 `China Standard Time` 且 `GetUserDefaultLocaleName` 或 `GetUserPreferredUILanguages` 为 `zh-CN` → **Mainland**
4. geo 合法且非 CN → **Other（放行）**

### 处理

PCL-CE 自己 P/Invoke 了 `dwmapi.dll`（`PCL.Core/Utils/OS/WindowInterop.cs`），而 `dwmapi` 不在 KnownDLLs 里，所以 exe 同目录的 `dwmapi.dll` 会被优先加载。加载后：

1. 44 个 dwmapi 导出**全部转发**到系统真 DLL（复制到 `%TEMP%\dwmapi_rt_pcl\dwmapi_rt_pcl\` 再加载，规避同名递归），WPF 特效零影响；
2. MinHook 在 **Win32 层**改写判定信号：

| 被 Hook | 返回值 |
|---|---|
| `kernel32!GetUserDefaultGeoName` | `US` |
| `kernel32!GetUserGeoID` | `244` |
| `kernel32!GetGeoInfoW` | `US` / `USA` / `United States` |
| `kernel32!GetUserDefaultLocaleName` | `zh-SG` |
| `kernel32!GetUserPreferredUILanguages` | `zh-SG` |
| `advapi32!RegOpenKeyExW` | 拦 `DeviceRegion` 键 → `ERROR_FILE_NOT_FOUND` |

`zh-SG` 是刻意选的：同为简体中文，`"zh-SG"` 既不等于 `"zh-CN"` 也不以 `-CN` 结尾，判定失效但界面语言、日期格式毫无变化。时区保持真实。

**为什么不怕更新**：hook 在 Windows API 上。PCL-CE 单文件发布，每次更新重写整个 exe，但取区域信息只能问这几个函数。

---

## 二、PCL2：编译期删除的下载入口

PCL2 的限制完全不是区域问题。`Modules/ModSecret.vb` 里：

```vb
Public Const RestrictMcDownload As Boolean = True
```

`Const` 会在编译期内联，于是

```vb
If RestrictMcDownload Then
    PageLoaderInit(..., New LoaderTask(...)("关闭下载", Sub() Throw New Exception("为遵守相关法律法规，PCL 暂时不再提供 MC 下载功能……")), ...)
Else
    PageLoaderInit(..., DlClientListLoader, AddressOf LoadMinecraft_OnFinish)
End If
```

里 **Else 分支被 vbc 整段删掉了**（已用 Mono.Cecil 反编译确认，`LoaderInit` 的 IL 里只剩 `ldstr "关闭下载"` 那条路径）。所以纯 Win32 hook 完全无效 —— 不是"条件被拦住"，是"正常入口不存在"。

但被删的只是那行**调用**，真正的加载器 `PCL.ModDownload.DlClientListLoader` 和回调 `LoadMinecraft_OnFinish` 都还在 exe 里。

### 处理

DLL 检测到宿主是桌面 CLR（`clr.dll`）时，把内嵌的托管 payload（`payload.dll`）和 `0Harmony.dll` 释放到 `%TEMP%\dwmapi_rt_pcl\`，通过 `CLRCreateInstance` + `ICLRRuntimeHost::ExecuteInDefaultAppDomain` 注入默认 AppDomain。

payload 用 Harmony 给 `PCL.PageDownloadInstall.LoaderInit` 挂一个 Prefix，Prefix 里用纯反射把源码里被删掉的那行重建出来：

```csharp
controlInit.Invoke(instance, null);
var loader = typeof(PCL.ModDownload).GetField("DlClientListLoader").GetValue(null);
pageLoaderInit.MakeGenericMethod(typeof(string), outType)
    .Invoke(instance, new[] { LoadMinecraft, PanLoad, PanAllBack, null,
                              loader, LoadMinecraft_OnFinish, null, true });
return false;   // 跳过原版那段只会抛异常的实现
```

两个细节：

- `LoadMinecraft_OnFinish` 是 `Sub()`，而 `PageLoaderInit` 要 `Action(Of LoaderTask(Of ...))`，VB 的 `AddressOf` 走的是"放宽委托转换"，`Delegate.CreateDelegate` 会拒绝 —— 改用表达式树包一层。
- `DlClientListLoader` 的泛型参数要从字段类型里取，不能硬编码。

**为什么不怕更新**：payload 是运行时 patch 内存中的方法，不动 exe；PCL2 自动更新只替换 exe，patch 每次启动重新做一遍。CLR 是"哪一个"由宿主决定，PCL-CE（CoreCLR）里这套逻辑直接跳过，不会尝试在 .NET 10 进程里启动 .NET Framework 运行时（两者无法共存）。

---

## 三、安装

```bat
install.bat "D:\你的\PCL目录"
```

或者直接把 `dwmapi.dll` 复制到 `PCL2_CE_*.exe` / `Plain Craft Launcher 2.exe` 所在目录。卸载就是删掉这个文件。

## 四、验证

`%TEMP%\PCLRegionUnlock.log`：

**PCL-CE**
```
---- dwmapi proxy attached host=...\PCL2_CE_Release_x64.exe ----
real dwmapi loaded (44/44 exports resolved)
host runs CoreCLR, PCL2 payload not applicable
hooks installed: 6 created, MH_EnableHook=0
region probe #1: GetUserDefaultGeoName -> US
suppressed DeviceRegion registry open
```

**PCL2**
```
---- dwmapi proxy attached host=...\Plain Craft Launcher 2.exe ----
real dwmapi loaded (44/44 exports resolved)
hooks installed: 6 created, MH_EnableHook=0
[PCL2-PAYLOAD] patched entries: PCL.PageDownloadInstall.LoaderInit
[PCL2-PAYLOAD] LoaderInit replaced with the real DlClientListLoader
```

实测结果：

- PCL-CE 2.15.1：下载页出现版本列表，点击后真实开始下载 `piston-meta.mojang.com` 的 json，PCL 日志里 `[RegionPolicy] 已阻止功能` 计数为 0。
- PCL2 2.99.0.0：下载页不再显示「为遵守相关法律法规」，版本列表正常，「开始下载」后任务管理器显示「26.3-Forge 安装 18.04%，3.76 M/s」。

## 五、注意事项

- **只对本目录内的程序生效**，不影响系统其它软件。
- **杀毒软件可能误报**：DLL 劫持 + API Hook + CLR 注入 + 运行时方法 patch，行为特征和恶意软件高度重合。加白名单。
- **PCL 装在 `C:\Program Files` 下时**复制文件需要管理员权限。
- **仅 x64**：`PCL2_CE_*_x64.exe`、`Plain Craft Launcher 2.exe`（AnyCPU，64 位系统上按 x64 运行）可用。ARM64 版 PCL 需要另做 ARM64 指令级 hook。
- 首次运行会在 `%TEMP%\dwmapi_rt_pcl\` 释放 `dwmapi.dll` 真身、`payload.dll`、`0Harmony.dll`，可随时删除，下次自动重建。
- PCL2 的整合包安装（`ModModpack.ModpackInstall`）同样是编译期被删的入口，目前只恢复了原版下载；需要的话可以按同样方式补。

## 六、目录结构 / 重新编译

```

  out/dwmapi.dll          成品（约 2.3 MB，含托管 payload）
  install.bat             一键安装
  src/proxy.c             原生代理 + Win32 hook + 资源释放 + 注入调度
  src/clrhost.cpp         CLR hosting（零 SDK 头依赖，手写 vtable）
  src/payload.cs          托管 payload（Harmony patch + 反射重建入口）
  src/embed.rc            把 payload.dll / 0Harmony.dll 嵌进原生 DLL
  gen/                    由 tools/gen_proxy.py 从 System32\dwmapi.dll 导出表生成
  build.bat               MSVC x64 编译脚本
```

依赖：VS 2022 BuildTools（C++ x64 + MASM）、[MinHook](https://github.com/TsudaKageyu/minhook)、[Lib.Harmony](https://www.nuget.org/packages/Lib.Harmony) 的 net472 `0Harmony.dll`（放到 `build\`）。

```bat
git clone https://github.com/TsudaKageyu/minhook ..\minhook-master
build.bat
```

## 七、验证工具

- `test/verify.c`：独立复现 `RegionPolicy.Evaluate()` 判定链，输出 `Classification=Other, IsRestricted=0`。
- `tools/ildump.cs`：Mono.Cecil 反编译器，用来确认 PCL2 的 `LoaderInit` 里 Else 分支确实已被删除。
