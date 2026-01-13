#ifndef UTILS_H
#define UTILS_H

#include <windows.h>
#include <wctype.h>  // Для towlower

// Structs для PEB (from RedOps 2025, corrected for x64 alignment)
typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _LDR_MODULE {
    LIST_ENTRY e[3];             // 0x000 - 0x030 (InLoadOrder, InMemoryOrder, InInitializationOrder)
    HMODULE base;                // 0x030
    void* entry;                 // 0x038
    ULONG size;                  // 0x040
    BYTE pad[4];                 // 0x044 - Padding for alignment
    UNICODE_STRING fullDllName;  // 0x048 - FullDllName
    UNICODE_STRING baseDllName;  // 0x058 - BaseDllName (short name, used for hash)
    ULONG flags;                 // 0x068
    SHORT LoadCount;             // 0x06C (USHORT actually, but SHORT is fine)
} LDR_MODULE, *PLDR_MODULE;

// NTSTATUS defines (manual, т.к. MinGW может не иметь ntstatus.h)
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#define STATUS_NOT_FOUND ((NTSTATUS)0xC0000034L)
#define STATUS_SUCCESS ((NTSTATUS)0x0L)  // Добавлено

// Functions
HMODULE GetModuleHandlePeb(LPCWSTR name);
PVOID GetProcAddressPeb(HMODULE hModule, LPCSTR name);
NTSTATUS UnhookFunctionDirect(PVOID targetFunc, HMODULE module, const char* funcName);

#endif