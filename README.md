# KingdeeFocusFix

> 金蝶云·星空财务报表系统（`Kingdee.BOS.KDSReport.exe`）在 Windows 10/11 下 Alt+Tab 切换窗口时，会抢占焦点导致任务切换器选中位置被重置，需要多按一次 Tab 才能切走。本工具通过 Windows CBT 全局钩子在消息级别拦截该行为，完全还原正常的 Alt+Tab 体验。

---

## 问题描述

| 环境 | 现象 |
|------|------|
| Windows 7 | 正常 |
| Windows 10 / 11 | 按下 Alt+Tab 后，任务切换界面闪一下，选中位置重置，需多按一次 Tab |

### 根本原因

金蝶报表系统基于 WinForms 开发，在窗口失去焦点时会触发内部的 `SetForegroundWindow` 调用。Windows 10/11 收紧了 DWM 合成器的焦点策略，导致该调用在 Alt 抬起后 **50ms 以内**成功抢占前台，打断了任务切换器的选中状态。

经诊断确认，`Kingdee.BOS.KDSReport.exe`（hwnd 类名：`WindowsForms10.Window.8.app.0.261f82a_r10_ad1`）在每次 Alt 抬起后立即成为前台窗口，任何基于轮询的方案（如 AutoHotkey 定时器）均因延迟过大而无法拦截。

---

## 解决方案

使用 **Windows `WH_CBT` 全局钩子**，在 `HCBT_ACTIVATE` 消息层面拦截。

```
Alt 抬起
  └→ Windows 发出 HCBT_ACTIVATE 消息（窗口尚未激活）
       └→ CbtProc 检测：Alt 按下 + 目标进程是 KDSReport？
            ├→ 是：返回非零，阻断激活，任务切换器不受干扰
            └→ 否：CallNextHookEx 放行
```

`WH_CBT` 钩子工作在消息队列层，比窗口真正获得焦点还早一步，彻底解决了轮询方案追不上的问题。

---

## 项目结构

```
KingdeeFocusFix/
├── HookDll/                  # C++ 原生 DLL（钩子实现）
│   ├── HookDll.h
│   ├── HookDll.cpp
│   └── HookDll.vcxproj
├── FixApp/                   # C# 托盘程序（加载 DLL）
│   ├── Program.cs
│   ├── app.manifest
│   └── FixApp.csproj
└── README.md
```

### 为什么拆成 DLL + EXE？

Windows 要求全局钩子（`threadId = 0`）的回调函数必须位于一个**独立的非托管 DLL** 中，以便系统将其注入其他进程的线程。.NET 托管程序集无法满足此条件，直接在 EXE 中调用 `SetWindowsHookEx` 会返回失败。

---

## 编译

### 环境要求

- Visual Studio 2019 或更高版本
- **C++ 桌面开发**工作负载（用于编译 HookDll）
- .NET Framework 4.8（系统自带，无需额外安装）

### 步骤

**第一步：编译 HookDll（C++ DLL）**

1. 用 Visual Studio 打开 `HookDll/HookDll.vcxproj`
2. 配置选择 `Release | x64`
3. 生成 → 生成 HookDll
4. 得到 `HookDll/x64/Release/HookDll.dll`

**第二步：编译 FixApp（C# EXE）**

1. 打开 `FixApp/FixApp.csproj`
2. 生成 → 生成 FixApp
3. 得到 `FixApp/bin/Release/KingdeeFocusFix.exe`

**第三步：部署**

将以下两个文件放在同一目录：

```
KingdeeFocusFix.exe
HookDll.dll
```

---

## 使用

1. 右键 `KingdeeFocusFix.exe` → **以管理员身份运行**（全局钩子需要管理员权限）
2. UAC 提示点"是"
3. 系统托盘出现盾牌图标，表示钩子已安装，程序运行中
4. 正常使用金蝶报表，此时 Alt+Tab 应恢复正常行为
5. 退出：右键托盘图标 → 退出

> **注意**：程序不修改任何系统文件和注册表，关闭后完全还原。

### 开机自启

由于程序需要管理员权限，普通启动项无法自动提权，推荐用任务计划程序：

1. 打开"任务计划程序"
2. 创建任务 → 常规 → 勾选"使用最高权限运行"
3. 触发器 → 新建 → 登录时
4. 操作 → 新建 → 启动程序 → 选择 `KingdeeFocusFix.exe`

---

## 技术细节

### CBT 钩子拦截逻辑

```cpp
static LRESULT CALLBACK CbtProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HCBT_ACTIVATE)
    {
        HWND hwnd = (HWND)wParam;
        bool altDown = ((GetAsyncKeyState(VK_LMENU) & 0x8000) != 0)
                    || ((GetAsyncKeyState(VK_RMENU) & 0x8000) != 0);

        if (altDown && IsKDSReport(hwnd))
            return 1;  // 阻断激活
    }
    return CallNextHookEx(g_hook, code, wParam, lParam);
}
```

- `HCBT_ACTIVATE`：窗口**即将**被激活时触发，此时激活尚未发生，返回非零可阻断
- `GetAsyncKeyState`：检测 Alt 键实时状态，确保只在 Alt+Tab 场景下介入，不影响正常点击金蝶窗口
- `IsKDSReport`：通过进程快照（`CreateToolhelp32Snapshot`）核验目标进程名，精确匹配

### 共享数据段

```cpp
#pragma data_seg(".SHARED")
HHOOK g_hook = NULL;
#pragma data_seg()
#pragma comment(linker, "/SECTION:.SHARED,RWS")
```

全局钩子 DLL 会被注入多个进程，`.SHARED` 段确保所有进程实例共享同一个 `g_hook` 句柄。

---

## 适用范围

| 项目 | 说明 |
|------|------|
| 目标程序 | 金蝶云·星空财务报表系统 `Kingdee.BOS.KDSReport.exe` |
| 操作系统 | Windows 10 / Windows 11 |
| 架构 | x64 |
| 运行时 | .NET Framework 4.8（系统自带） |

---

## License

MIT
