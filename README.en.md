[中文](README.md) / English

# nms-ime-proxy

No Man's Sky calls Steam's Big Picture gamepad text-input API when naming discoveries, ships,
freighters and more. This proxy intercepts that call and opens its own text panel at input time.
It fixes both CJK input in the game and the case where the Steam Big Picture keyboard fails to
display properly over the game. The game's `steam_api64.dll` is replaced by a proxy that forwards
every other Steam call unchanged to the original DLL, and the panel supports the full Windows IME
— no need to actually enter Big Picture.

## Features

- Local WinForms text panel, centered over the game, topmost, full IME support
- Works with a normal launch and with a Big Picture launch
- Works in borderless and fullscreen (borderless recommended)
- Enter submits, Esc cancels
- UTF-8 text is returned through the real Steam callback the game expects (id 714)
- While the panel is open the game ignores mouse clicks, so it cannot steal focus or react
- After closing, focus returns to the game automatically; keyboard input works immediately
- Optional debug logging (off by default)

## Requirements

- Windows 10 or 11, x64
- Steam version of No Man's Sky
- .NET Framework 4.x (included in Windows)
- PowerShell 5.1 for the installer

## Install

Quit No Man's Sky first. The installer refuses to run while `NMS.exe` is active.

### Option 1 — release package

1. Download and extract the release zip.
2. Run the installer from the extracted folder:

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\install.ps1
   ```

   The script finds the game through the Steam registry and `libraryfolders.vdf`. If detection
   fails, pass the folder explicitly:

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\install.ps1 -Binaries "D:\SteamLibrary\steamapps\common\No Man's Sky\Binaries"
   ```

3. Launch the game. Windows SmartScreen may warn about the unsigned panel executable — this is
   expected for a community build; choose "More info" → "Run anyway".

### Option 2 — manual install

1. Quit the game and open `No Man's Sky\Binaries`.
2. Rename `steam_api64.dll` to `steam_api64_orig.dll` (keep it — the proxy needs it).
3. Copy `steam_api64.dll`, `nms-ime-panel.exe` and `nms-ime-proxy.ini` from the release into the
   same folder.
4. Launch the game.

To roll back manually, delete the proxy `steam_api64.dll`, rename `steam_api64_orig.dll` back to
`steam_api64.dll`, and remove the panel and ini files.

## Usage

- A normal launch and a Big Picture launch both work. A borderless window is recommended; in
  fullscreen the game briefly drops to the desktop while input is open, then returns.
- Trigger any input that normally opens the Steam keyboard, e.g. renaming a ship or a discovery.
  The panel appears over the game:
  - type with your IME (candidates, composition — all normal)
  - **Enter** submits, **Esc** cancels
  - clicking outside the panel while it is open does nothing to the game
- In borderless mode, after the panel closes, **one manual left-click** is needed to recapture the
  mouse for camera control. In fullscreen mode the mouse is recaptured automatically.

## What gets modified

Exactly **one game file is replaced**, the original is kept as a backup, and two files are added
inside `No Man's Sky\Binaries`:

| File | Role |
|---|---|
| `steam_api64.dll` | Replaced by this proxy |
| `steam_api64_orig.dll` | The original Valve DLL, renamed by the installer; the proxy forwards all calls to it |
| `nms-ime-panel.exe` | The text panel process |
| `nms-ime-proxy.ini` | Optional configuration (debug logging) |

No save files, game settings, registry entries, or other game files are touched. To locate the
game the installer only reads the Steam registry and `libraryfolders.vdf`; it never writes to the
registry.

## Compatibility

- **Steam features (achievements, playtime, cloud saves, overlay, friends):** unaffected. Every
  Steam API call except the SteamUtils text-input methods is forwarded unchanged to the original
  Valve DLL; the game still talks to the real Steam client.
