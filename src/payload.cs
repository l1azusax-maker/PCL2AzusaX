/*
 * PCL2 (Meloong-Git/PCL, VB.NET) download unlock payload.
 *
 * PCL2 gates Minecraft downloading with a compile-time constant:
 *     Public Const RestrictMcDownload As Boolean = True
 * so vbc folds every branch and physically deletes the "Else" arm from
 * PageDownloadInstall.LoaderInit. The real loader (PCL.ModDownload.DlClientListLoader)
 * is still present, LoaderInit just passes the "关闭下载" thrower instead of it.
 *
 * This payload re-implements LoaderInit through a Harmony prefix and hands
 * PageLoaderInit the real loader, exactly like the deleted source branch:
 *     PageLoaderInit(LoadMinecraft, PanLoad, PanAllBack, Nothing,
 *                    DlClientListLoader, AddressOf LoadMinecraft_OnFinish)
 *
 * Everything is reflection-based: nothing here links against PCL at compile time.
 */

using System;
using System.IO;
using System.Linq.Expressions;
using System.Reflection;
using HarmonyLib;

public static class Pcl2DownloadUnlock
{
    private const BindingFlags All = BindingFlags.Instance | BindingFlags.Static |
                                     BindingFlags.Public | BindingFlags.NonPublic;

    private static readonly string LogPath = Path.Combine(Path.GetTempPath(), "PCLRegionUnlock.log");

    /* The CLR probes the host application directory for dependencies, not this payload's
     * directory, so resolve 0Harmony.dll by hand before HarmonyLib is ever touched. */
    static Pcl2DownloadUnlock()
    {
        AppDomain.CurrentDomain.AssemblyResolve += (sender, e) =>
        {
            try
            {
                var simpleName = new AssemblyName(e.Name).Name;
                var here = Path.GetDirectoryName(typeof(Pcl2DownloadUnlock).Assembly.Location);
                if (string.IsNullOrEmpty(here)) return null;
                var candidate = Path.Combine(here, simpleName + ".dll");
                return File.Exists(candidate) ? Assembly.LoadFrom(candidate) : null;
            }
            catch { return null; }
        };
    }

    internal static void Log(string s)
    {
        try { File.AppendAllText(LogPath, "[PCL2-PAYLOAD] " + s + Environment.NewLine); }
        catch { }
    }

    public static int Entry(string arg)
    {
        Log("entry, arg=" + arg);
        try
        {
            var asm = FindPclAssembly();
            if (asm == null) { Log("PCL assembly not loaded"); return 1; }
            Log("assembly: " + asm.FullName);

            var pageType = asm.GetType("PCL.PageDownloadInstall", false);
            if (pageType == null) { Log("type PCL.PageDownloadInstall not found"); return 2; }

            var loaderInit = pageType.GetMethod("LoaderInit", All);
            if (loaderInit == null) { Log("LoaderInit not found"); return 3; }

            var prefix = typeof(Pcl2DownloadUnlock).GetMethod("LoaderInitPrefix", All);
            var harmony = new Harmony("pcl2.download.unlock");
            harmony.Patch(loaderInit, prefix: new HarmonyMethod(prefix));

            var patched = new System.Collections.Generic.List<string>();
            foreach (var m in harmony.GetPatchedMethods())
                patched.Add(m.DeclaringType.FullName + "." + m.Name);
            Log("patched entries: " + string.Join(" | ", patched.ToArray()));
            Log("target: " + loaderInit.DeclaringType.FullName + "." + loaderInit.Name +
                " virtual=" + loaderInit.IsVirtual);
            return 0;
        }
        catch (Exception ex)
        {
            Log("FAILED: " + ex);
            return 99;
        }
    }

    private static Assembly FindPclAssembly()
    {
        foreach (var a in AppDomain.CurrentDomain.GetAssemblies())
        {
            try
            {
                if (a.GetName().Name == "Plain Craft Launcher 2") return a;
            }
            catch { }
        }
        try { return Assembly.Load("Plain Craft Launcher 2"); }
        catch { return null; }
    }

