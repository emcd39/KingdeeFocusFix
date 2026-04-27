#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define HOOKDLL_EXPORTS
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <stdio.h>
#include "MinHook.h"
#include "HookDll.h"

// ========== 日志 ==========
static FILE* g_logFile = nullptr;

static void Log(const char* fmt, ...) {
    if (!g_logFile) {
        g_logFile = fopen("C:\\temp\\hook_debug.log", "a");
        if (!g_logFile) return;
    }
    va_list args;
    va_start(args, fmt);
    vfprintf(g_logFile, fmt, args);
    va_end(args);
    fflush(g_logFile);
}

// ========== 共享内存（跨进程通信） ==========
// 用于在 YonyouWorker 和 EnterprisePortal 之间共享 Alt 按下时间戳
struct SharedData {
    volatile LONG altPressTime;  // Alt 按下的时间戳 (GetTickCount)
};

static HANDLE g_hSharedMem = NULL;
static SharedData* g_pShared = nullptr;
static const wchar_t* SHARED_MEM_NAME = L"KingdeeFocusFix_SharedMem";
static const int ALT_BLOCK_WINDOW_MS = 500;  // Alt 按下后 500ms 内阻止焦点抢占

static void InitSharedMemory() {
    // 尝试打开已存在的共享内存，如果不存在则创建
    g_hSharedMem = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, SHARED_MEM_NAME);
    if (!g_hSharedMem) {
        g_hSharedMem = CreateFileMappingW(
            INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
            0, sizeof(SharedData), SHARED_MEM_NAME);
        Log("[InitSharedMemory] Created new shared memory: %p\n", g_hSharedMem);
    } else {
        Log("[InitSharedMemory] Opened existing shared memory: %p\n", g_hSharedMem);
    }

    if (g_hSharedMem) {
        g_pShared = (SharedData*)MapViewOfFile(g_hSharedMem, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData));
        Log("[InitSharedMemory] Mapped view: %p\n", g_pShared);
        if (g_pShared) {
            // 只有创建者才初始化为 0
            // InterlockedExchange(&g_pShared->altPressTime, 0);
        }
    } else {
        Log("[InitSharedMemory] FAILED to create/open shared memory\n");
    }
}

static void CleanupSharedMemory() {
    if (g_pShared) {
        UnmapViewOfFile(g_pShared);
        g_pShared = nullptr;
    }
    if (g_hSharedMem) {
        CloseHandle(g_hSharedMem);
        g_hSharedMem = NULL;
    }
}

// 记录 Alt 按下的时间
static void RecordAltPress() {
    if (g_pShared) {
        DWORD now = GetTickCount();
        InterlockedExchange(&g_pShared->altPressTime, now);
        Log("[RecordAltPress] Recorded time=%lu, g_pShared=%p\n", now, g_pShared);
    } else {
        Log("[RecordAltPress] FAILED: g_pShared is null\n");
    }
}

// 检查 Alt 是否在最近 N ms 内被按下过
static bool WasAltRecentlyPressed() {
    if (!g_pShared) {
        Log("[WasAltRecentlyPressed] FAILED: g_pShared is null\n");
        return false;
    }
    DWORD pressTime = (DWORD)InterlockedCompareExchange(&g_pShared->altPressTime, 0, 0);
    if (pressTime == 0) {
        Log("[WasAltRecentlyPressed] pressTime=0, returning false\n");
        return false;
    }
    DWORD now = GetTickCount();
    DWORD elapsed = now - pressTime;
    bool result = elapsed < ALT_BLOCK_WINDOW_MS;
    Log("[WasAltRecentlyPressed] pressTime=%lu, now=%lu, elapsed=%lu, result=%d\n",
        pressTime, now, elapsed, result);
    return result;
}

// ========== 共享数据段 ==========
#pragma data_seg(".SHARED")
HHOOK g_hCbtHook = NULL;
#pragma data_seg()
#pragma comment(linker, "/SECTION:.SHARED,RWS")

static HMODULE g_hMod = NULL;

// ========== MinHook 相关（拦截用友） ==========
typedef BOOL(WINAPI* pAttachThreadInput)(DWORD, DWORD, BOOL);
typedef BOOL(WINAPI* pBringWindowToTop)(HWND);
typedef BOOL(WINAPI* pSetForegroundWindow)(HWND);
typedef BOOL(WINAPI* pSetWindowPos)(HWND, HWND, int, int, int, int, UINT);
typedef HWND(WINAPI* pSetFocus)(HWND);
typedef HWND(WINAPI* pSetActiveWindow)(HWND);

