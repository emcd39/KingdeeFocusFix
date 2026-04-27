# KingdeeFocusFix

> 金蝶/用友焦点修复工具 - 解决 Windows 10/11 下 Alt+Tab 切换窗口时的焦点抢占问题

---

## 功能

| 目标程序 | 框架 | 焦点抢占方式 | 修复方案 |
|---------|------|-------------|---------|
| 金蝶云·星空财务报表 (`Kingdee.BOS.KDSReport.exe`) | WinForms | `SetForegroundWindow` | CBT 钩子 (`HCBT_ACTIVATE`) |
| 用友 (`EnterprisePortal.exe`) | VB6 (ThunderRT6FormDC) | `AttachThreadInput` + `BringWindowToTop` | MinHook API Hook |

---

## 项目结构

```
KingdeeFocusFix/
├── HookDll/                      # C++ 原生 DLL（钩子实现）
│   ├── HookDll.h
│   ├── HookDll.cpp               # 整合金蝶 CBT 钩子 + 用友 MinHook
│   ├── HookDll.vcxproj           # 支持 x64/x86 双平台编译
│   └── minhook/                  # MinHook 库（已包含）
│       ├── include/MinHook.h
│       └── lib/
│           ├── x64/libMinHook.x64.lib
│           └── x86/libMinHook.x86.lib
├── KingdeeWorker/                # 金蝶修复进程 (64位)
│   ├── Program.cs
│   └── KingdeeWorker.csproj
├── YonyouWorker/                 # 用友修复进程 (32位)
│   ├── Program.cs
│   └── YonyouWorker.csproj
├── YonyouAndKingdeeFix/          # 统一启动器（托盘应用）
│   ├── Program.cs
│   └── YonyouAndKingdeeFix.csproj
└── README.md
```

### 架构说明

```
YonyouAndKingdeeFix.exe (AnyCPU 托盘程序)
    ├─ KingdeeWorker.exe (64位) → HookDll64.dll → CBT 钩子拦截金蝶
    └─ YonyouWorker.exe (32位) → HookDll32.dll → MinHook 拦截用友
```

**为什么需要两个 Worker 进程？**
- 金蝶是 64 位程序，需要 64 位 DLL 注入
- 用友是 32 位程序，需要 32 位 DLL 注入
- Windows 全局钩子要求 DLL 架构与目标进程匹配

**为什么用友需要 MinHook？**
- 用友使用 VB6 框架，通过 `AttachThreadInput` + `BringWindowToTop` 抢占焦点
- 这不触发 `HCBT_ACTIVATE` 消息，CBT 钩子无法拦截
- MinHook 可以 Hook 这两个 API，在 Alt+Tab 时阻止调用

---

## 编译

### 环境要求

- Visual Studio 2019 或更高版本
- **C++ 桌面开发**工作负载
- .NET Framework 4.8（系统自带）

### 步骤

**第一步：编译 HookDll（C++ DLL）**

1. 用 Visual Studio 打开 `HookDll/HookDll.vcxproj`
2. 分别编译 `Release | x64` 和 `Release | Win32`
3. 得到 `HookDll/x64/Release/HookDll64.dll` 和 `HookDll/Win32/Release/HookDll32.dll`

**第二步：编译 Worker 进程**

1. 打开 `KingdeeWorker/KingdeeWorker.csproj`，生成
2. 打开 `YonyouWorker/YonyouWorker.csproj`，生成

**第三步：编译启动器**

1. 打开 `YonyouAndKingdeeFix/YonyouAndKingdeeFix.csproj`，生成

**第四步：部署**

将以下文件放在同一目录：

```
YonyouAndKingdeeFix.exe
KingdeeWorker.exe
YonyouWorker.exe
HookDll64.dll
HookDll32.dll
```

---

## 使用

1. 右键 `YonyouAndKingdeeFix.exe` → **以管理员身份运行**
2. UAC 提示点"是"
3. 系统托盘出现盾牌图标，表示修复已启用
4. 右键托盘图标可以：
   - 启用修复
   - 停止修复
   - 退出

> **注意**：程序不修改任何系统文件和注册表，关闭后完全还原。

### 开机自启

由于程序需要管理员权限，推荐用任务计划程序：

1. 打开"任务计划程序"
2. 创建任务 → 常规 → 勾选"使用最高权限运行"
3. 触发器 → 新建 → 登录时
4. 操作 → 新建 → 启动程序 → 选择 `YonyouAndKingdeeFix.exe`

---

## 技术细节

### 金蝶拦截（CBT 钩子）

```cpp
static LRESULT CALLBACK CbtProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HCBT_ACTIVATE)
    {
        HWND hwnd = (HWND)wParam;
        bool altDown = (GetAsyncKeyState(VK_LMENU) & 0x8000) ||
                       (GetAsyncKeyState(VK_RMENU) & 0x8000);
        if (altDown && IsKingdeeReport(hwnd))
            return 1;  // 阻断激活
    }
    return CallNextHookEx(g_hCbtHook, code, wParam, lParam);
}
```

### 用友拦截（MinHook API Hook）

```cpp
// Hook AttachThreadInput - 阻止用友在 Alt+Tab 时连接线程输入
static BOOL WINAPI Detour_AttachThreadInput(DWORD idAttach, DWORD idAttachTo, BOOL fAttach)
{
    if (fAttach) {
        bool altDown = (GetAsyncKeyState(VK_LMENU) & 0x8000) ||
                       (GetAsyncKeyState(VK_RMENU) & 0x8000);
        if (altDown) return FALSE;  // 阻止连接
    }
    return fpAttachThreadInput(idAttach, idAttachTo, fAttach);
}

// Hook BringWindowToTop - 阻止用友在 Alt+Tab 时抢占前台
static BOOL WINAPI Detour_BringWindowToTop(HWND hWnd)
{
    bool altDown = (GetAsyncKeyState(VK_LMENU) & 0x8000) ||
                   (GetAsyncKeyState(VK_RMENU) & 0x8000);
    if (altDown) return FALSE;  // 阻止置顶
    return fpBringWindowToTop(hWnd);
}
```

### 共享数据段

```cpp
#pragma data_seg(".SHARED")
HHOOK g_hCbtHook = NULL;
#pragma data_seg()
#pragma comment(linker, "/SECTION:.SHARED,RWS")
```

全局钩子 DLL 会被注入多个进程，`.SHARED` 段确保所有进程实例共享同一个 `g_hCbtHook` 句柄。

---

## 适用范围

| 项目 | 说明 |
|------|------|
| 目标程序 | 金蝶云·星空财务报表 `Kingdee.BOS.KDSReport.exe` (64位)<br>用友 `EnterprisePortal.exe` (32位) |
| 操作系统 | Windows 10 / Windows 11 |
| 运行时 | .NET Framework 4.8（系统自带）<br>Visual C++ 运行库 |

---

## License

MIT
