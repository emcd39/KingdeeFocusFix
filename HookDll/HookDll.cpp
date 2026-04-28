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
static bool g_realAltDown = false;

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

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return false;

    wchar_t name[MAX_PATH];
    if (!GetProcessName(pid, name, MAX_PATH)) return false;

    return wcsstr(className, L"ThunderRT6") != NULL && wcscmp(name, L"enterpriseportal.exe") == 0;
}

static LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION)
    {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        bool isInjected = (kb->flags & LLKHF_INJECTED) != 0;

        if (!isInjected)
        {
            if (kb->vkCode == VK_LMENU || kb->vkCode == VK_RMENU)
            {
                if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
                    g_realAltDown = true;
                else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
                    g_realAltDown = false;
            }

            if (kb->vkCode == VK_TAB && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) && g_realAltDown)
            {
                HWND fgWnd = GetForegroundWindow();
                if (IsYonyouWindow(fgWnd))
                {
                    INPUT inputs[1] = {};
                    inputs[0].type = INPUT_KEYBOARD;
                    inputs[0].ki.wVk = VK_TAB;

                    SendInput(1, inputs, sizeof(INPUT));
                }
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

    g_realAltDown = false;
    return ok;
}
