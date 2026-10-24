param([string]$Compiler = 'D:\AI\Tools\LLVM-MinGW\llvm-mingw-20260908-ucrt-x86_64\bin\clang++.exe')
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$temporary = 'D:\AI\Temp\Q-Souls-1\runtime-tests'
New-Item -ItemType Directory -Path $temporary -Force | Out-Null
$test = Join-Path $temporary 'Test-RTSS.exe'
& $Compiler --target=x86_64-w64-mingw32 -std=c++17 -static -fno-exceptions -fno-rtti -O2 -Wall -Wextra -Werror `
    (Join-Path $PSScriptRoot 'Test-RTSS.cpp') -lgdi32 -ldxguid -ld3d11 -ldxgi -municode -o $test
if ($LASTEXITCODE -ne 0) { throw 'RTSS compatibility test build failed.' }
& $test $repository
if ($LASTEXITCODE -ne 0) { throw 'RTSS compatibility check failed.' }
