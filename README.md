中文 / [English](README.en.md)

# nms-ime-proxy

《无人深空》在命名发现物、飞船、货船等场景会调用 Steam 大屏幕模式的手柄文本输入接口。本代理
拦截这一调用，在输入时弹出自己的输入面板：既解决游戏内无法输入中文的问题，也解决 Steam 大屏幕
模式下输入窗口仍无法正常显示的问题。实现上是用代理替换游戏的 `steam_api64.dll`，其余 Steam
调用全部原样转发给原版 DLL；面板完整支持 Windows 输入法，不需要真正进入大屏幕模式。

## 特性

- 本地 WinForms 面板，居中显示在游戏上方，置顶，完整输入法支持
- 正常启动与大屏幕模式启动均可使用
- 无边框、全屏模式均可输入（建议无边框）
- Enter 提交，Esc 取消
- 通过游戏期望的真实 Steam 回调（id 714）回传 UTF-8 文本
- 面板打开期间游戏忽略鼠标点击，不会抢焦点、不会触发游戏内操作
- 关闭面板后自动把焦点还给游戏，键盘立即可用
- 可选调试日志（默认关闭）

## 环境要求

- Windows 10 / 11，x64
- Steam 版《无人深空》
- .NET Framework 4.x（Windows 自带）
- 安装脚本需要 PowerShell 5.1

## 安装

请先完全退出游戏；`NMS.exe` 运行中时安装脚本会拒绝执行。

### 方式一 — release 包自动安装

1. 下载并解压 release zip。
2. 在解压目录运行：

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\install.ps1
   ```

   脚本会通过 Steam 注册表和 `libraryfolders.vdf` 自动定位游戏。若定位失败，手动指定目录：

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\install.ps1 -Binaries "D:\SteamLibrary\steamapps\common\No Man's Sky\Binaries"
   ```

3. 启动游戏。Windows SmartScreen 可能提示面板程序未签名——社区构建属正常现象，选择"更多信息"→"仍要运行"。

### 方式二 — 手动安装

1. 退出游戏，打开 `No Man's Sky\Binaries`。
2. 把 `steam_api64.dll` 重命名为 `steam_api64_orig.dll`（保留原件，代理需要它）。
3. 把 release 中的 `steam_api64.dll`、`nms-ime-panel.exe` 和 `nms-ime-proxy.ini` 复制到同一目录。
4. 启动游戏。

手动回滚：删除代理的 `steam_api64.dll`，把 `steam_api64_orig.dll` 改回原名，再删除面板和 ini。

## 使用

- 正常启动或大屏幕模式启动均可。建议游戏内使用**无边框窗口**；全屏模式在输入时会短暂返回桌面，
  输入结束后自动回到游戏。
- 触发任何原本会打开 Steam 键盘的输入框（如重命名飞船、命名发现物），面板会显示在游戏上方：
  - 用输入法正常打字（候选、组词均正常）
  - **Enter** 提交，**Esc** 取消
  - 面板打开期间点击面板外区域对游戏没有任何影响
- 无边框模式下，面板关闭后**鼠标视角需手动左键点一次游戏**才会被重新捕获；**全屏模式下会自动捕获**。

## 修改范围

只**替换一个游戏文件**，保留一个原件备份，并在 `No Man's Sky\Binaries` 内新增两个文件：

| 文件 | 作用 |
|---|---|
| `steam_api64.dll` | 被代理替换 |
| `steam_api64_orig.dll` | 原版 Valve DLL，由安装脚本改名保留；代理把所有调用转发给它 |
| `nms-ime-panel.exe` | 文本输入面板 |
| `nms-ime-proxy.ini` | 可选配置（调试日志） |

不会改动任何存档、游戏设置、注册表或其他游戏文件；安装脚本仅为定位游戏读取 Steam 注册表和
`libraryfolders.vdf`，不会写入注册表。

## 兼容性

- **Steam 功能（成就、时长、云存档、叠加层、好友）：** 不受影响。除 SteamUtils 的文本输入方法外，
  所有 Steam API 调用都原样转发给原版 DLL，游戏仍与真正的 Steam 客户端通信。
- **其他模组：** 不碰 `steam_api64.dll` 的模组（常见的 NMS mod、ASI 加载器、ReShade/OptiScaler 等）
  可正常共存。`steam_api64.dll` 这个文件名同时只能有一个占用者，因此同样替换/挂钩它的工具
  （Steam 模拟器、成就解锁器）会冲突——二者只能留一个。
