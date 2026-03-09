using System;
using System.Drawing;
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

        string dllPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "HookDll.dll");
        if (!File.Exists(dllPath))
        {
            MessageBox.Show(
                "找不到 HookDll.dll，请确保它和 exe 在同一目录。\n\n期望路径：" + dllPath,
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

        NotifyIcon tray = new NotifyIcon();
        tray.Icon = SystemIcons.Shield;
        tray.Text = "金蝶报表焦点修复（运行中）";
        tray.Visible = true;

        ContextMenuStrip menu = new ContextMenuStrip();
        ToolStripMenuItem title = new ToolStripMenuItem("金蝶报表焦点修复 · 运行中");
        title.Enabled = false;
        menu.Items.Add(title);
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("退出", null, (s, e) => Application.Exit());
        tray.ContextMenuStrip = menu;

        Application.Run();

        UninstallHook();
        tray.Visible = false;
    }
}
