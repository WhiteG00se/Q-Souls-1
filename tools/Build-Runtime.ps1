param([string]$Compiler = 'D:\AI\Tools\LLVM-MinGW\llvm-mingw-20260908-ucrt-x86_64\bin\clang.exe')
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) { throw 'Provide -Compiler with an x64 LLVM-MinGW clang.exe path.' }
& $Compiler --target=x86_64-w64-mingw32 -shared -O2 -Wall -Wextra -Werror `
    (Join-Path $repository 'runtime\TitleMusic.c') -lwinmm `
    '-Wl,--no-insert-timestamp' -o (Join-Path $repository 'runtime\QSoulsRuntime.dll')
if ($LASTEXITCODE -ne 0) { throw 'Runtime build failed.' }