- **游戏更新 / "验证游戏文件完整性"：** Steam 会还原官方 DLL，重新运行 `install.ps1` 即可。
  残留的 `steam_api64_orig.dll` 无害；当现有 DLL 是 Valve 签名版时，安装脚本会自动刷新备份。

## 配置（可选）

`nms-ime-proxy.ini`（与安装后的 DLL 同目录）：

```ini
[nms-ime-proxy]
Debug=0
```

设为 `Debug=1` 后，诊断日志写入 `%LOCALAPPDATA%\nms-ime-proxy\`（`proxy.log`、`panel.log`）。
默认关闭，不产生任何日志文件。

## 卸载

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1 -Uninstall
```

会还原原版 `steam_api64.dll` 并删除面板与配置文件。也可以使用 Steam → 右键游戏 → 属性 →
已安装文件 → **验证游戏文件完整性**。

## 从源码构建

只有修改代码时才需要；最终用户请直接使用 release 包。以下两种工具链都用 Windows 自带的
.NET Framework 编译器编译面板。

### 方式一 — MSVC

安装 Visual Studio 2022（Community 或 Build Tools），勾选"使用 C++ 的桌面开发"工作负载，
然后在仓库根目录运行：

```bat
build-msvc.bat
```

脚本会自动定位 `vswhere`/`vcvars64.bat`，以静态 CRT（`/MT`）编译并准备 `dist/`。

### 方式二 — MSYS2 UCRT64

安装 MSYS2 及 `mingw-w64-ucrt-x86_64-gcc` 包，在 UCRT64 终端运行：

```bash
bash build.sh
```

`build.sh` 优先使用 `PATH` 中的 `g++`/`objdump`，找不到时回退到 `C:\msys64\ucrt64\bin`。

两种方式都会把二进制、配置、安装脚本和 README 准备到 `dist/`，可直接分发，或运行
`dist\install.ps1` 安装。

## 已知限制

- 多用途工具、货船、服装等部分重命名界面不会预填当前名字，这是游戏侧行为，非代理问题。
- 无边框模式下关闭面板后需手动点一次左键重新捕获鼠标（见"使用"）。
- 提交延迟：回传本身约 10–20 ms；剩余停顿发生在游戏自己的 UI 内。
- 仅支持 Steam 版游戏，仅 Windows。

## 工作原理

`steam_api64_proxy.cpp` 导出 NMS 导入的全部 11 个函数，逐个转发到 `steam_api64_orig.dll`。
游戏创建 `SteamUtils010` 接口时，代理把它的虚表替换为副本，并替换其中三个方法：

| 虚表偏移 | 方法 | 行为 |
|---|---|---|
| `0xA0` | `ShowGamepadTextInput` | 打开面板，返回 `true` |
| `0xA8` | `GetEnteredGamepadTextLength` | 返回 UTF-8 字节长度 |
| `0xB0` | `GetEnteredGamepadTextInput` | 拷出 UTF-8 文本 |

面板结束（Enter / Esc / 关闭）后，代理在下一次 `SteamAPI_RunCallbacks` 中合成
`GamepadTextInputDismissed_t`（回调 id 714），游戏随即通过上面两个 getter 取回文本。
面板打开期间，代理还会子类化游戏的 GLFW 窗口过程，吞掉鼠标激活与按键消息。

面板是一个小型 WinForms 程序。代理轮询其原子写出的响应文件，因此无需等待面板进程退出即可回传文本。

## 仓库结构

```
.
├─ steam_api64_proxy.cpp   C++ 代理 DLL（全部导出 + 钩子）
├─ panel/panel.cs          WinForms 文本面板
├─ build.sh                构建并准备 dist/（MSYS2 / Git Bash）
├─ build-msvc.bat          构建并准备 dist/（Visual Studio，无需 MSYS）
├─ install.ps1             安装 / -Uninstall
├─ nms-ime-proxy.ini       运行配置（Debug）
├─ README.md               本文件（中文说明）
├─ README.en.md            英文说明
├─ LICENSE                 MIT 许可证
└─ dist/                   生成的构建产物（可直接分发）
```

## 许可证

MIT，见 [LICENSE](LICENSE)。
