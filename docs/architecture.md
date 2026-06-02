# Architecture

Open League Overlay is a small native Windows companion app.

```text
League Live Client Data API
  -> API client
  -> JSON parser and state model
  -> TUI renderer
  -> GUI dashboard
  -> transparent overlay renderer
  -> performance tracker
  -> local match history and report generator
```

## Data Sources

Live in-game data comes from the local League Live Client Data API:

```text
https://127.0.0.1:2999/liveclientdata/allgamedata
```

Champion-select features use the local League client lockfile and local client endpoints such as champion-select, perks, and item-set endpoints.

OP.GG matchup/build data is requested only for recommendations. Saved match reports remain local.

## Local Storage

User data is stored under:

```text
%LOCALAPPDATA%\OpenLeagueOverlay\
```

This includes settings, match history, reports, snapshots, logs, and cache. These files are not packaged into GitHub releases.

## Main Source Areas

- `src/main.cpp`: TUI entry point and GUI launch handoff.
- `src/game_tui.cpp`: terminal scoreboard and champ-select TUI rendering.
- `src/overlay_main.cpp`: native Win32 GUI dashboard and transparent overlay rendering.
- `src/overlay_data.cpp`: live data transformation for overlay and scoreboard views.
- `src/champ_select.cpp`: local League client champ-select state, rune import, item-set import, and recommendation parsing.
- `src/lcu_client.cpp`: League client lockfile discovery and authenticated local client requests.
- `src/api/`: HTTP and JSON helpers.
- `src/performance_tracker.cpp`: live snapshot collection and final report writing.
- `src/performance_scorer.cpp`: OL Score calculation.
- `src/match_history.cpp`: local match history/report loading.
- `include/`: project headers.
- `resources/`: Windows manifests and executable metadata.
- `scripts/`: build and release packaging helpers.

## Boundaries

The app does not:

- Inject into League.
- Hook DirectX.
- Read League process memory.
- Automate gameplay.
- Modify game files.
- Upload saved matches or reports.

Start with Windows is opt-in, current-user only, and does not require administrator privileges.
