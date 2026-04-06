#include "beacon.h"
#include "ntdefs.h"
#include "utils.h"

DECLSPEC_IMPORT BOOL WINAPI KERNEL32$DeleteFileW(LPCWSTR);

void go(char* args, int len) {
    datap parser;
    BeaconDataParse(&parser, args, len);

    int    pathLen = 0;
    LPWSTR path    = (LPWSTR)BeaconDataExtract(&parser, &pathLen);

    if (!path) {
        BeaconPrintf(CALLBACK_ERROR, "[-] Invalid arguments\n");
        return;
    }

    if (KERNEL32$DeleteFileW(path)) {
        BeaconPrintf(CALLBACK_OUTPUT, "[+] Deleted: %S\n", path);
    } else {
        BeaconPrintf(CALLBACK_ERROR,
            "[-] DeleteFileW failed: %lu\n",
            KERNEL32$GetLastError());
    }
}