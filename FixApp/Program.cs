using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;

class Program
{
    [DllImport("HookDll.dll")] static extern bool InstallHook();
    [DllImport("HookDll.dll")] static extern bool UninstallHook();

    [STAThread]
    static void Main()
    {
        Application.EnableVisualStyles();

        // HookDll.dll 必须和 exe 在同一目录
        string dllPath = Path.Combine(AppContext.BaseDirectory, "HookDll.dll");
        if (!File.Exists(dllPath))
        {
            MessageBox.Show(
                $"找不到 HookDll.dll，请确保它和 exe 在同一目录。\n\n期望路径：{dllPath}",
                "金蝶焦点修复", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }

        if (!InstallHook())
        {
            MessageBox.Show(
                "钩子安装失败！\n请确认以【管理员身份】运行。",
                "金蝶焦点修复", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }

        using var tray = new NotifyIcon
        {
            Icon    = SystemIcons.Shield,
            Text    = "金蝶报表焦点修复（运行中）",
            Visible = true,
        };
        var menu = new ContextMenuStrip();
        menu.Items.Add("金蝶报表焦点修复 · 运行中").Enabled = false;
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("退出", null, (_, _) => Application.Exit());
        tray.ContextMenuStrip = menu;

        Application.Run();

        UninstallHook();
    }
}
