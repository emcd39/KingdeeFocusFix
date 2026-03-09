== 编译步骤（.NET Framework 版，单文件，无需安装运行时）==

1. 用 Visual Studio 打开，确保安装了：
   - "使用 C++ 的桌面开发"工作负载
   - ".NET 桌面开发"工作负载

2. 【先】编译 HookDll（Release | x64）
   右键 HookDll 项目 → 生成
   → 产出：HookDll\x64\Release\HookDll.dll

3. 【再】编译 FixApp（Release）
   右键 FixApp 项目 → 生成
   → 产出：FixApp\bin\Release\KingdeeFocusFix.exe

4. 直接运行 KingdeeFocusFix.exe，无需安装任何运行时或依赖。

== 为什么用 .NET Framework 4.6.2 ==

Windows 10/11 系统自带 .NET Framework 4.x，
编译出来的 exe 只有几百 KB，用户无需额外安装任何东西。
