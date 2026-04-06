#pragma once
#include <windows.h>

// ─── NTSTATUS ────────────────────────────────────────────────
typedef LONG NTSTATUS;
#define STATUS_SUCCESS                  0x00000000
#define STATUS_PRIVILEGE_NOT_HELD       0xC0000061
#define STATUS_OBJECT_NAME_COLLISION    0xC0000035

// ─── UNICODE_STRING ──────────────────────────────────────────
typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

// ─── OBJECT_ATTRIBUTES ───────────────────────────────────────
typedef struct _OBJECT_ATTRIBUTES {
    ULONG           Length;
    HANDLE          RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG           Attributes;
    PVOID           SecurityDescriptor;
    PVOID           SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

#define InitializeObjectAttributes(p, n, a, r, s) \
    do {                                           \
        (p)->Length = sizeof(OBJECT_ATTRIBUTES);   \
        (p)->RootDirectory = r;                    \
        (p)->Attributes = a;                       \
        (p)->ObjectName = n;                       \
        (p)->SecurityDescriptor = s;               \
        (p)->SecurityQualityOfService = NULL;      \
    } while(0)

// ─── FUNCTION POINTER TYPEDEFS ───────────────────────────────
typedef NTSTATUS (NTAPI* _NtLoadDriver)(
    PUNICODE_STRING DriverServiceName
);

typedef NTSTATUS (NTAPI* _NtUnloadDriver)(
    PUNICODE_STRING DriverServiceName
);

typedef VOID (NTAPI* _RtlInitUnicodeString)(
    PUNICODE_STRING DestinationString,
    PCWSTR          SourceString
);