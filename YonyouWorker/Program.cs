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

        [DllImport("user32.dll")]
        static extern int GetMessage(out MSG lpMsg, IntPtr hWnd, uint wMsgFilterMin, uint wMsgFilterMax);

        [DllImport("user32.dll")]
        static extern bool TranslateMessage(ref MSG lpMsg);

        [DllImport("user32.dll")]
        static extern IntPtr DispatchMessage(ref MSG lpMsg);

        [StructLayout(LayoutKind.Sequential)]
        public struct MSG
        {
            public IntPtr hwnd;
            public uint message;
            public IntPtr wParam;
            public IntPtr lParam;
            public uint time;
            public System.Drawing.Point pt;
        }

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

            Console.WriteLine("[YonyouWorker] 用友钩子已安装，消息循环运行中...");

            // 消息循环 - 处理 SetWinEventHook 的回调
            MSG msg;
            while (GetMessage(out msg, IntPtr.Zero, 0, 0) > 0)
            {
                TranslateMessage(ref msg);
                DispatchMessage(ref msg);
            }
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
