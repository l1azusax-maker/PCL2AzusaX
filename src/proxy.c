/*
 * PCL-CE Region Unlock proxy DLL (dwmapi.dll)
 *
 * PCL P/Invokes "dwmapi.dll" (PCL.Core.Utils.OS.WindowInterop), so a dwmapi.dll
 * placed next to PCL2_CE_*.exe is loaded out of the application directory
 * before the system one. This DLL:
 *   1. forwards every dwmapi export to a renamed copy of the real system DLL
 *   2. inline-hooks the Win32 "where am I" APIs consulted by
 *      PCL.Core.App.Region.RegionPolicy.ReadWindowsSignals():
 *        kernel32!GetUserDefaultGeoName
 *        kernel32!GetUserGeoID
 *        kernel32!GetGeoInfoW
 *        advapi32!RegOpenKeyExW   (HKLM\...\Control Panel\DeviceRegion)
 *      forcing the United States, so RegionPolicy.Evaluate() yields
 *      RegionClassification.Other and RegionAssessment.IsRestricted == false.
 *
 * Hooking at the Win32 boundary keeps it update-proof: a PCL-CE release can
 * rewrite all of its managed code, but it still has to ask Windows for the region.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include <string.h>
#include "MinHook.h"

/* ---------- forwarding slots (generated from System32\dwmapi.dll) ---------- */
#include "../gen/proxy_exports.inc"

static const int g_exportCount = (int)(sizeof(g_exports) / sizeof(g_exports[0]));

/* ---------- logging (no CRT file IO, safe under loader lock) ---------- */
static void LogA(const char* fmt, ...)
{
    char line[1200];
    int n;

    SYSTEMTIME st;
    GetLocalTime(&st);
    n = _snprintf_s(line, sizeof(line), _TRUNCATE, "[%02u:%02u:%02u.%03u] ",
                    st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    va_list ap;
    va_start(ap, fmt);
    n += _vsnprintf_s(line + n, sizeof(line) - n, _TRUNCATE, fmt, ap);
    va_end(ap);

    if (n < 0) n = (int)strlen(line);
    if (n > (int)sizeof(line) - 3) n = (int)sizeof(line) - 3;
    line[n++] = '\r';
    line[n++] = '\n';

    WCHAR path[MAX_PATH];
    DWORD len = GetTempPathW(MAX_PATH - 32, path);
    if (len == 0) return;
    lstrcatW(path, L"PCLRegionUnlock.log");

    HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    DWORD written = 0;
    WriteFile(h, line, (DWORD)n, &written, NULL);
    CloseHandle(h);
}

/* ---------- real DLL loading ---------- */
static HMODULE g_real = NULL;
static WCHAR   g_realPath[MAX_PATH];

static BOOL FileSizeEqual(const WCHAR* a, const WCHAR* b)
{
    WIN32_FILE_ATTRIBUTE_DATA fa, fb;
    if (!GetFileAttributesExW(a, GetFileExInfoStandard, &fa)) return FALSE;
    if (!GetFileAttributesExW(b, GetFileExInfoStandard, &fb)) return FALSE;
    return fa.nFileSizeHigh == fb.nFileSizeHigh && fa.nFileSizeLow == fb.nFileSizeLow;
}

static BOOL InitRealDll(void)
{
    WCHAR tmp[MAX_PATH], dir[MAX_PATH], sys[MAX_PATH];

    if (GetTempPathW(MAX_PATH - 64, tmp) == 0) return FALSE;
    lstrcpyW(dir, tmp); lstrcatW(dir, L"dwmapi_rt_pcl");
    CreateDirectoryW(dir, NULL);
    wsprintfW(g_realPath, L"%s\\dwmapi.dll", dir);

    if (GetSystemDirectoryW(sys, MAX_PATH - 32) == 0) return FALSE;
    lstrcatW(sys, L"\\dwmapi.dll");

    if (!FileSizeEqual(sys, g_realPath)) {
        if (!CopyFileW(sys, g_realPath, FALSE)) {
            /* a sibling PCL process may hold the extracted copy open; reuse it */
            WIN32_FILE_ATTRIBUTE_DATA fa;
            if (!GetFileAttributesExW(g_realPath, GetFileExInfoStandard, &fa)) {
                UINT n = GetWindowsDirectoryW(sys, MAX_PATH - 64);
                if (n == 0) return FALSE;
                lstrcpyW(sys + n, L"\\System32\\dwmapi.dll");
                if (!CopyFileW(sys, g_realPath, FALSE)) return FALSE;
            }
        }
    }

    g_real = LoadLibraryW(g_realPath);
    if (g_real == NULL) return FALSE;

    int ok = 0;
    for (int i = 0; i < g_exportCount; i++) {
        PVOID p = (PVOID)GetProcAddress(g_real, g_exports[i].name);
        if (p != NULL) {
            *g_exports[i].slot = p;
            ok++;
        }
    }
    LogA("real dwmapi loaded (%d/%d exports resolved)", ok, g_exportCount);
    return ok == g_exportCount;
}

/* ---------- hooked Win32 region APIs ---------- */
static int     (WINAPI* orig_GetUserDefaultGeoName)(LPWSTR, int);
static GEOID   (WINAPI* orig_GetUserGeoID)(GEOCLASS);
static int     (WINAPI* orig_GetGeoInfoW)(GEOID, GEOTYPE, LPWSTR, int, LANGID);
static int     (WINAPI* orig_GetUserDefaultLocaleName)(LPWSTR, int);
static BOOL    (WINAPI* orig_GetUserPreferredUILanguages)(ULONG, PULONG, PWSTR, PULONG);
static LSTATUS (WINAPI* orig_RegOpenKeyExW)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);

