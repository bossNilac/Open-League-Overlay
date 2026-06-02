$ErrorActionPreference = "Stop"

powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "package.ps1") -Configuration Release