static pAttachThreadInput fpAttachThreadInput = nullptr;
static pBringWindowToTop fpBringWindowToTop = nullptr;
static pSetForegroundWindow fpSetForegroundWindow = nullptr;
static pSetWindowPos fpSetWindowPos = nullptr;
static pSetFocus fpSetFocus = nullptr;
static pSetActiveWindow fpSetActiveWindow = nullptr;

static bool g_isYonyou = false;

static std::string GetCurrentProcessName() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    const char* pName = strrchr(path, '\\');
    return (pName ? pName + 1 : path);
}

static bool IsYonyouProcess() {
    std::string name = GetCurrentProcessName();
    return (_stricmp(name.c_str(), "EnterprisePortal.exe") == 0);
}

static bool IsAltDown() {
    return (GetAsyncKeyState(VK_LMENU) & 0x8000) ||
           (GetAsyncKeyState(VK_RMENU) & 0x8000);
}

// 检查是否应该阻止焦点抢占
// 条件：Alt 正在按下 OR Alt 在最近 500ms 内被按下过
static bool ShouldBlock() {
    return IsAltDown() || WasAltRecentlyPressed();
}

// ========== Hook Detours ==========

static BOOL WINAPI Detour_AttachThreadInput(DWORD idAttach, DWORD idAttachTo, BOOL fAttach) {
    bool block = ShouldBlock();
    Log("[AttachThreadInput] idAttach=%d, idAttachTo=%d, fAttach=%d, block=%d\n",
        idAttach, idAttachTo, fAttach, block);
    if (fAttach && block) {
        Log("[AttachThreadInput] BLOCKED!\n");
        return FALSE;
    }
    return fpAttachThreadInput(idAttach, idAttachTo, fAttach);
}

static BOOL WINAPI Detour_BringWindowToTop(HWND hWnd) {
    bool block = ShouldBlock();
    Log("[BringWindowToTop] hWnd=%p, block=%d\n", hWnd, block);
    if (block) {
        Log("[BringWindowToTop] BLOCKED!\n");
        return FALSE;
    }
    return fpBringWindowToTop(hWnd);
}

static BOOL WINAPI Detour_SetForegroundWindow(HWND hWnd) {
    bool block = ShouldBlock();
    Log("[SetForegroundWindow] hWnd=%p, block=%d\n", hWnd, block);
    if (block) {
        Log("[SetForegroundWindow] BLOCKED!\n");
        return FALSE;
    }
    return fpSetForegroundWindow(hWnd);
}

static BOOL WINAPI Detour_SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    bool block = ShouldBlock();
    bool isTop = (hWndInsertAfter == HWND_TOP || hWndInsertAfter == HWND_TOPMOST);
    Log("[SetWindowPos] hWnd=%p, insertAfter=%p, flags=%u, block=%d, isTop=%d\n",
        hWnd, hWndInsertAfter, uFlags, block, isTop);
    if (block && isTop) {
        Log("[SetWindowPos] BLOCKED!\n");
        return FALSE;
    }
    return fpSetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

static HWND WINAPI Detour_SetFocus(HWND hWnd) {
    bool block = ShouldBlock();
    Log("[SetFocus] hWnd=%p, block=%d\n", hWnd, block);
    if (block) {
        Log("[SetFocus] BLOCKED!\n");
        return NULL;
    }
    return fpSetFocus(hWnd);
}

static HWND WINAPI Detour_SetActiveWindow(HWND hWnd) {
    bool block = ShouldBlock();
    Log("[SetActiveWindow] hWnd=%p, block=%d\n", hWnd, block);
    if (block) {
        Log("[SetActiveWindow] BLOCKED!\n");
        return NULL;
    }
    return fpSetActiveWindow(hWnd);
}

