# DLSS Indicator Tray

NVIDIA DLSS has a hidden debug overlay that, when enabled, shows the active DLSS DLL version, render resolution, preset, DLSS-G frame generation ratio, Reflex status, and more in-game. This tool controls it from the Windows notification area by setting one registry DWORD: `HKLM\SOFTWARE\NVIDIA Corporation\Global\NGXCore\ShowDlssIndicator`.

| Value | Effect |
|---|---|
| `0` / missing | Off |
| `1` | Blue `DEV` letters (dev DLLs only) |
| `1024` | Full text overlay (SR/RR bottom-left + DLSS-G top) |

The tray icon shows the current value: red `OFF`  for `0`, blue `DEV`  for `1`, or green `ON` for `1024`. Left-click toggles immediately: `0` switches to `1024`, while any other value (including `1`) switches to `0`. Right-click opens the full menu, including `DEV` (`1`), refresh, Registry Editor, options to enable or disable startup with Windows, and exit.

## Usage

Download `DlssIndicatorTray.exe` from [Releases](../../releases) and run it normally. The app requests administrator approval only when you choose a registry value. Restart your game for the change to take effect.

## Build

Visual Studio 2022 with the C++ workload. Run `_build_msvc.bat`.

## Acknowledgments

This project was inspired by [DLSS Indicator Toggle](https://github.com/djkcyl/dlss-indicator-toggle) and [HDRTray](https://github.com/res2k/HDRTray). Development was assisted by GitHub Copilot.

## License

MIT
