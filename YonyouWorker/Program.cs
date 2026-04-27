using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

namespace YonyouWorker
{
    class Program
    {
        [DllImport("HookDll32.dll", CallingConvention = CallingConvention.Cdecl)]
        static extern bool InstallCbtHook(uint threadId);

        [DllImport("HookDll32.dll", CallingConvention = CallingConvention.Cdecl)]
        static extern void UninstallCbtHook();

        static void Main(string[] args)
        {
            if (!IsAdmin())
            {
                Console.Error.WriteLine("[YonyouWorker] 需要管理员权限");
                Environment.Exit(1);
            }

            string dllPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "HookDll32.dll");
            if (!File.Exists(dllPath))
            {
                Console.Error.WriteLine($"[YonyouWorker] 找不到 DLL: {dllPath}");
                Environment.Exit(2);
            }

            if (!InstallCbtHook(0))
            {
                Console.Error.WriteLine("[YonyouWorker] 钩子安装失败");
                Environment.Exit(3);
            }

            Console.WriteLine("[YonyouWorker] 用友 MinHook 钩子已安装");

            // 无限等待，直到被父进程终止
            Thread.Sleep(Timeout.Infinite);
        }

        static bool IsAdmin()
        {
            using (var identity = System.Security.Principal.WindowsIdentity.GetCurrent())
            {
                var principal = new System.Security.Principal.WindowsPrincipal(identity);
                return principal.IsInRole(System.Security.Principal.WindowsBuiltInRole.Administrator);
            }
        }
    }
}
