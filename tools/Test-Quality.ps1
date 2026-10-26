param([string]$Compiler = 'D:\AI\Tools\LLVM-MinGW\llvm-mingw-20260908-ucrt-x86_64\bin\clang++.exe')
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$temporary = 'D:\AI\Temp\Q-Souls-1\quality-tests'
New-Item -ItemType Directory -Path $temporary -Force | Out-Null
. (Join-Path $PSScriptRoot 'Build-FpsObjects.ps1')
$objects = @(Build-FpsObjects -Compiler $Compiler -Repository $repository -Temporary $temporary)
$test = Join-Path $temporary 'Test-Quality.exe'
& $Compiler --target=x86_64-w64-mingw32 -std=c++17 -static -fno-exceptions -fno-rtti -O2 -Wall -Wextra -Werror (Join-Path $PSScriptRoot 'Test-Quality.cpp') $objects -ldbghelp -municode -o $test
if ($LASTEXITCODE -ne 0) { throw 'Quality tests build failed.' }
& $test 'D:\Games\Steam\steamapps\common\DARK SOULS REMASTERED\DarkSoulsRemastered.exe' (Join-Path $temporary ([Guid]::NewGuid().ToString('N')))
if ($LASTEXITCODE -ne 0) { throw 'Quality tests failed.' }
