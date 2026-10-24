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
& $cppCompiler @common @cpp (Join-Path $PSScriptRoot 'Test-GamePresentation.cpp') -municode -o (Join-Path $temporary 'Test-GamePresentation.exe')
if ($LASTEXITCODE -ne 0) { throw 'Game presentation test build failed.' }
& (Join-Path $temporary 'Test-GamePresentation.exe') (Join-Path $game 'DarkSoulsRemastered.exe')
if ($LASTEXITCODE -ne 0) { throw 'Game presentation tests failed.' }
& $Compiler @common (Join-Path $PSScriptRoot 'Test-Respec.c') -municode -o (Join-Path $temporary 'Test-Respec.exe')
if ($LASTEXITCODE -ne 0) { throw 'Respec test build failed.' }
& (Join-Path $temporary 'Test-Respec.exe') (Join-Path $game 'DarkSoulsRemastered.exe')
if ($LASTEXITCODE -ne 0) { throw 'Respec tests failed.' }
& $Compiler @common (Join-Path $PSScriptRoot 'Test-ParamOverride.c') -municode -o (Join-Path $temporary 'Test-ParamOverride.exe')
if ($LASTEXITCODE -ne 0) { throw 'Parameter redirection test build failed.' }
$parameterFixture = Join-Path $temporary ([Guid]::NewGuid().ToString())
$parameterDirectory = Join-Path $parameterFixture '.local\game\param\GameParam'
New-Item -ItemType Directory -Path $parameterDirectory -Force | Out-Null
[IO.File]::WriteAllBytes((Join-Path $parameterDirectory 'GameParam.parambnd.dcx'), [Text.Encoding]::ASCII.GetBytes('TEST'))
$assetFixture = Join-Path $parameterFixture '.local\respec'
foreach ($relative in @('script\talk\m10_00_00_00.talkesdbnd.dcx', 'msg\ENGLISH\menu.msgbnd.dcx', 'menu\menu.drb', 'menu\menu.drb.dcx')) {
    $target = Join-Path $assetFixture $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    [IO.File]::WriteAllBytes($target, [Text.Encoding]::ASCII.GetBytes('TEST'))
}
[IO.File]::WriteAllLines((Join-Path $assetFixture 'overrides.txt'), [string[]]@('script\talk\m10_00_00_00.talkesdbnd.dcx', 'msg\ENGLISH\menu.msgbnd.dcx', 'menu\menu.drb', 'menu\menu.drb.dcx'), [Text.Encoding]::ASCII)
& (Join-Path $temporary 'Test-ParamOverride.exe') (Join-Path $game 'DarkSoulsRemastered.exe') $parameterFixture
if ($LASTEXITCODE -ne 0) { throw 'Parameter redirection tests failed.' }
& $Compiler @common (Join-Path $PSScriptRoot 'Test-SaveExtension.c') -municode -o (Join-Path $temporary 'Test-SaveExtension.exe')
if ($LASTEXITCODE -ne 0) { throw 'Save extension test build failed.' }
$saveFixture = Join-Path $temporary ('save-fixture-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $saveFixture -Force | Out-Null
& (Join-Path $temporary 'Test-SaveExtension.exe') (Join-Path $game 'DarkSoulsRemastered.exe') $saveFixture
if ($LASTEXITCODE -ne 0) { throw 'Save extension tests failed.' }
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