/* PCL: length = GetUserDefaultGeoName(buf, 16); length > 1 && length <= 16 -> new string(buf, 0, length - 1) */
static volatile LONG g_geoProbeHits = 0;

static int WINAPI Hook_GetUserDefaultGeoName(LPWSTR geoName, int geoNameCount)
{
    LONG hit = InterlockedIncrement(&g_geoProbeHits);
    if (hit <= 8) LogA("region probe #%ld: GetUserDefaultGeoName -> US", hit);
    if (geoName == NULL || geoNameCount < 3) return -1;
    geoName[0] = L'U';
    geoName[1] = L'S';
    geoName[2] = L'\0';
    return 3;
}

static GEOID WINAPI Hook_GetUserGeoID(GEOCLASS geoClass)
{
    (void)geoClass;
    return (GEOID)244; /* United States */
}

static int WINAPI Hook_GetGeoInfoW(GEOID location, GEOTYPE geoType, LPWSTR geoData, int dataSize, LANGID langId)
{
    const WCHAR* s = NULL;
    switch ((int)geoType) {
    case GEO_NATION:       return 244;
    case GEO_ISO2:         s = L"US";            break;
    case GEO_ISO3:         s = L"USA";           break;
    case GEO_FRIENDLYNAME: s = L"United States"; break;
    default: break;
    }
    if (s == NULL)
        return orig_GetGeoInfoW ? orig_GetGeoInfoW(location, geoType, geoData, dataSize, langId) : 0;

    int len = (int)wcslen(s) + 1;
    if (geoData == NULL || dataSize < len) return 0;
    memcpy(geoData, s, (size_t)len * sizeof(WCHAR));
    return len;
}

/*
 * Second gate in RegionPolicy.Evaluate():
 *     chinaTimeZone && (hasChinaLocale || hasChinaPreferredLanguage) -> Mainland
 * Neutralise the locale half by reporting zh-SG: a real Simplified-Chinese
 * culture (so nothing about rendering changes) whose name neither equals
 * "zh-CN" nor ends with "-CN".
 */
static int WINAPI Hook_GetUserDefaultLocaleName(LPWSTR localeName, int localeNameCount)
{
    static const WCHAR v[] = L"zh-SG";
    const int need = (int)(sizeof(v) / sizeof(WCHAR)); /* 6, null included */
    if (localeName == NULL || localeNameCount < need) return 0;
    memcpy(localeName, v, sizeof(v));
    return need;
}

static BOOL WINAPI Hook_GetUserPreferredUILanguages(ULONG flags, PULONG languageCount, PWSTR languageBuffer, PULONG bufferChars)
{
    static const WCHAR v[] = L"zh-SG";
    const ULONG need = (ULONG)(sizeof(v) / sizeof(WCHAR)); /* characters incl. null */
    (void)flags;

    if (languageCount != NULL) *languageCount = 1;
    if (bufferChars == NULL) return FALSE;

    if (languageBuffer == NULL) {
        *bufferChars = need;
        return TRUE;
    }
    if (*bufferChars < need) {
        *bufferChars = need;
        return FALSE;
    }
    memcpy(languageBuffer, v, sizeof(v));
    *bufferChars = need;
    return TRUE;
}

static BOOL ContainsNoCase(const WCHAR* hay, const WCHAR* needle)
{
    if (hay == NULL || needle == NULL) return FALSE;
    size_t n = wcslen(needle);
    for (const WCHAR* p = hay; *p; p++) {
        if (_wcsnicmp(p, needle, n) == 0) return TRUE;
    }
    return FALSE;
}

