# Open League Overlay

A lightweight Windows companion app for League of Legends.

Open League Overlay provides live in-game information, a compact transparent overlay, a TUI scoreboard, a GUI dashboard, local match history, and post-game reports.

## Features

- Live enhanced scoreboard
- Transparent in-game overlay
- TUI scoreboard mode
- GUI dashboard and local match history
- Inventory-value gold estimates
- Player vs lane opponent comparison
- Team summary stats
- Objective and event tracking
- Local post-game reports
- OL Score performance estimate and grade
- Recommended builds, runes, summoner spells, and skill order when available
- Optional rune and item-set export when supported by the local League client
- Optional Start with Windows setting

## How It Works

The app reads live in-game data from the local League Live Client Data API:

`https://127.0.0.1:2999/liveclientdata/allgamedata`

The app only works while League is running and live data is available in an active match. Champ-select features use the local League client lockfile and local client endpoints.

The app:

- Does not inject into League
- Does not hook DirectX
- Does not read game process memory
- Does not automate gameplay
- Does not modify game files
- Does not require a Riot API key for live in-game data
- Does not require a Riot account password

## Gold Values

Enemy unspent gold is not exposed by the Live Client Data API. Enemy gold values shown by this app are visible inventory-value estimates based on items. Treat labels such as `Inventory Gold`, `Visible Item Value`, or `Estimated Inventory Value` as item-value estimates, not exact total gold.

## Local Data and Privacy

Saved game data stays on your machine.

Local data is stored under:

`%LOCALAPPDATA%\OpenLeagueOverlay\`

This includes:

- `settings.json`
- `match_history\`
- `reports\`
- `snapshots\`
- `logs\`
- `cache\`

The app does not upload saved matches, reports, snapshots, event logs, or player data. It does not include telemetry or analytics. GitHub release packages do not include user match history or local settings.

To delete local history and settings, close the app and delete:

`%LOCALAPPDATA%\OpenLeagueOverlay\`

## Installation

1. Download the latest Windows x64 release zip from GitHub Releases.
2. Extract the zip.
3. Run `OpenLeagueOverlay.exe`.
4. Start a League match.
5. The overlay and scoreboard activate automatically when local live data is available.

The app remembers your last selected UI mode:

- First run opens the GUI dashboard.
- Switching to TUI makes the next launch open TUI.
- Switching back to GUI makes the next launch open GUI.

Advanced command-line options:

```powershell
OpenLeagueOverlay.exe --gui
OpenLeagueOverlay.exe --tui
OpenLeagueOverlay.exe --font-height 10
```
## First Run and Start With Windows

Start with Windows is disabled by default.

On first GUI dashboard launch, the app asks whether it should start automatically when Windows starts. You can choose Enable or Not now. The setting can also be toggled later from the GUI dashboard status card.

Startup uses the per-user registry key:

`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`

Value name:

`OpenLeagueOverlay`

This only launches the local app. It does not upload data or run with administrator privileges.

## Build From Source

Requirements used by this project:

- Windows
- CMake
- Ninja
- MinGW toolchain
- vcpkg dependencies for `curl` and `jsoncpp`

Example build commands from this repository:

```powershell
scripts\build_release.cmd
```

Package a release:

```bat
scripts\package_release.cmd
```

Release output:

`dist\OpenLeagueOverlay-v1.0.0-windows-x64.zip`

If you prefer PowerShell directly, use:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build_release.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\package_release.ps1
```

The `.cmd` wrappers are included so downloaded source archives can build without changing the system-wide PowerShell execution policy.

## Troubleshooting

No data shown:

You must be inside an active League match for the Live Client Data API to expose game data.

Overlay not visible:

Check that League client/game is focused and that the overlay mode is enabled.

HTTPS or certificate issue:

The app handles the local self-signed Live Client Data API certificate internally.

Missing history:

Match history only exists for games played while the app was running.

Gold values look different from the scoreboard:

Enemy gold is an inventory-value estimate, not exact total gold.

Start with Windows not working:

Check the GUI dashboard setting and Windows startup permissions for the current user.

## Security Notes

- No Riot account password is required.
- No Riot API key is required for live in-game data.
- Saved match data stays local.
- No process injection is used.
- No memory reading is used.
- No gameplay automation is used.
- Start with Windows is optional and can be disabled anytime.
