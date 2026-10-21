/*
 * Minimal CLR (.NET Framework 4.x) hosting shim, no SDK headers required.
 * Only used when the host process already runs the desktop CLR (PCL2);
 * CoreCLR hosts (PCL-CE) are skipped by the caller because the two
 * runtimes cannot coexist inside one process.
 *
 * Interfaces below are the canonical CLR hosting vtable layouts
 * (metahost.h / corhost.h); GUIDs are spelled out so nothing has to be
 * linked from mscoree.lib.
 *
 * Return codes: 1 = ok, negative = failure stage (also logged by the caller).
 */

#include <windows.h>

/* {9280188D-0E8E-4867-B30C-7FA83884E8DE} */
static const GUID kClsidClrMetaHost =
    { 0x9280188D, 0x0E8E, 0x4867, { 0xB3, 0x0C, 0x7F, 0xA8, 0x38, 0x84, 0xE8, 0xDE } };
/* {D332DB9E-B9B3-4125-8207-A14884F53216} */
static const GUID kIidIclrMetaHost =
    { 0xD332DB9E, 0xB9B3, 0x4125, { 0x82, 0x07, 0xA1, 0x48, 0x84, 0xF5, 0x32, 0x16 } };
/* {BD39D1D2-BA2F-486A-89B0-B4B0CB466891} */
static const GUID kIidIclrRuntimeInfo =
    { 0xBD39D1D2, 0xBA2F, 0x486A, { 0x89, 0xB0, 0xB4, 0xB0, 0xCB, 0x46, 0x68, 0x91 } };
/* {90F1A06E-7712-4762-86B5-7A5EBA6BDB02} */
static const GUID kClsidClrRuntimeHost =
    { 0x90F1A06E, 0x7712, 0x4762, { 0x86, 0xB5, 0x7A, 0x5E, 0xBA, 0x6B, 0xDB, 0x02 } };
/* {90F1A06C-7712-4762-86B5-7A5EBA6BDB02} */
static const GUID kIidIclrRuntimeHost =
    { 0x90F1A06C, 0x7712, 0x4762, { 0x86, 0xB5, 0x7A, 0x5E, 0xBA, 0x6B, 0xDB, 0x02 } };

