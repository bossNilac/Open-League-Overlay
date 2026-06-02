# Contributing

Thanks for helping improve Open League Overlay.

## Build Locally

Requirements:

- Windows
- CMake
- Ninja
- MinGW toolchain
- vcpkg dependencies for `curl` and `jsoncpp`

Build:

```powershell
scripts\build_release.cmd
```

Package a release:

```powershell
scripts\package_release.cmd
```

## Running

Use the release folder or build output:

```powershell
OpenLeagueOverlay.exe --gui
OpenLeagueOverlay.exe --tui
```

The app shows live match data only when League exposes local live data.

## Code Expectations

- Keep the app lightweight.
- Prefer small, focused changes.
- Keep parsing/data logic separate from rendering where practical.
- Do not commit generated build folders or local user data.
- Do not add telemetry or analytics.
- Keep user data local by default.

Do not add:

- League process memory reading.
- Process injection.
- DirectX hooks.
- Gameplay automation.
- Silent startup registration.
- Download-and-execute updaters.

## Pull Requests

Good pull requests include:

- A clear description of the change.
- Screenshots for UI changes, with private data removed.
- Build or test notes.
- Any privacy/security impact.

## Issues

When reporting issues, include the app version, Windows version, whether you used a release zip or source build, and which mode was active: GUI, TUI, overlay, champ select, or reports.

Remove private player names, Riot account details, lockfile contents, auth tokens, and personal match data before posting logs or screenshots.
