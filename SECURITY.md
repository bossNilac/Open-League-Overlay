# Security Policy

Open League Overlay is designed as a lightweight local companion app.

## Security Model

- No League process injection.
- No DirectX hooking.
- No League process memory reading.
- No gameplay automation.
- No game file modification.
- Live in-game data comes from the local League Live Client Data API.
- Optional champion-select and client-side features use local League client endpoints.
- User match data stays local under `%LOCALAPPDATA%\OpenLeagueOverlay\`.
- No telemetry, analytics, uploaded reports, or uploaded saved matches.
- Start with Windows is opt-in and uses only the current-user registry key under `HKCU`.
- The app does not require administrator privileges.

## Reporting Security Issues

Please report security issues privately to the repository owner instead of opening a public issue with sensitive details.

When reporting, include:

- A short description of the issue.
- Steps to reproduce.
- App version and Windows version.
- Whether you used a GitHub release zip or a source build.

Do not post Riot account details, lockfile contents, auth tokens, private match data, or unsanitized logs publicly.

## Unsigned Builds

Current public builds are unsigned. Windows Defender SmartScreen may warn until the project earns reputation or uses code signing. If you believe a GitHub release was incorrectly flagged, submit the downloaded zip or executable to Microsoft Security Intelligence for review.
