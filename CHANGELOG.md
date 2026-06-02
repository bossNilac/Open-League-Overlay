# Changelog

## v1.0.0

Initial public release.

Added:

- Live enhanced scoreboard
- GUI dashboard
- Transparent overlay modes
- TUI scoreboard mode
- Inventory-value gold estimates
- Team and player stat summaries
- Player vs lane opponent comparison
- Objective and event tracking
- Local match saving
- Match history and post-game reports
- OL Score performance estimate and grade
- Build, rune, summoner spell, and skill recommendations when available
- Optional rune and item-set export when supported by the local League client
- Optional Start with Windows support
- Local-only storage for user match data

Release hygiene rebuild:

- Added Windows executable version metadata.
- Added normal-user Windows application manifests.
- Kept Start with Windows opt-in and HKCU-only.
- Packaged only required executables, runtime DLLs, docs, and example settings.
- Excluded local user data, reports, logs, snapshots, cache, and settings from the release zip.
- Rebuilt the Windows x64 package without a version bump.
