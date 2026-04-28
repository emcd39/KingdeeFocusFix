# KingdeeFocusFix

> Windows 系统级窗口切换修复工具，解决金蝶云·星空和用友 ERP 的 Alt+Tab 异常问题。

---

## 功能

| 功能 | 说明 |
|------|------|
| 金蝶焦点修复 | 拦截金蝶报表系统抢占焦点行为，还原正常 Alt+Tab 体验 |
| 用友窗口跳过 | 在用友界面按 Alt+Tab 时自动跳过一个窗口（等效按两次 Tab） |

---

## 问题描述

### 金蝶报表焦点抢占

| 环境 | 现象 |
|------|------|
| Windows 7 | 正常 |
| Windows 10 / 11 | 按下 Alt+Tab 后，任务切换界面闪一下，选中位置重置，需多按一次 Tab |

**根本原因**：金蝶报表系统在窗口失去焦点时会调用 `SetForegroundWindow`，在 Alt 抬起后 50ms 内抢占前台。

### 用友 ERP 窗口切换

用友 ERP（`EnterprisePortal.exe`）的子窗口在 Alt+Tab 时行为异常，需要按两次 Tab 才能切换到下一个窗口。本工具自动发送额外的 Tab 按键，实现一次 Alt+Tab 跳过用友窗口。

---

## 解决方案

### 金蝶焦点修复：WH_CBT 全局钩子

```
Alt 抬起
  └→ Windows 发出 HCBT_ACTIVATE 消息（窗口尚未激活）
       └→ CbtProc 检测：Alt 按下 + 目标进程是 KDSReport？
            ├→ 是：返回非零，阻断激活，任务切换器不受干扰
            └→ 否：CallNextHookEx 放行
```

### 用友窗口跳过：WH_KEYBOARD_LL 低级键盘钩子

```
Alt+Tab 按下
  └→ KeyboardProc 检测：前台窗口是用友？
       ├→ 是：发送额外 Tab 键（实现跳过效果）
       └→ 否：正常放行
```

通过 `LLKHF_INJECTED` 标志区分真实键盘事件和模拟事件，避免递归触发。

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

### CBT 钩子拦截逻辑（金蝶修复）

```cpp
static LRESULT CALLBACK CbtProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HCBT_ACTIVATE)
    {
        HWND hwnd = (HWND)wParam;
        bool altDown = ((GetAsyncKeyState(VK_LMENU) & 0x8000) != 0)
                    || ((GetAsyncKeyState(VK_RMENU) & 0x8000) != 0);

        if (altDown && IsKDSReport(hwnd))
            return 1;
    }
    return CallNextHookEx(g_hook, code, wParam, lParam);
}
```

### 键盘钩子逻辑（用友跳过）

```cpp
static LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION)
    {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        bool isInjected = (kb->flags & LLKHF_INJECTED) != 0;

        if (!isInjected)
        {
            // 追踪真实 Alt 键状态
            if (kb->vkCode == VK_LMENU || kb->vkCode == VK_RMENU)
            {
                if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
                    g_realAltDown = true;
                else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
                    g_realAltDown = false;
            }

            // 检测用友窗口的 Alt+Tab
            if (kb->vkCode == VK_TAB && g_realAltDown)
            {
                HWND fgWnd = GetForegroundWindow();
                if (IsYonyouWindow(fgWnd))
                {
                    INPUT inputs[1] = {};
                    inputs[0].type = INPUT_KEYBOARD;
                    inputs[0].ki.wVk = VK_TAB;
                    SendInput(1, inputs, sizeof(INPUT));
                }
            }
        }
    }
    return CallNextHookEx(g_kbHook, code, wParam, lParam);
}
```

- `LLKHF_INJECTED`：区分真实键盘事件和模拟事件，避免递归触发
- `g_realAltDown`：只在真实事件时更新，不受模拟事件干扰

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
| 目标程序 | 金蝶云·星空 `Kingdee.BOS.KDSReport.exe`、用友 ERP `EnterprisePortal.exe` |
| 操作系统 | Windows 10 / Windows 11 |
| 架构 | x64 |
| 运行时 | .NET Framework 4.8（系统自带） |

---

## License

MIT
