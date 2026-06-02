param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$cmake = if ($env:CMAKE_EXE) { $env:CMAKE_EXE } else { "C:\Program Files\JetBrains\CLion 2026.1\bin\cmake\win\x64\bin\cmake.exe" }
$ninja = if ($env:NINJA_EXE) { $env:NINJA_EXE } else { "C:\Program Files\JetBrains\CLion 2026.1\bin\ninja\win\x64\ninja.exe" }
$mingwBin = if ($env:MINGW_BIN) { $env:MINGW_BIN } else { "C:\Program Files\JetBrains\CLion 2026.1\bin\mingw\bin" }
$toolchain = $env:VCPKG_TOOLCHAIN_FILE
if (-not $toolchain -and $env:VCPKG_ROOT) {
    $toolchain = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
}
if (-not $toolchain) {
    $toolchain = Join-Path $env:USERPROFILE ".vcpkg-clion\vcpkg\scripts\buildsystems\vcpkg.cmake"
}
if (-not (Test-Path -LiteralPath $toolchain)) {
    throw "Could not find vcpkg toolchain. Set VCPKG_TOOLCHAIN_FILE or VCPKG_ROOT."
}
$buildDir = Join-Path $repoRoot "cmake-build-$($Configuration.ToLowerInvariant())"
$releaseName = "OpenLeagueOverlay-v1.0.0-windows-x64"
$distDir = Join-Path $repoRoot "dist\$releaseName"
$distRoot = Join-Path $repoRoot "dist"
$zipPath = Join-Path $repoRoot "dist\$releaseName.zip"

$repoRootFull = [System.IO.Path]::GetFullPath($repoRoot)
$distRootFull = [System.IO.Path]::GetFullPath($distRoot)
$distDirFull = [System.IO.Path]::GetFullPath($distDir)
$zipPathFull = [System.IO.Path]::GetFullPath($zipPath)
if (-not $distDirFull.StartsWith($distRootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to package outside the repository dist folder: $distDirFull"
}
if (-not $zipPathFull.StartsWith($distRootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to create zip outside the repository dist folder: $zipPathFull"
}

$env:Path = "$mingwBin;$env:Path"

& $cmake -S $repoRoot -B $buildDir -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    "-DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic" `
    "-DCMAKE_BUILD_TYPE=$Configuration"
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE"
}

& $cmake --build $buildDir --config $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE"
}

if (Test-Path -LiteralPath $distDir) {
    Remove-Item -LiteralPath $distDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $distDir | Out-Null

Copy-Item -Path (Join-Path $buildDir "*.dll") -Destination $distDir -Force
Copy-Item -LiteralPath (Join-Path $buildDir "OpenLeagueOverlay.exe") -Destination $distDir -Force
Copy-Item -LiteralPath (Join-Path $buildDir "OpenLeagueOverlayGui.exe") -Destination $distDir -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "README.md") -Destination $distDir -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "CHANGELOG.md") -Destination $distDir -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "settings.example.json") -Destination $distDir -Force
if (Test-Path -LiteralPath (Join-Path $repoRoot "LICENSE")) {
    Copy-Item -LiteralPath (Join-Path $repoRoot "LICENSE") -Destination $distDir -Force
}

if (Test-Path -LiteralPath $zipPathFull) {
    Remove-Item -LiteralPath $zipPathFull -Force
}
Compress-Archive -Path (Join-Path $distDir "*") -DestinationPath $zipPathFull -Force

Write-Host "Packaged portable executable folder:"
Write-Host $distDirFull
Write-Host "Zip:"
Write-Host $zipPathFull
