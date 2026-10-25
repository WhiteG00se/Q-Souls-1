param([string]$Compiler = 'D:\AI\Tools\LLVM-MinGW\llvm-mingw-20260908-ucrt-x86_64\bin\clang++.exe')
$ErrorActionPreference = 'Stop'
$temporary = 'D:\AI\Temp\Q-Souls-1\controller-tests'
New-Item -ItemType Directory -Path $temporary -Force | Out-Null
$fixture = Join-Path $temporary ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $fixture '.local') -Force | Out-Null
$test = Join-Path $temporary 'Test-ControllerHelper.exe'
& $Compiler -std=c++17 -static -fno-exceptions -fno-rtti -O2 -Wall -Wextra -Werror (Join-Path $PSScriptRoot 'Test-ControllerHelper.cpp') -ldxguid -municode -o $test
if ($LASTEXITCODE -ne 0) { throw 'Controller helper test build failed.' }
& $test 'D:\Games\Steam\steamapps\common\DARK SOULS REMASTERED\DarkSoulsRemastered.exe' $fixture
if ($LASTEXITCODE -ne 0) { throw 'Controller helper tests failed.' }
