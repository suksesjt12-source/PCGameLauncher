# PCGameLauncher

Lightweight native Win32 launcher for Windows games.

## Features
- Browse and remember a game executable.
- Optional command-line arguments.
- Write DXVK frame-rate settings to `dxvk.conf`.
- Preserve unrelated existing DXVK configuration lines.
- Optional `dxgi.syncInterval = 0` setting.
- GitHub Actions builds Windows x64 and x86 executables.

## Build
Open the Actions tab and run **Build PC Game Launcher**, or push a commit to `main`. Download the `PCGameLauncher-Windows` artifact from the completed run.

## Use
1. Run the launcher on Windows.
2. Select the game's `.exe`.
3. Optionally enter launch arguments.
4. Set the DXVK FPS limit (`0` means unlimited).
5. Optionally check the VSync setting.
6. Select **Launch Game**.

The launcher writes `dxvk.conf` beside the selected executable. These settings apply only when the game actually uses DXVK, not WineD3D or another graphics translation layer.

This is a Windows desktop launcher, not an Android APK or a native Android port of PC games.