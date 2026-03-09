#pragma once
#ifdef HOOKDLL_EXPORTS
#define HOOKDLL_API __declspec(dllexport)
#else
#define HOOKDLL_API __declspec(dllimport)
#endif

extern "C" {
    HOOKDLL_API BOOL InstallHook();
    HOOKDLL_API BOOL UninstallHook();
}
