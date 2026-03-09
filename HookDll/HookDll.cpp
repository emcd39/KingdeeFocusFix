#define HOOKDLL_EXPORTS
#include <windows.h>
#include <tlhelp32.h>
#include "HookDll.h"

#pragma data_seg(".SHARED")
HHOOK g_hook = NULL;
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
    return g_hook != NULL;
}

extern "C" HOOKDLL_API BOOL UninstallHook()
{
    if (!g_hook) return TRUE;
    BOOL ok = UnhookWindowsHookEx(g_hook);
    g_hook = NULL;
    return ok;
}