    /* Reflection helpers: GetMethod/GetProperty throw AmbiguousMatchException on PCL's
     * overloaded members, so pick explicitly. */
    private static MethodInfo FindMethod(Type t, string name, int paramCount, bool genericDefinition)
    {
        foreach (var m in t.GetMethods(All))
        {
            if (m.Name != name) continue;
            if (m.IsGenericMethodDefinition != genericDefinition) continue;
            if (m.GetParameters().Length != paramCount) continue;
            return m;
        }
        return null;
    }

    private static PropertyInfo FindProperty(Type t, string name)
    {
        foreach (var p in t.GetProperties(All))
            if (p.Name == name) return p;
        return null;
    }

    /* Mirrors the source branch that vbc deleted:
     *   PageLoaderInit(LoadMinecraft, PanLoad, PanAllBack, Nothing,
     *                  DlClientListLoader, AddressOf LoadMinecraft_OnFinish) */
    private static bool LoaderInitPrefix(object __instance)
    {
        try
        {
            var t = __instance.GetType();

            var controlInit = FindMethod(t, "ControlInit", 0, false);
            if (controlInit == null) { Log("ControlInit not found"); return true; }
            controlInit.Invoke(__instance, null);

            var asm = t.Assembly;
            var modDownload = asm.GetType("PCL.ModDownload", false);
            if (modDownload == null) { Log("PCL.ModDownload not found"); return true; }

            var loaderField = modDownload.GetField("DlClientListLoader", All);
            if (loaderField == null) { Log("DlClientListLoader field not found"); return true; }

            var loader = loaderField.GetValue(null);
            if (loader == null) { Log("DlClientListLoader is null"); return true; }

            var loadMinecraftUi = FindProperty(t, "LoadMinecraft").GetValue(__instance, null);
            var panLoad = FindProperty(t, "PanLoad").GetValue(__instance, null);
            var panAllBack = FindProperty(t, "PanAllBack").GetValue(__instance, null);

            var loaderTaskType = loaderField.FieldType;                 /* LoaderTask`2[String, DlClientListResult] */
            var outType = loaderTaskType.GetGenericArguments()[1];
            var onFinishMethod = FindMethod(t, "LoadMinecraft_OnFinish", 0, false);
            var actionType = typeof(Action<>).MakeGenericType(loaderTaskType);

            /* VB's AddressOf performs a relaxed conversion (the Sub() takes no argument),
             * which Delegate.CreateDelegate refuses; build the wrapper with an expression tree. */
            var taskParam = Expression.Parameter(loaderTaskType, "task");
            var body = Expression.Call(Expression.Constant(__instance), onFinishMethod);
            var onFinish = Expression.Lambda(actionType, body, taskParam).Compile();

            MethodInfo pageLoaderInit = null;
            var baseType = t;
            while (baseType != null && pageLoaderInit == null)
            {
                pageLoaderInit = FindMethod(baseType, "PageLoaderInit", 8, true);
                if (pageLoaderInit == null) baseType = baseType.BaseType;
            }
            if (pageLoaderInit == null) { Log("PageLoaderInit not found"); return true; }

            var generic = pageLoaderInit.MakeGenericMethod(typeof(string), outType);
            generic.Invoke(__instance, new object[]
            {
                loadMinecraftUi,
                panLoad,
                panAllBack,
                null,           /* PanAlways */
                loader,         /* real loader instead of the "关闭下载" thrower */
                onFinish,       /* Action(Of LoaderTask(Of String, DlClientListResult)) */
                null,           /* InputInvoke */
                true            /* AutoRun */
            });

            Log("LoaderInit replaced with the real DlClientListLoader");
            return false;       /* skip the original, broken implementation */
        }
        catch (Exception ex)
        {
            Log("prefix failed, falling back to original: " + ex);
            return true;
        }
    }
}
