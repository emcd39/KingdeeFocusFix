#define HOOKDLL_EXPORTS
#include <windows.h>
#include <tlhelp32.h>
#include "HookDll.h"

#pragma data_seg(".SHARED")
HHOOK g_hook = NULL;
#pragma data_seg()
#pragma comment(linker, "/SECTION:.SHARED,RWS")

static HMODULE g_hMod = NULL;
static HHOOK g_kbHook = NULL;
static bool g_altDown = false;
static bool g_suppressNext = false;

static bool GetProcessName(DWORD pid, wchar_t* name, int maxLen)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    bool found = false;

    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                int i = 0;
                for (i = 0; pe.szExeFile[i] && i < maxLen - 1; i++)
                    name[i] = (wchar_t)towlower(pe.szExeFile[i]);
                name[i] = 0;
                found = true;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

static bool IsKDSReport(HWND hwnd)
{
    if (!hwnd) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return false;

    wchar_t name[MAX_PATH];
    if (!GetProcessName(pid, name, MAX_PATH)) return false;
    return (wcscmp(name, L"kingdee.bos.kdsreport.exe") == 0);
}

static bool IsYonyouWindow(HWND hwnd)
{
    if (!hwnd) return false;

    wchar_t className[256];
    if (!GetClassNameW(hwnd, className, 256)) return false;
    if (_wcsicmp(className, L"ThunderRT6FormDC") != 0) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return false;

    wchar_t name[MAX_PATH];
    if (!GetProcessName(pid, name, MAX_PATH)) return false;
    return (wcscmp(name, L"enterpriseportal.exe") == 0);
}

static LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && !g_suppressNext)
    {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;

        if (kb->vkCode == VK_LMENU || kb->vkCode == VK_RMENU)
        {
            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
                g_altDown = true;
            else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
                g_altDown = false;
        }

        if (kb->vkCode == VK_TAB && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) && g_altDown)
        {
            HWND fgWnd = GetForegroundWindow();
            if (IsYonyouWindow(fgWnd))
            {
                // 防止递归触发
                g_suppressNext = true;

                // 发送Alt+Tab+Tab（跳过一个窗口）
                INPUT inputs[4] = {};

                inputs[0].type = INPUT_KEYBOARD;
                inputs[0].ki.wVk = VK_MENU;

                inputs[1].type = INPUT_KEYBOARD;
                inputs[1].ki.wVk = VK_TAB;

                inputs[2].type = INPUT_KEYBOARD;
                inputs[2].ki.wVk = VK_TAB;

                inputs[3].type = INPUT_KEYBOARD;
                inputs[3].ki.wVk = VK_MENU;
                inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

                SendInput(4, inputs, sizeof(INPUT));

                g_suppressNext = false;

                return 1;
            }
        }
    }
    return CallNextHookEx(g_kbHook, code, wParam, lParam);
}

static LRESULT CALLBACK CbtProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HCBT_ACTIVATE)
    {
        HWND hwnd = (HWND)wParam;
        bool altDown = ((GetAsyncKeyState(VK_LMENU) & 0x8000) != 0)
                    || ((GetAsyncKeyState(VK_RMENU) & 0x8000) != 0);

        if (altDown && IsKDSReport(hwnd))
        {
            return 1;
        }
    }
    return CallNextHookEx(g_hook, code, wParam, lParam);
}

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        g_hMod = hMod;
    return TRUE;
}

extern "C" HOOKDLL_API BOOL InstallHook()
{
    if (g_hook) return TRUE;

    g_hook = SetWindowsHookEx(WH_CBT, CbtProc, g_hMod, 0);
    if (!g_hook) return FALSE;

    g_kbHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, g_hMod, 0);
    if (!g_kbHook)
    {
        UnhookWindowsHookEx(g_hook);
        g_hook = NULL;
        return FALSE;
    }

    return TRUE;
}

extern "C" HOOKDLL_API BOOL UninstallHook()
{
    BOOL ok = TRUE;

    if (g_kbHook)
    {
        ok = UnhookWindowsHookEx(g_kbHook) && ok;
        g_kbHook = NULL;
    }

    if (g_hook)
    {
        ok = UnhookWindowsHookEx(g_hook) && ok;
        g_hook = NULL;
    }

    g_altDown = false;
    return ok;
}
