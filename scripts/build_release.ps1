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

$buildDir = Join-Path $repoRoot "cmake-build-release"

$env:Path = "$mingwBin;$env:Path"
& $cmake -S $repoRoot -B $buildDir -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    "-DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic" `
    "-DCMAKE_BUILD_TYPE=Release"
if ($LASTEXITCODE -ne 0) {
    throw "Release configure failed with exit code $LASTEXITCODE"
}

& $cmake --build $buildDir --config Release
if ($LASTEXITCODE -ne 0) {
    throw "Release build failed with exit code $LASTEXITCODE"
}
