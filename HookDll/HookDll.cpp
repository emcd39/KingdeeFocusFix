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
static const int ALT_BLOCK_WINDOW_MS = 2000;

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
typedef void (WINAPI* pSwitchToThisWindow)(HWND, BOOL);
typedef BOOL(WINAPI* pShowWindow)(HWND, int);
typedef BOOL(WINAPI* pMoveWindow)(HWND, int, int, int, int, BOOL);

static pAttachThreadInput fpAttachThreadInput = nullptr;
static pBringWindowToTop fpBringWindowToTop = nullptr;
static pSetForegroundWindow fpSetForegroundWindow = nullptr;
static pSetWindowPos fpSetWindowPos = nullptr;
static pSetFocus fpSetFocus = nullptr;
static pSetActiveWindow fpSetActiveWindow = nullptr;
static pSwitchToThisWindow fpSwitchToThisWindow = nullptr;
static pShowWindow fpShowWindow = nullptr;
static pMoveWindow fpMoveWindow = nullptr;

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

// 检查并阻止：如果 Alt 在最近 N ms 内被按下过，则阻止并刷新时间戳
static bool ShouldBlock() {
    if (!g_pShared) return false;
    DWORD pressTime = g_pShared->altPressTime;
    if (pressTime == 0) return false;
    DWORD now = GetTickCount();
    DWORD elapsed = now - pressTime;
    if (elapsed < ALT_BLOCK_WINDOW_MS) {
        g_pShared->altPressTime = now;
        Log("[ShouldBlock] BLOCK elapsed=%lu, wrote=%lu\n", elapsed, now);
        return true;
    }
    return false;
}

// ========== Hook Detours ==========

static BOOL WINAPI Detour_AttachThreadInput(DWORD idAttach, DWORD idAttachTo, BOOL fAttach) {
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
    return fpSetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

static HWND WINAPI Detour_SetFocus(HWND hWnd) {
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

// SwitchToThisWindow - 关键！用友通过这个 API 绕过我们的 Hook
static void WINAPI Detour_SwitchToThisWindow(HWND hwnd, BOOL fAltTab) {
    bool block = ShouldBlock();
    Log("[SwitchToThisWindow] hwnd=%p, fAltTab=%d, block=%d\n", hwnd, fAltTab, block);
    if (block) {
        Log("[SwitchToThisWindow] BLOCKED!\n");
        return;
    }
    fpSwitchToThisWindow(hwnd, fAltTab);
}

static BOOL WINAPI Detour_ShowWindow(HWND hWnd, int nCmdShow) {
    return fpShowWindow(hWnd, nCmdShow);
}

static BOOL WINAPI Detour_MoveWindow(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint) {
    return fpMoveWindow(hWnd, X, Y, nWidth, nHeight, bRepaint);
}

// ========== 窗口消息监控 ==========
static HHOOK g_hCallWndProcHook = NULL;

static LRESULT CALLBACK CallWndProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        CWPSTRUCT* pMsg = (CWPSTRUCT*)lParam;
        if (pMsg->message == WM_ACTIVATE) {
            DWORD pid = 0;
            GetWindowThreadProcessId(pMsg->hwnd, &pid);
            Log("[CallWndProc] WM_ACTIVATE hwnd=%p, wParam=%p, lParam=%p, pid=%lu\n",
                pMsg->hwnd, (void*)pMsg->wParam, (void*)pMsg->lParam, pid);
        }
    }
    return CallNextHookEx(g_hCallWndProcHook, nCode, wParam, lParam);
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

    // SwitchToThisWindow - 关键！用友通过这个 API 绕过我们的 Hook
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        // 尝试通过名字获取
        fpSwitchToThisWindow = (pSwitchToThisWindow)GetProcAddress(hUser32, "SwitchToThisWindow");
        if (!fpSwitchToThisWindow) {
            // 尝试通过 ordinal 获取（常见 ordinal: 0x5E9）
            fpSwitchToThisWindow = (pSwitchToThisWindow)GetProcAddress(hUser32, MAKEINTRESOURCEA(0x5E9));
        }
        if (fpSwitchToThisWindow) {
            MH_CreateHook(fpSwitchToThisWindow, &Detour_SwitchToThisWindow, (LPVOID*)&fpSwitchToThisWindow);
            Log("[DllMain] Hook SwitchToThisWindow: %p\n", fpSwitchToThisWindow);
        } else {
            Log("[DllMain] SwitchToThisWindow not found!\n");
        }
    }

    MH_CreateHookApi(L"user32.dll", "ShowWindow",
        &Detour_ShowWindow, (LPVOID*)&fpShowWindow);
    Log("[DllMain] Hook ShowWindow: %p\n", fpShowWindow);

    MH_CreateHookApi(L"user32.dll", "MoveWindow",
        &Detour_MoveWindow, (LPVOID*)&fpMoveWindow);
    Log("[DllMain] Hook MoveWindow: %p\n", fpMoveWindow);

    status = MH_EnableHook(MH_ALL_HOOKS);
    Log("[DllMain] MH_EnableHook: %d\n", status);

    // 安装 WH_CALLWNDPROC 钩子监控窗口消息
    g_hCallWndProcHook = SetWindowsHookEx(WH_CALLWNDPROC, CallWndProc, g_hMod, 0);
    Log("[DllMain] WH_CALLWNDPROC hook installed: %p\n", g_hCallWndProcHook);
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