/* PCL device-region signal: HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Control Panel\DeviceRegion */
static LSTATUS WINAPI Hook_RegOpenKeyExW(HKEY hKey, LPCWSTR lpSubKey, DWORD ulOptions,
                                         REGSAM samDesired, PHKEY phkResult)
{
    if (lpSubKey != NULL && ContainsNoCase(lpSubKey, L"DeviceRegion")) {
        LogA("suppressed DeviceRegion registry open");
        return ERROR_FILE_NOT_FOUND;
    }
    return orig_RegOpenKeyExW(hKey, lpSubKey, ulOptions, samDesired, phkResult);
}

/* ---------- hook install ---------- */
static void TryHook(HMODULE mod, const char* name, LPVOID detour, LPVOID* original, int* count)
{
    if (mod == NULL) return;
    LPVOID target = (LPVOID)GetProcAddress(mod, name);
    if (target == NULL) {
        LogA("target not found: %s", name);
        return;
    }
    MH_STATUS st = MH_CreateHook(target, detour, original);
    if (st == MH_OK) (*count)++;
    else LogA("MH_CreateHook(%s) failed: %d", name, (int)st);
}

static DWORD WINAPI HookThread(LPVOID param)
{
    (void)param;

    if (MH_Initialize() != MH_OK) {
        LogA("MH_Initialize failed");
        return 0;
    }

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE adv = GetModuleHandleW(L"advapi32.dll");
    if (adv == NULL) {
        LoadLibraryW(L"advapi32.dll");
        adv = GetModuleHandleW(L"advapi32.dll");
    }

    int n = 0;
    TryHook(k32, "GetUserDefaultGeoName", (LPVOID)&Hook_GetUserDefaultGeoName, (LPVOID*)&orig_GetUserDefaultGeoName, &n);
    TryHook(k32, "GetUserGeoID",          (LPVOID)&Hook_GetUserGeoID,          (LPVOID*)&orig_GetUserGeoID,          &n);
    TryHook(k32, "GetGeoInfoW",           (LPVOID)&Hook_GetGeoInfoW,           (LPVOID*)&orig_GetGeoInfoW,           &n);
    TryHook(k32, "GetUserDefaultLocaleName",    (LPVOID)&Hook_GetUserDefaultLocaleName,    (LPVOID*)&orig_GetUserDefaultLocaleName,    &n);
    TryHook(k32, "GetUserPreferredUILanguages", (LPVOID)&Hook_GetUserPreferredUILanguages, (LPVOID*)&orig_GetUserPreferredUILanguages, &n);
    TryHook(adv, "RegOpenKeyExW",         (LPVOID)&Hook_RegOpenKeyExW,         (LPVOID*)&orig_RegOpenKeyExW,         &n);

    MH_STATUS st = MH_EnableHook(MH_ALL_HOOKS);
    LogA("hooks installed: %d created, MH_EnableHook=%d", n, (int)st);
    return 0;
}

/* ---------- managed payload for PCL2 (desktop CLR host) ---------- */
extern int __cdecl RunManagedPayloadW(const wchar_t* payloadPath, const wchar_t* typeName,
                                      const wchar_t* methodName, DWORD* outResult);

static HINSTANCE g_hinst = NULL;

#define IDR_PAYLOAD 101
#define IDR_HARMONY 102

static BOOL ExtractResourceToDir(HMODULE hmod, int resId, const WCHAR* dir, const WCHAR* baseName,
                                 BOOL uniqueBySize, WCHAR* outPath, DWORD outChars)
{
    HRSRC res = FindResourceW(hmod, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (res == NULL) return FALSE;
    DWORD size = SizeofResource(hmod, res);
    HGLOBAL glob = LoadResource(hmod, res);
    if (glob == NULL || size == 0) return FALSE;
    const BYTE* data = (const BYTE*)LockResource(glob);
    if (data == NULL) return FALSE;

    if (uniqueBySize) {
        WCHAR suffix[32];
        wsprintfW(suffix, L"_%lu", (unsigned long)size);
        lstrcpyW(outPath, dir);
        lstrcatW(outPath, L"\\");
        lstrcatW(outPath, baseName);
        lstrcatW(outPath, suffix);
        lstrcatW(outPath, L".dll");
    } else {
        lstrcpyW(outPath, dir);
        lstrcatW(outPath, L"\\");
        lstrcatW(outPath, baseName);
        lstrcatW(outPath, L".dll");
    }

    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(outPath, GetFileExInfoStandard, &fa) &&
        fa.nFileSizeHigh == 0 && fa.nFileSizeLow == size)
        return TRUE; /* already extracted, same build */

    HANDLE h = CreateFileW(outPath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD written = 0;
    BOOL ok = WriteFile(h, data, size, &written, NULL);
    CloseHandle(h);
    return ok && written == size;
}

typedef struct _FINDWND { DWORD pid; HWND hwnd; } FINDWND;

static BOOL CALLBACK FindMainWindowProc(HWND h, LPARAM lp)
{
    FINDWND* f = (FINDWND*)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == f->pid && IsWindowVisible(h) && GetWindow(h, GW_OWNER) == NULL) {
        f->hwnd = h;
        return FALSE;
    }
    return TRUE;
}

