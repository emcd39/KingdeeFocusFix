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

static BOOL WINAPI Detour_AttachThreadInput(DWORD idAttach, DWORD idAttachTo, BOOL fAttach) {
    bool altDown = IsAltDown();
    Log("[AttachThreadInput] idAttach=%d, idAttachTo=%d, fAttach=%d, altDown=%d\n",
        idAttach, idAttachTo, fAttach, altDown);
    if (fAttach && altDown) {
        Log("[AttachThreadInput] BLOCKED!\n");
        return FALSE;
    }
    return fpAttachThreadInput(idAttach, idAttachTo, fAttach);
}

static BOOL WINAPI Detour_BringWindowToTop(HWND hWnd) {
    bool altDown = IsAltDown();
    Log("[BringWindowToTop] hWnd=%p, altDown=%d\n", hWnd, altDown);
    if (altDown) {
        Log("[BringWindowToTop] BLOCKED!\n");
        return FALSE;
    }
    return fpBringWindowToTop(hWnd);
}

static BOOL WINAPI Detour_SetForegroundWindow(HWND hWnd) {
    bool altDown = IsAltDown();
    Log("[SetForegroundWindow] hWnd=%p, altDown=%d\n", hWnd, altDown);
    if (altDown) {
        Log("[SetForegroundWindow] BLOCKED!\n");
        return FALSE;
    }
    return fpSetForegroundWindow(hWnd);
}

static BOOL WINAPI Detour_SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    bool altDown = IsAltDown();
    bool isTop = (hWndInsertAfter == HWND_TOP || hWndInsertAfter == HWND_TOPMOST);
    Log("[SetWindowPos] hWnd=%p, insertAfter=%p, flags=%u, altDown=%d, isTop=%d\n",
        hWnd, hWndInsertAfter, uFlags, altDown, isTop);
    if (altDown && isTop) {
        Log("[SetWindowPos] BLOCKED!\n");
        return FALSE;
    }
    return fpSetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

static HWND WINAPI Detour_SetFocus(HWND hWnd) {
    bool altDown = IsAltDown();
    Log("[SetFocus] hWnd=%p, altDown=%d\n", hWnd, altDown);
    if (altDown) {
        Log("[SetFocus] BLOCKED!\n");
        return NULL;
    }
    return fpSetFocus(hWnd);
}

static HWND WINAPI Detour_SetActiveWindow(HWND hWnd) {
    bool altDown = IsAltDown();
    Log("[SetActiveWindow] hWnd=%p, altDown=%d\n", hWnd, altDown);
    if (altDown) {
        Log("[SetActiveWindow] BLOCKED!\n");
        return NULL;
    }
    return fpSetActiveWindow(hWnd);
}

static void InstallMinHookIfYonyou() {
    Log("[DllMain] DLL loaded in process: %s\n", GetCurrentProcessName().c_str());

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
    if (g_logFile) {
        fclose(g_logFile);
        g_logFile = nullptr;
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
        bool altDown = IsAltDown();
        bool isKingdee = IsKingdeeReport(hwnd);
        Log("[CbtProc] HCBT_ACTIVATE hwnd=%p, altDown=%d, isKingdee=%d\n", hwnd, altDown, isKingdee);
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
