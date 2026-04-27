#pragma once

#ifdef HOOKDLL_EXPORTS
#define HOOKDLL_API __declspec(dllexport)
#else
#define HOOKDLL_API __declspec(dllimport)
#endif

extern "C" {
    HOOKDLL_API BOOL InstallCbtHook(DWORD threadId);
    HOOKDLL_API void UninstallCbtHook();
}
