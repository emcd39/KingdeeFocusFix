# KingdeeFocusFix

修复金蝶云星空财务报表系统（`Kingdee.BOS.KDSReport.exe`）在 Windows 10/11 下 Alt+Tab 切换窗口时焦点异常的问题。

## 问题描述

在 Windows 10/11 环境下，当 `Kingdee.BOS.KDSReport.exe`（金蝶云星空财务报表系统）处于前台时，按下 Alt+Tab 切换窗口会出现以下异常：

- Alt+Tab 任务切换界面**闪烁一下**
- 切换器中选中的窗口**重置回第一个**
- 需要**额外多按一次 Tab** 才能切换到目标窗口

该问题在 Windows 7 下不存在，Windows 10/11 均有此现象。

## 根本原因

通过毫秒级诊断日志确认，`Kingdee.BOS.KDSReport.exe` 会在 **Alt 键抬起后 50ms 内**调用 `SetForegroundWindow()` 强行抢占焦点，导致 Windows 任务切换器重置当前选中位置。

```
用户按下 Alt+Tab
→ 任务切换器弹出（正常）
→ 用户松开 Alt
→ KDSReport 在 <50ms 内抢占焦点        ← 问题根源
→ 任务切换器重置选中位置
→ 需要多按一次 Tab
```

由于抢占发生在 50ms 以内，任何基于定时器的方案（如 AutoHotkey）均无法有效拦截。

## 解决方案

使用 **Win32 `WH_CBT` 全局钩子**，在 `HCBT_ACTIVATE` 消息层面拦截。该消息在窗口**即将**被激活时触发，早于焦点实际转移，可以通过返回非零值直接阻断。

由于 Windows 要求全局钩子的回调函数必须位于独立的非托管 DLL 中，项目拆分为两个部分：

- **HookDll**：C++ 编写的动态链接库，包含 CBT 钩子的实际实现
- **FixApp**：C# 编写的托盘程序，负责加载 DLL、安装/卸载钩子

### 拦截逻辑

```
HCBT_ACTIVATE 触发
→ 检查 Alt 键是否按下
→ 检查即将激活的窗口是否属于 KDSReport 进程
→ 若两者均为真 → 返回 1，阻断激活
→ 否则 → 正常放行
```

## 项目结构

```
KingdeeFocusFix/
├── HookDll/
│   ├── HookDll.h          # 导出函数声明
│   ├── HookDll.cpp        # CBT 钩子实现（C++）
│   └── HookDll.vcxproj    # Visual Studio C++ 项目
└── FixApp/
    ├── Program.cs          # 托盘程序主体（C#）
    ├── app.manifest        # 请求管理员权限
    └── FixApp.csproj       # .NET 6 项目文件
```

## 编译

### 环境要求

- Windows 10/11 x64
- Visual Studio 2019 或更高版本
- 工作负载：**使用 C++ 的桌面开发** + **.NET 桌面开发**
- .NET 6 SDK

### 步骤

1. 用 Visual Studio 打开解决方案

2. 编译 `HookDll`（**必须先编译**）：
   ```
   配置：Release | x64
   ```

3. 编译 `FixApp`：
   ```
   配置：Release | x64
   ```

4. 将编译产物放在同一目录：
   ```
   KingdeeFocusFix.exe
   HookDll.dll
   ```

> HookDll.dll 和 KingdeeFocusFix.exe 必须位数一致（均为 x64）

## 使用

1. 以**管理员身份**运行 `KingdeeFocusFix.exe`（程序会自动弹出 UAC 提示）
2. 系统托盘出现盾牌图标，表示钩子已安装并生效
3. 正常使用金蝶报表，Alt+Tab 行为恢复正常
4. 右键托盘图标 → **退出** 可卸载钩子并退出程序

### 开机自启

由于程序需要管理员权限，建议通过**任务计划程序**设置开机自启：

1. 打开任务计划程序 → 创建任务
2. 常规选项卡：勾选"使用最高权限运行"
3. 触发器：选择"登录时"
4. 操作：启动程序，指向 `KingdeeFocusFix.exe`

## 注意事项

- 本工具不修改任何系统文件和注册表，关闭即完全还原
- 全局 CBT 钩子会注入所有进程，但拦截逻辑仅对 `Kingdee.BOS.KDSReport.exe` 生效，不影响其他程序
- 如果金蝶报表进程名与预期不符，可在 `HookDll.cpp` 中修改 `TARGET_PROC` 常量后重新编译

## 已测试环境

| 操作系统 | 金蝶版本 | 结果 |
|----------|----------|------|
| Windows 11 | 金蝶云星空 | ✅ 正常 |
| Windows 10 | 金蝶云星空 | 待测试 |

## License

MIT