class ICLRRuntimeInfoX : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetVersionString(LPWSTR pwzBuffer, DWORD* pcchBuffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRuntimeDirectory(LPWSTR pwzBuffer, DWORD* pcchBuffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsLoaded(HANDLE hProcess, BOOL* pbLoaded) = 0;
    virtual HRESULT STDMETHODCALLTYPE LoadErrorString(UINT iResourceID, LPWSTR pwzBuffer,
                                                     DWORD* pcchBuffer, LCID lcid) = 0;
    virtual HRESULT STDMETHODCALLTYPE LoadLibrary(LPCWSTR pwzDllName, HMODULE* phndModule) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcAddress(LPCSTR pszProcName, LPVOID* ppProc) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetInterface(REFCLSID rclsid, REFIID riid, LPVOID* ppUnk) = 0;
    virtual HRESULT STDMETHODCALLTYPE ReleaseProcess(HANDLE hProcess) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDefaultStartupFlags(DWORD* pdwStartupFlags, LPWSTR pwzHostConfigFile,
                                                             DWORD* pcchHostConfigFile) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultStartupFlags(DWORD dwStartupFlags, LPCWSTR pwzHostConfigFile) = 0;
    virtual HRESULT STDMETHODCALLTYPE BindAsLegacyV2Runtime() = 0;
    virtual HRESULT STDMETHODCALLTYPE IsStarted(BOOL* pbStarted, DWORD* pdwStartupFlags) = 0;
};

class ICLRMetaHostX : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetRuntime(LPCWSTR pwzVersion, REFIID riid, LPVOID* ppRuntime) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVersionFromFile(LPCWSTR pwzFilePath, LPWSTR pwzBuffer,
                                                         DWORD* pcchBuffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE EnumerateInstalledRuntimes(IUnknown** ppEnumerator) = 0;
    virtual HRESULT STDMETHODCALLTYPE EnumerateLoadedRuntimes(HANDLE hProcess, IUnknown** ppEnumerator) = 0;
    virtual HRESULT STDMETHODCALLTYPE RequestRuntimeLoadedNotification(void* pCallbackFunction) = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryLegacyV2RuntimeBinding(REFIID riid, LPVOID* ppUnk) = 0;
    virtual HRESULT STDMETHODCALLTYPE ExitProcess(HANDLE hProcess) = 0;
};

class ICLRRuntimeHostX : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Start() = 0;
    virtual HRESULT STDMETHODCALLTYPE Stop() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetHostControl(IUnknown* pHostControl) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCLRControl(IUnknown** pCLRControl) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnloadAppDomain(DWORD dwAppDomainId, BOOL fWaitUntilDone) = 0;
    virtual HRESULT STDMETHODCALLTYPE ExecuteInAppDomain(DWORD dwAppDomainId, void* pfnManaged, void* pArgs) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentAppDomainId(DWORD* pdwAppDomainId) = 0;
    virtual HRESULT STDMETHODCALLTYPE ExecuteApplication(LPCWSTR pwzAppFullName, DWORD dwManifestPaths,
                                                         LPCWSTR* ppwzManifestPaths, DWORD dwActivationData,
                                                         LPCWSTR* ppwzActivationData, int* pReturnValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE ExecuteInDefaultAppDomain(LPCWSTR pwzAssemblyPath, LPCWSTR pwzTypeName,
                                                                LPCWSTR pwzMethodName, LPCWSTR pwzArgument,
                                                                DWORD* pReturnValue) = 0;
};

typedef HRESULT(STDAPICALLTYPE* PFN_CLRCreateInstance)(REFCLSID rclsid, REFIID riid, LPVOID* ppv);

extern "C" __declspec(dllexport)
int __cdecl RunManagedPayloadW(const wchar_t* payloadPath, const wchar_t* typeName,
                               const wchar_t* methodName, DWORD* outResult)
{
    if (outResult) *outResult = 0xFFFFFFFF;

    HMODULE mscoree = GetModuleHandleW(L"mscoree.dll");
    if (!mscoree) mscoree = LoadLibraryW(L"mscoree.dll");
    if (!mscoree) return -1;

    PFN_CLRCreateInstance create = (PFN_CLRCreateInstance)GetProcAddress(mscoree, "CLRCreateInstance");
    if (!create) return -2;

    ICLRMetaHostX* metaHost = NULL;
    HRESULT hr = create(kClsidClrMetaHost, kIidIclrMetaHost, (LPVOID*)&metaHost);
    if (FAILED(hr) || !metaHost) return -3;

    ICLRRuntimeInfoX* runtimeInfo = NULL;
    hr = metaHost->GetRuntime(L"v4.0.30319", kIidIclrRuntimeInfo, (LPVOID*)&runtimeInfo);
    if (FAILED(hr) || !runtimeInfo) { metaHost->Release(); return -4; }

    ICLRRuntimeHostX* runtimeHost = NULL;
    hr = runtimeInfo->GetInterface(kClsidClrRuntimeHost, kIidIclrRuntimeHost, (LPVOID*)&runtimeHost);
    if (FAILED(hr) || !runtimeHost)
    {
        runtimeInfo->Release();
        metaHost->Release();
        return -5;
    }

    /* Already-started CLR returns a failure code here; that is fine. */
    runtimeHost->Start();

    DWORD ret = 0xFFFFFFFF;
    hr = runtimeHost->ExecuteInDefaultAppDomain(payloadPath, typeName, methodName, L"", &ret);

    runtimeHost->Release();
    runtimeInfo->Release();
    metaHost->Release();

    if (outResult) *outResult = ret;
    if (SUCCEEDED(hr)) return 1;
    return (int)(-100 - (LONG)(hr & 0xFF));
}
