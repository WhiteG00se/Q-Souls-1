param([string]$Compiler = 'D:\AI\Tools\LLVM-MinGW\llvm-mingw-20260908-ucrt-x86_64\bin\clang.exe')
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) { throw 'Provide -Compiler with an x64 LLVM-MinGW clang.exe path.' }
$temporary = 'D:\AI\Temp\Q-Souls-1\runtime-build'
New-Item -ItemType Directory -Path $temporary -Force | Out-Null
$cppCompiler = Join-Path (Split-Path -Parent $Compiler) 'clang++.exe'
& $Compiler --target=x86_64-w64-mingw32 -c -O2 -Wall -Wextra -Werror `
    (Join-Path $repository 'runtime\TitleMusic.c') `
    -o (Join-Path $temporary 'TitleMusic.o')
if ($LASTEXITCODE -ne 0) { throw 'Runtime compilation failed.' }
& $cppCompiler --target=x86_64-w64-mingw32 -std=c++17 -shared -static -fno-exceptions -fno-rtti -O2 -Wall -Wextra -Werror `
    (Join-Path $temporary 'TitleMusic.o') (Join-Path $repository 'runtime\TitleOverlay.cpp') (Join-Path $repository 'runtime\ControllerHelper.cpp') `
    -lgdi32 -ldxguid '-Wl,--no-insert-timestamp' -o (Join-Path $repository 'runtime\QSoulsRuntime.dll')
if ($LASTEXITCODE -ne 0) { throw 'Runtime build failed.' }