// 检查目标窗口是否属于用友进程
static bool IsYonyouWindow(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return false;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    bool found = false;

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                wchar_t name[MAX_PATH];
                int i = 0;
                for (i = 0; pe.szExeFile[i]; i++)
                    name[i] = (wchar_t)towlower(pe.szExeFile[i]);
                name[i] = 0;
                found = (wcscmp(name, L"enterpriseportal.exe") == 0);
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return found;
}

// 检查共享内存中 Alt 是否在最近 N ms 内被按下过（用于 CBT 钩子）
static bool WasAltRecentlyPressedCBT() {
    if (!g_pShared) return false;
    DWORD pressTime = g_pShared->altPressTime;
    if (pressTime == 0) return false;
    DWORD now = GetTickCount();
    DWORD elapsed = now - pressTime;
    return elapsed < ALT_BLOCK_WINDOW_MS;
}

static HWND g_hLastForeground = NULL;  // 记录上一个前台窗口
static HWINEVENTHOOK g_hEventHook = NULL;  // 焦点监控钩子

static LRESULT CALLBACK CbtProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HCBT_ACTIVATE) {
        HWND hwnd = (HWND)wParam;
        HWND hwndActive = GetForegroundWindow();
        bool altDown = IsAltDown();
        bool isKingdee = IsKingdeeReport(hwnd);
        bool isYonyou = IsYonyouWindow(hwnd);
        bool altRecent = WasAltRecentlyPressedCBT();
        bool wasYonyouActive = IsYonyouWindow(hwndActive);

        Log("[CbtProc] HCBT_ACTIVATE hwnd=%p, altDown=%d, isKingdee=%d, isYonyou=%d, altRecent=%d, wasYonyouActive=%d\n",
            hwnd, altDown, isKingdee, isYonyou, altRecent, wasYonyouActive);

        if (altDown) {
            RecordAltPress();
            Log("[CbtProc] Alt pressed, recorded timestamp\n");
        }

        // 阻止金蝶
        if (altDown && isKingdee) {
            Log("[CbtProc] BLOCKED Kingdee!\n");
            return 1;
        }

        // 用友窗口即将被激活
        if (isYonyou && (altDown || altRecent)) {
            // 虽然无法阻止激活，但可以立即切回上一个窗口
            if (hwndActive && !IsYonyouWindow(hwndActive)) {
                g_hLastForeground = hwndActive;
                Log("[CbtProc] Yonyou stealing focus, will restore to %p\n", g_hLastForeground);
                // 使用 PostMessage 异步恢复焦点（避免死锁）
                PostMessage(hwndActive, WM_ACTIVATE, WA_ACTIVE, 0);
            }
        }
    }
    return CallNextHookEx(g_hCbtHook, code, wParam, lParam);
}

// ========== 焦点监控回调 ==========
static void CALLBACK WinEventProc(HWINEVENTHOOK hHook, DWORD event, HWND hwnd,
    LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    if (event != EVENT_SYSTEM_FOREGROUND) return;
    
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    
    // 检查是否是用友窗口获得焦点
    bool isYonyou = false;
    if (pid) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe;
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    if (pe.th32ProcessID == pid) {
                        wchar_t name[MAX_PATH];
                        int j = 0;
                        for (j = 0; pe.szExeFile[j]; j++)
                            name[j] = (wchar_t)towlower(pe.szExeFile[j]);
                        name[j] = 0;
                        isYonyou = (wcscmp(name, L"enterpriseportal.exe") == 0);
                        break;
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }
    }
    
    if (isYonyou && WasAltRecentlyPressedCBT()) {
        Log("[WinEventProc] Yonyou got focus during Alt+Tab, restoring previous window\n");
        HWND hPrev = g_hLastForeground;
        if (hPrev && IsWindow(hPrev)) {
            SetForegroundWindow(hPrev);
        }
    }
    
    // 记录非用友窗口作为"上一个前台窗口"
    if (!isYonyou) {
        g_hLastForeground = hwnd;
    }
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
    
    // 安装焦点监控钩子
    g_hEventHook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        NULL, WinEventProc, 0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    Log("[InstallCbtHook] g_hEventHook=%p\n", g_hEventHook);
    
    return g_hCbtHook != NULL;
}

extern "C" HOOKDLL_API void UninstallCbtHook() {
    Log("[UninstallCbtHook]\n");
    if (g_hEventHook) {
        UnhookWinEvent(g_hEventHook);
        g_hEventHook = NULL;
    }
    if (g_hCbtHook) {
        UnhookWindowsHookEx(g_hCbtHook);
        g_hCbtHook = NULL;
    }
}