/*
 * PCL-CE runs on CoreCLR (.NET 10); PCL2 runs on the desktop CLR (.NET Framework 4.x).
 * The two runtimes cannot share a process, so only inject when clr.dll is the active one.
 *
 * Timing matters: ExecuteInDefaultAppDomain fails while the CLR is still warming up or
 * before the PCL assembly is loaded, and that happens much more often when a proxy/VPN
 * (Clash, TUN adapter, DNS override) stretches out PCL2's startup. So wait for the host's
 * main window first, then inject with retries instead of a single shot.
 */
static DWORD WINAPI ManagedPayloadThread(LPVOID param)
{
    (void)param;

    for (int i = 0; i < 80; i++) {
        if (GetModuleHandleW(L"coreclr.dll") != NULL) {
            LogA("host runs CoreCLR, PCL2 payload not applicable");
            return 0;
        }
        if (GetModuleHandleW(L"clr.dll") != NULL) break;
        Sleep(250);
    }
    if (GetModuleHandleW(L"clr.dll") == NULL) {
        LogA("no desktop CLR detected, PCL2 payload skipped");
        return 0;
    }

    /* Wait for the shell window: by then the CLR is up and the PCL assembly is loaded. */
    FINDWND f;
    f.pid = GetCurrentProcessId();
    f.hwnd = NULL;
    int waited = 0;
    for (int i = 0; i < 160 && f.hwnd == NULL; i++) {
        EnumWindows(FindMainWindowProc, (LPARAM)&f);
        if (f.hwnd == NULL) { Sleep(250); waited += 250; }
    }
    LogA("host main window ready after %d ms (hwnd=%p)", waited, (void*)f.hwnd);

    WCHAR tmp[MAX_PATH], dir[MAX_PATH], payloadPath[MAX_PATH], harmonyPath[MAX_PATH];
    if (GetTempPathW(MAX_PATH - 64, tmp) == 0) return 0;
    lstrcpyW(dir, tmp); lstrcatW(dir, L"dwmapi_rt_pcl");
    CreateDirectoryW(dir, NULL);

    if (!ExtractResourceToDir(g_hinst, IDR_PAYLOAD, dir, L"payload", TRUE, payloadPath, MAX_PATH)) {
        LogA("PCL2 payload extract failed");
        return 0;
    }
    if (!ExtractResourceToDir(g_hinst, IDR_HARMONY, dir, L"0Harmony", FALSE, harmonyPath, MAX_PATH)) {
        LogA("PCL2 harmony extract failed");
        return 0;
    }

    DWORD ret = 0xFFFFFFFF;
    int ok = 0;
    for (int attempt = 1; attempt <= 30; attempt++) {
        ret = 0xFFFFFFFF;
        ok = RunManagedPayloadW(payloadPath, L"Pcl2DownloadUnlock", L"Entry", &ret);
        if (ok && ret == 0) {
            LogA("PCL2 payload applied on attempt %d", attempt);
            return 0;
        }
        LogA("PCL2 payload attempt %d: invoked=%d result=%lu", attempt, ok, (unsigned long)ret);
        Sleep(1000);
    }
    LogA("PCL2 payload FAILED after 30 attempts");
    return 0;
}

/* ---------- entry ---------- */
BOOL WINAPI DllMain(HINSTANCE hinstDll, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)lpvReserved;

    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hinst = hinstDll;
        DisableThreadLibraryCalls(hinstDll);
        WCHAR host[MAX_PATH] = L"?";
        char hostA[MAX_PATH * 2] = "?";
        GetModuleFileNameW(NULL, host, MAX_PATH);
        WideCharToMultiByte(CP_UTF8, 0, host, -1, hostA, sizeof(hostA), NULL, NULL);
        LogA("---- dwmapi proxy attached (PCL region unlock) host=%s ----", hostA);
        if (!InitRealDll()) {
            LogA("FATAL: real dwmapi.dll unavailable, forwarded exports will fail");
            return TRUE;
        }
        HANDLE t = CreateThread(NULL, 0, HookThread, NULL, 0, NULL);
        if (t != NULL) CloseHandle(t);

        HANDLE t2 = CreateThread(NULL, 0, ManagedPayloadThread, NULL, 0, NULL);
        if (t2 != NULL) CloseHandle(t2);
    }
    return TRUE;
}