static void InstallMinHookIfYonyou() {
    Log("[DllMain] DLL loaded in process: %s\n", GetCurrentProcessName().c_str());

    // 初始化共享内存
    InitSharedMemory();

    if (!IsYonyouProcess()) {
        Log("[DllMain] Not Yonyou process, skipping MinHook\n");
        return;
    }

    g_isYonyou = true;
    Log("[DllMain] Yonyou process detected! Installing MinHook...\n");

    MH_STATUS status = MH_Initialize();
    Log("[DllMain] MH_Initialize: %d\n", status);
    if (status != MH_OK) return;

    MH_CreateHookApi(L"user32.dll", "AttachThreadInput",
        &Detour_AttachThreadInput, (LPVOID*)&fpAttachThreadInput);
    Log("[DllMain] Hook AttachThreadInput: %p\n", fpAttachThreadInput);

    MH_CreateHookApi(L"user32.dll", "BringWindowToTop",
        &Detour_BringWindowToTop, (LPVOID*)&fpBringWindowToTop);
    Log("[DllMain] Hook BringWindowToTop: %p\n", fpBringWindowToTop);

    MH_CreateHookApi(L"user32.dll", "SetForegroundWindow",
        &Detour_SetForegroundWindow, (LPVOID*)&fpSetForegroundWindow);
    Log("[DllMain] Hook SetForegroundWindow: %p\n", fpSetForegroundWindow);

    MH_CreateHookApi(L"user32.dll", "SetWindowPos",
        &Detour_SetWindowPos, (LPVOID*)&fpSetWindowPos);
    Log("[DllMain] Hook SetWindowPos: %p\n", fpSetWindowPos);

    MH_CreateHookApi(L"user32.dll", "SetFocus",
        &Detour_SetFocus, (LPVOID*)&fpSetFocus);
    Log("[DllMain] Hook SetFocus: %p\n", fpSetFocus);

    MH_CreateHookApi(L"user32.dll", "SetActiveWindow",
        &Detour_SetActiveWindow, (LPVOID*)&fpSetActiveWindow);
    Log("[DllMain] Hook SetActiveWindow: %p\n", fpSetActiveWindow);

    status = MH_EnableHook(MH_ALL_HOOKS);
    Log("[DllMain] MH_EnableHook: %d\n", status);
}

static void UninstallMinHookIfYonyou() {
    if (g_isYonyou) {
        Log("[DllMain] Uninstalling MinHook\n");
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }
    CleanupSharedMemory();
    if (g_logFile) {
        fclose(g_logFile);
        g_logFile = nullptr;
    }
}

// ========== CBT 钩子相关（拦截金蝶 + 记录 Alt 时间戳） ==========
static bool IsKingdeeReport(HWND hwnd) {
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (!processId) return false;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    bool found = false;

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (pe.th32ProcessID == processId) {
                wchar_t name[MAX_PATH];
                int i = 0;
                for (i = 0; pe.szExeFile[i]; i++)
                    name[i] = (wchar_t)towlower(pe.szExeFile[i]);
                name[i] = 0;
                found = (wcscmp(name, L"kingdee.bos.kdsreport.exe") == 0);
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return found;
}

static LRESULT CALLBACK CbtProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HCBT_ACTIVATE) {
        HWND hwnd = (HWND)wParam;
        bool altDown = IsAltDown();
        bool isKingdee = IsKingdeeReport(hwnd);
        Log("[CbtProc] HCBT_ACTIVATE hwnd=%p, altDown=%d, isKingdee=%d\n", hwnd, altDown, isKingdee);

        // 记录 Alt 按下的时间戳（用于用友的延迟焦点抢占）
        if (altDown) {
            RecordAltPress();
            Log("[CbtProc] Alt pressed, recorded timestamp\n");
        }

        if (altDown && isKingdee) {
            Log("[CbtProc] BLOCKED Kingdee!\n");
            return 1;
        }
    }
    return CallNextHookEx(g_hCbtHook, code, wParam, lParam);
}

// ========== DLL 入口 ==========
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        g_hMod = hModule;
        InstallMinHookIfYonyou();
        break;
    case DLL_PROCESS_DETACH:
        UninstallMinHookIfYonyou();
        break;
    }
    return TRUE;
}

// ========== 导出函数（供 C# 宿主调用） ==========
extern "C" HOOKDLL_API BOOL InstallCbtHook(DWORD threadId) {
    Log("[InstallCbtHook] threadId=%d\n", threadId);
    if (g_hCbtHook) return TRUE;
    g_hCbtHook = SetWindowsHookEx(WH_CBT, CbtProc, g_hMod, threadId);
    Log("[InstallCbtHook] g_hCbtHook=%p\n", g_hCbtHook);
    return g_hCbtHook != NULL;
}

extern "C" HOOKDLL_API void UninstallCbtHook() {
    Log("[UninstallCbtHook]\n");
    if (g_hCbtHook) {
        UnhookWindowsHookEx(g_hCbtHook);
        g_hCbtHook = NULL;
    }
}
