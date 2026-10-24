param([string]$Compiler = 'D:\AI\Tools\LLVM-MinGW\llvm-mingw-20260908-ucrt-x86_64\bin\clang.exe')
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$temporary = 'D:\AI\Temp\Q-Souls-1\runtime-tests'
$game = 'D:\Games\Steam\steamapps\common\DARK SOULS REMASTERED'
foreach ($file in @('DarkSoulsRemastered.exe', 'fmodex64.dll', 'fmod_event64.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $game $file) -PathType Leaf)) {
        throw "Missing game component: $game\$file. Provide the correct development path or install the game."
    }
}
New-Item -ItemType Directory -Path $temporary -Force | Out-Null
$cppCompiler = Join-Path (Split-Path -Parent $Compiler) 'clang++.exe'
$common = @('--target=x86_64-w64-mingw32', '-O2', '-Wall', '-Wextra', '-Werror')
$cpp = @('-std=c++17', '-static', '-fno-exceptions', '-fno-rtti')
& $Compiler @common (Join-Path $PSScriptRoot 'Test-IntroSkip.c') -municode -o (Join-Path $temporary 'Test-IntroSkip.exe')
if ($LASTEXITCODE -ne 0) { throw 'Intro-skip test build failed.' }
& (Join-Path $temporary 'Test-IntroSkip.exe') (Join-Path $game 'DarkSoulsRemastered.exe')
if ($LASTEXITCODE -ne 0) { throw 'Intro-skip tests failed.' }
& $cppCompiler @common @cpp (Join-Path $PSScriptRoot 'Test-TitleOverlay.cpp') `
    -lgdi32 -ldxguid -ld3d11 -ldxgi -municode -o (Join-Path $temporary 'Test-TitleOverlay.exe')
if ($LASTEXITCODE -ne 0) { throw 'Overlay test build failed.' }
$fixture = Join-Path $temporary ([Guid]::NewGuid().ToString())
& (Join-Path $temporary 'Test-TitleOverlay.exe') $repository $fixture
if ($LASTEXITCODE -ne 0) { throw 'Overlay tests failed.' }
& $Compiler @common -c (Join-Path $PSScriptRoot 'Test-TitleMusic.c') -o (Join-Path $temporary 'Test-TitleMusic.o')
if ($LASTEXITCODE -ne 0) { throw 'Music test compilation failed.' }
& $cppCompiler @common @cpp (Join-Path $temporary 'Test-TitleMusic.o') `
    (Join-Path $repository 'runtime\TitleOverlay.cpp') -lgdi32 -ldxguid -municode -o (Join-Path $temporary 'Test-TitleMusic.exe')
if ($LASTEXITCODE -ne 0) { throw 'Music test link failed.' }
& (Join-Path $temporary 'Test-TitleMusic.exe') $game (Join-Path $repository 'assets\audio\title-theme.wav') (Join-Path $temporary 'music.log')
if ($LASTEXITCODE -ne 0) { throw 'Music tests failed.' }
