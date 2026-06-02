# Release Checklist

Use this checklist before replacing a GitHub release asset.

- Build a clean Release configuration.
- Package from the release script.
- Confirm the release folder contains only required executables, runtime DLLs, docs, and example settings.
- Confirm no local user data is packaged:
  - `settings.json`
  - `config.json`
  - `match_history/`
  - `reports/`
  - `snapshots/`
  - `logs/`
  - `cache/`
  - `saved_matches/`
- Confirm no debug artifacts are packaged:
  - `.pdb`
  - debug folders
  - temporary files
  - test executables
- Confirm Windows executable metadata is present.
- Confirm executables request normal user privileges only.
- Confirm Start with Windows is disabled by default and opt-in.
- Confirm the SHA256 file is generated next to the zip.
- Extract the zip and run `OpenLeagueOverlay.exe --help`.
- Confirm README and CHANGELOG are up to date.
- Confirm GitHub release notes mention signing status if the build is unsigned.
- Upload the zip and `.sha256` file to the GitHub release.
- Verify the release download links work.