- **Other mods:** mods that do not touch `steam_api64.dll` (the usual NMS mods, ASI loaders,
  ReShade/OptiScaler etc.) coexist normally. Only one file can occupy the `steam_api64.dll` name,
  so tools that also replace or hook it (Steam emulators, achievement unlockers) conflict — keep
  one of them.
- **Game updates / "Verify integrity of game files":** Steam restores the official DLL. Re-run
  `install.ps1` afterwards. A stale `steam_api64_orig.dll` is harmless; the installer refreshes
  the backup when the live DLL is Valve-signed.

## Configuration (optional)

`nms-ime-proxy.ini`, next to the installed DLL:

```ini
[nms-ime-proxy]
Debug=0
```

Set `Debug=1` to write diagnostic logs to `%LOCALAPPDATA%\nms-ime-proxy\` (`proxy.log`,
`panel.log`). Logging is disabled by default; no log files are created then.

## Uninstall

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1 -Uninstall
```

This restores the original `steam_api64.dll` and removes the panel and config. Alternatively use
Steam → right-click the game → Properties → Installed Files → **Verify integrity of game files**.

## Build from source

Only needed if you modify the code; end users should use the release zip. Both toolchains below
use the .NET Framework compiler that ships with Windows for the panel.

### Option 1 — MSVC

Install Visual Studio 2022 (Community or Build Tools) with the "Desktop development with C++"
workload, then run in the repository root:

```bat
build-msvc.bat
```

The script locates `vswhere`/`vcvars64.bat`, compiles with static CRT (`/MT`), and stages `dist/`.

### Option 2 — MSYS2 UCRT64

Install MSYS2 with the `mingw-w64-ucrt-x86_64-gcc` package, open the UCRT64 shell and run:

```bash
bash build.sh
```

`build.sh` prefers `g++`/`objdump` from `PATH` and falls back to `C:\msys64\ucrt64\bin`.

Both scripts produce a complete distribution in `dist/` (binaries, config, installer, READMEs),
ready to zip or install with `dist\install.ps1`.

## Known limitations

- Some rename screens (multi-tool, freighter, suit) do not prefill the current name. This is
  game-side behaviour, not a proxy issue.
- Borderless mode needs one manual left-click after closing the panel (see Usage).
- Submit latency: handing the text back takes ~10–20 ms; the remaining pause happens inside the
  game's own UI.
- Steam version of the game, Windows only.

## How it works

`steam_api64_proxy.cpp` exports the 11 functions NMS imports and forwards each to
`steam_api64_orig.dll`. When the game creates the `SteamUtils010` interface, the proxy swaps its
vtable for a clone in which three methods are replaced:

| Vtable offset | Method | Behaviour |
|---|---|---|
| `0xA0` | `ShowGamepadTextInput` | opens the panel, returns `true` |
| `0xA8` | `GetEnteredGamepadTextLength` | returns the UTF-8 byte length |
| `0xB0` | `GetEnteredGamepadTextInput` | copies the UTF-8 text |

When the panel finishes (Enter/Esc/close), the proxy synthesizes
`GamepadTextInputDismissed_t` (callback id 714) on the next `SteamAPI_RunCallbacks`. The game then
pulls the text through the two getters above. While the panel is open, the proxy also subclasses
the game's GLFW window procedure to swallow mouse activation and button messages.

The panel is a small WinForms app. The proxy polls an atomically written response file, so the
text is handed back without waiting for the panel process to exit.

## Repository layout

```
.
├─ steam_api64_proxy.cpp   C++ proxy DLL (all Steam exports + hooks)
├─ panel/panel.cs          WinForms text panel
├─ build.sh                build + stage dist/ (MSYS2 / Git Bash)
├─ build-msvc.bat          build + stage dist/ (Visual Studio, no MSYS)
├─ install.ps1             install / -Uninstall
├─ nms-ime-proxy.ini       runtime config (Debug)
├─ README.md               Chinese README (default)
├─ README.en.md            this file
└─ dist/                   generated build output (ready to distribute)
```
