#define WIN32_LEAN_AND_MEAN
#define HOOKDLL_EXPORTS
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include "MinHook.h"
#include "HookDll.h"

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

// ========== Hook Detours ==========

// Hook AttachThreadInput - 阻止用友在 Alt+Tab 时连接线程输入
static BOOL WINAPI Detour_AttachThreadInput(DWORD idAttach, DWORD idAttachTo, BOOL fAttach) {
    if (fAttach && IsAltDown()) {
        return FALSE;
    }
    return fpAttachThreadInput(idAttach, idAttachTo, fAttach);
}

// Hook BringWindowToTop - 阻止用友在 Alt+Tab 时抢占前台
static BOOL WINAPI Detour_BringWindowToTop(HWND hWnd) {
    if (IsAltDown()) {
        return FALSE;
    }
    return fpBringWindowToTop(hWnd);
}

// Hook SetForegroundWindow - 阻止用友强制设置前台窗口
static BOOL WINAPI Detour_SetForegroundWindow(HWND hWnd) {
    if (IsAltDown()) {
        return FALSE;
    }
    return fpSetForegroundWindow(hWnd);
}

// Hook SetWindowPos - 阻止用友在 Alt+Tab 时置顶窗口
static BOOL WINAPI Detour_SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    if (IsAltDown() && (hWndInsertAfter == HWND_TOP || hWndInsertAfter == HWND_TOPMOST)) {
        return FALSE;
    }
    return fpSetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

// Hook SetFocus - 阻止用友在 Alt+Tab 时抢夺焦点
static HWND WINAPI Detour_SetFocus(HWND hWnd) {
    if (IsAltDown()) {
        return NULL;
    }
    return fpSetFocus(hWnd);
}

// Hook SetActiveWindow - 阻止用友在 Alt+Tab 时激活窗口
static HWND WINAPI Detour_SetActiveWindow(HWND hWnd) {
    if (IsAltDown()) {
        return NULL;
    }
    return fpSetActiveWindow(hWnd);
}

static void InstallMinHookIfYonyou() {
    if (!IsYonyouProcess()) return;

    g_isYonyou = true;

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) return;

    // Hook 所有焦点相关 API
    MH_CreateHookApi(L"user32.dll", "AttachThreadInput",
        &Detour_AttachThreadInput, (LPVOID*)&fpAttachThreadInput);

    MH_CreateHookApi(L"user32.dll", "BringWindowToTop",
        &Detour_BringWindowToTop, (LPVOID*)&fpBringWindowToTop);

    MH_CreateHookApi(L"user32.dll", "SetForegroundWindow",
        &Detour_SetForegroundWindow, (LPVOID*)&fpSetForegroundWindow);

    MH_CreateHookApi(L"user32.dll", "SetWindowPos",
        &Detour_SetWindowPos, (LPVOID*)&fpSetWindowPos);

    MH_CreateHookApi(L"user32.dll", "SetFocus",
        &Detour_SetFocus, (LPVOID*)&fpSetFocus);

    MH_CreateHookApi(L"user32.dll", "SetActiveWindow",
        &Detour_SetActiveWindow, (LPVOID*)&fpSetActiveWindow);

    MH_EnableHook(MH_ALL_HOOKS);
}

static void UninstallMinHookIfYonyou() {
    if (g_isYonyou) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }
}

// ========== CBT 钩子相关（拦截金蝶） ==========
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
        if (IsAltDown() && IsKingdeeReport(hwnd)) {
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
    if (g_hCbtHook) return TRUE;
    g_hCbtHook = SetWindowsHookEx(WH_CBT, CbtProc, g_hMod, threadId);
    return g_hCbtHook != NULL;
}

extern "C" HOOKDLL_API void UninstallCbtHook() {
    if (g_hCbtHook) {
        UnhookWindowsHookEx(g_hCbtHook);
        g_hCbtHook = NULL;
    }
}
