using System;
using System.Diagnostics;
using System.Drawing;
using System.Windows.Forms;

namespace YonyouAndKingdeeFix
{
    class LauncherProgram
    {
        static Process kingdeeProc, yonyouProc;
        static NotifyIcon trayIcon;
        static bool exiting = false;

        [STAThread]
        static void Main()
        {
            if (!IsAdmin())
            {
                MessageBox.Show(
                    "请以管理员身份运行本程序。",
                    "需要管理员权限",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Exclamation);
                return;
            }

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            trayIcon = new NotifyIcon
            {
                Icon = SystemIcons.Shield,
                Text = "金蝶/用友焦点修复 - 未启动",
                Visible = true,
                ContextMenuStrip = CreateMenu()
            };

            StartWorkers();

            Application.Run();
        }

        static ContextMenuStrip CreateMenu()
        {
            var menu = new ContextMenuStrip();

            var titleItem = new ToolStripMenuItem("金蝶/用友焦点修复");
            titleItem.Enabled = false;
            menu.Items.Add(titleItem);
            menu.Items.Add(new ToolStripSeparator());

            menu.Items.Add("启用修复", null, (s, e) => StartWorkers());
            menu.Items.Add("停止修复", null, (s, e) => StopWorkers());
            menu.Items.Add(new ToolStripSeparator());
            menu.Items.Add("退出", null, (s, e) => ExitApplication());

            return menu;
        }

        static void StartWorkers()
        {
            if ((kingdeeProc != null && !kingdeeProc.HasExited) ||
                (yonyouProc != null && !yonyouProc.HasExited))
            {
                MessageBox.Show(
                    "修复已处于运行状态。",
                    "提示",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Information);
                return;
            }

            try
            {
                string baseDir = AppDomain.CurrentDomain.BaseDirectory;

                kingdeeProc = new Process
                {
                    StartInfo = new ProcessStartInfo
                    {
                        FileName = System.IO.Path.Combine(baseDir, "KingdeeWorker.exe"),
                        WorkingDirectory = baseDir,
                        UseShellExecute = false,
                        CreateNoWindow = true,
                        WindowStyle = ProcessWindowStyle.Hidden
                    }
                };
                kingdeeProc.EnableRaisingEvents = true;
                kingdeeProc.Exited += OnWorkerExited;
                kingdeeProc.Start();

                yonyouProc = new Process
                {
                    StartInfo = new ProcessStartInfo
                    {
                        FileName = System.IO.Path.Combine(baseDir, "YonyouWorker.exe"),
                        WorkingDirectory = baseDir,
                        UseShellExecute = false,
                        CreateNoWindow = true,
                        WindowStyle = ProcessWindowStyle.Hidden
                    }
                };
                yonyouProc.EnableRaisingEvents = true;
                yonyouProc.Exited += OnWorkerExited;
                yonyouProc.Start();

                trayIcon.Text = "金蝶/用友焦点修复 - 运行中";
            }
            catch (Exception ex)
            {
                MessageBox.Show(
                    $"启动修复进程失败：{ex.Message}",
                    "错误",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
                StopWorkers();
            }
        }

        static void StopWorkers()
        {
            try
            {
                if (kingdeeProc != null && !kingdeeProc.HasExited)
                    kingdeeProc.Kill();
                if (yonyouProc != null && !yonyouProc.HasExited)
                    yonyouProc.Kill();
            }
            catch { }
            finally
            {
                kingdeeProc?.Dispose();
                yonyouProc?.Dispose();
                kingdeeProc = null;
                yonyouProc = null;
                trayIcon.Text = "金蝶/用友焦点修复 - 未启动";
            }
        }

        static void OnWorkerExited(object sender, EventArgs e)
        {
            if (!exiting)
            {
                if (kingdeeProc?.HasExited != false && yonyouProc?.HasExited != false)
                {
                    kingdeeProc = null;
                    yonyouProc = null;
                    trayIcon.Text = "金蝶/用友焦点修复 - 已停止";
                }
            }
        }

        static void ExitApplication()
        {
            exiting = true;
            StopWorkers();
            trayIcon.Visible = false;
            Application.Exit();
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
