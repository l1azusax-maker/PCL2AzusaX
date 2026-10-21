/* Verification harness: mirrors PCL.Core.App.Region.RegionPolicy.ReadWindowsSignals/Evaluate
 * and checks that the dwmapi proxy redirects every signal the policy consults. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <winnls.h>
#include <string.h>

static BOOL IsChinaGeoName(const WCHAR* name)
{
    return name != NULL && (_wcsicmp(name, L"CN") == 0 || wcscmp(name, L"156") == 0);
}

static BOOL IsChinaLocale(const WCHAR* l)
{
    if (l == NULL || !*l) return FALSE;
    size_t n = wcslen(l);
    if (_wcsicmp(l, L"zh-CN") == 0) return TRUE;
    if (n >= 3 && _wcsicmp(l + n - 3, L"-CN") == 0) return TRUE;
    return FALSE;
}

int wmain(void)
{
    int failures = 0;

    HMODULE h = LoadLibraryW(L"dwmapi.dll");
    wprintf(L"LoadLibrary(dwmapi.dll) -> %p\n", (void*)h);
    if (h == NULL) { wprintf(L"FAIL: proxy not loaded\n"); return 1; }
    Sleep(500);

    /* forwarding sanity */
    typedef HRESULT (WINAPI* PFN)(BOOL*);
    PFN dwmIsComp = (PFN)GetProcAddress(h, "DwmIsCompositionEnabled");
    BOOL comp = FALSE;
    HRESULT hr = dwmIsComp ? dwmIsComp(&comp) : E_FAIL;
    wprintf(L"[fwd] DwmIsCompositionEnabled hr=0x%08lX enabled=%d\n", (unsigned long)hr, comp);
    if (FAILED(hr)) { wprintf(L"  FAIL: forwarding broken\n"); failures++; }

    /* ---- signals, in RegionPolicy order ---- */
    WCHAR geo[64] = {0};
    int geoLen = GetUserDefaultGeoName(geo, 64);
    BOOL geoAvail = (geoLen > 1 && geoLen <= 64);
    if (geoAvail) geo[geoLen - 1] = 0;
    wprintf(L"[1] GetUserDefaultGeoName   -> \"%ls\"\n", geo);

    GEOID gid = GetUserGeoID(GEOCLASS_NATION);
    WCHAR iso2[16] = {0};
    int n2 = GetGeoInfoW(gid, GEO_ISO2, iso2, 16, 0);
    if (n2 > 0) iso2[n2 - 1] = 0;
    wprintf(L"[2] GetUserGeoID/GetGeoInfoW-> %d / \"%ls\"\n", (int)gid, iso2);

    /* device region */
    HKEY k = NULL;
    LSTATUS rs = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Control Panel\\DeviceRegion", 0, KEY_READ, &k);
    BOOL deviceRegionAvail = (rs == ERROR_SUCCESS);
    if (deviceRegionAvail) RegCloseKey(k);
    wprintf(L"[3] DeviceRegion readable   -> %d (want 0)\n", deviceRegionAvail);
    if (deviceRegionAvail) { failures++; wprintf(L"  FAIL: DeviceRegion not blocked\n"); }

    HKEY k2 = NULL;
    LSTATUS rs2 = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &k2);
    if (rs2 == ERROR_SUCCESS) RegCloseKey(k2); else { failures++; wprintf(L"  FAIL: registry forwarding broken\n"); }
    wprintf(L"[3b] other registry key     -> %ld (want 0)\n", (long)rs2);

    WCHAR loc[85] = {0};
    int locLen = GetUserDefaultLocaleName(loc, 85);
    if (locLen > 0) loc[locLen - 1] = 0;
    wprintf(L"[4] GetUserDefaultLocaleName-> \"%ls\"\n", loc);

    WCHAR uil[256] = {0};
    ULONG cnt = 0, chars = 0;
    GetUserPreferredUILanguages(8, &cnt, NULL, &chars);
    BOOL uiOk = FALSE;
    if (chars > 0 && chars < 256) uiOk = GetUserPreferredUILanguages(8, &cnt, uil, &chars) != 0;
    wprintf(L"[5] GetUserPreferredUILang  -> \"%ls\" (ok=%d count=%lu)\n", uil, uiOk, cnt);

    /* timezone stays untouched on purpose */
    DYNAMIC_TIME_ZONE_INFORMATION tz = {0};
    GetDynamicTimeZoneInformation(&tz);
    BOOL chinaTz = (_wcsicmp(tz.TimeZoneKeyName, L"China Standard Time") == 0);
    wprintf(L"[6] timezone (not hooked)   -> \"%ls\" chinaTz=%d\n", tz.TimeZoneKeyName, chinaTz);

    /* ---- Evaluate() emulation ---- */
    const char* cls = "Unknown";
    const char* reason = "insufficient-signals";
    if (geoAvail && IsChinaGeoName(geo)) { cls = "Mainland"; reason = "geo-cn"; }
    else if (deviceRegionAvail) { cls = "Mainland"; reason = "device-region-cn"; }
    else if (chinaTz && (IsChinaLocale(loc) || IsChinaLocale(uil))) { cls = "Mainland"; reason = "china-time-zone-and-locale"; }
    else if (geoAvail && geo[0] && geo[1] && !IsChinaGeoName(geo)) { cls = "Other"; reason = "geo-other"; }

    BOOL restricted = (strcmp(cls, "Mainland") == 0 || strcmp(cls, "Unknown") == 0);
    wprintf(L"\nRegionPolicy.Evaluate -> Classification=%hs (%hs), IsRestricted=%d\n",
            cls, reason, (int)restricted);
    if (restricted) { wprintf(L"FAIL: PCL would still block downloads\n"); failures++; }
    else wprintf(L"PASS: minecraft-install / download features unlocked\n");

    return failures == 0 ? 0 : 1;
}
