$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$cmake = "C:\Program Files\JetBrains\CLion 2026.1\bin\cmake\win\x64\bin\cmake.exe"
$mingwBin = "C:\Program Files\JetBrains\CLion 2026.1\bin\mingw\bin"

$env:Path = "$mingwBin;$env:Path"
& $cmake --build (Join-Path $repoRoot "cmake-build-release") --config Release
if ($LASTEXITCODE -ne 0) {
    throw "Release build failed with exit code $LASTEXITCODE"
}
