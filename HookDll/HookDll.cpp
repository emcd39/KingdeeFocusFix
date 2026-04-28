#define HOOKDLL_EXPORTS
#include <windows.h>
#include <tlhelp32.h>
#include "HookDll.h"

#pragma data_seg(".SHARED")
HHOOK g_hook = NULL;
HHOOK g_keyboardHook = NULL;
#pragma data_seg()
#pragma comment(linker, "/SECTION:.SHARED,RWS")

static HMODULE g_hMod = NULL;

static bool IsKDSReport(HWND hwnd)
{
    if (!hwnd) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return false;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    bool found = false;

    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                // 转小写
                wchar_t name[MAX_PATH];
                int i = 0;
                for (i = 0; pe.szExeFile[i]; i++)
                    name[i] = (wchar_t)towlower(pe.szExeFile[i]);
                name[i] = 0;
                found = (wcscmp(name, L"kingdee.bos.kdsreport.exe") == 0);
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

static bool IsYonyou(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return false;

    wchar_t className[256] = {0};
    int len = GetClassNameW(hwnd, className, ARRAYSIZE(className));
    if (len == 0)
        return false;

    if (wcscmp(className, L"ThunderRT6FormDC") != 0)
        return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return false;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProcess) return false;

    wchar_t processPath[MAX_PATH] = {0};
    DWORD pathLen = MAX_PATH;
    bool found = false;

    if (QueryFullProcessImageNameW(hProcess, 0, processPath, &pathLen))
    {
        wchar_t lowerPath[MAX_PATH] = {0};
        for (DWORD i = 0; i < pathLen && i < MAX_PATH - 1; i++)
            lowerPath[i] = (wchar_t)towlower(processPath[i]);
        lowerPath[pathLen] = 0;

        if (wcsstr(lowerPath, L"enterpriseportal.exe") != NULL)
            found = true;
    }

    CloseHandle(hProcess);
    return found;
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

static LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION)
    {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;

        if (wParam == WM_KEYDOWN && p->vkCode == VK_TAB)
        {
            bool altDown = ((GetAsyncKeyState(VK_LMENU) & 0x8000) != 0)
                        || ((GetAsyncKeyState(VK_RMENU) & 0x8000) != 0);

            if (altDown)
            {
                HWND hwnd = GetForegroundWindow();
                if (IsYonyou(hwnd))
                {
                    INPUT input = {};
                    input.type = INPUT_KEYBOARD;
                    input.ki.wVk = VK_TAB;
                    SendInput(1, &input, sizeof(INPUT));

                    input.ki.dwFlags = KEYEVENTF_KEYUP;
                    SendInput(1, &input, sizeof(INPUT));

                    return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
                }
            }
        }
    }
    return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
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
    return g_hook != NULL;
}

extern "C" HOOKDLL_API BOOL UninstallHook()
{
    if (!g_hook) return TRUE;
    BOOL ok = UnhookWindowsHookEx(g_hook);
    g_hook = NULL;
    return ok;
}

extern "C" HOOKDLL_API BOOL InstallKeyboardHook()
{
    if (g_keyboardHook) return TRUE;
    g_keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, g_hMod, 0);
    return g_keyboardHook != NULL;
}

extern "C" HOOKDLL_API BOOL UninstallKeyboardHook()
{
    if (!g_keyboardHook) return TRUE;
    BOOL ok = UnhookWindowsHookEx(g_keyboardHook);
    g_keyboardHook = NULL;
    return ok;
}
