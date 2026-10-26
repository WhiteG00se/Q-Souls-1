function Build-FpsObjects {
    param([string]$Compiler, [string]$Repository, [string]$Temporary)
    $source = Join-Path $Repository 'third_party\ds1-fps-unlock\src'
    foreach ($relative in @('common\log.cpp', 'common\diag.cpp', 'dsr\patches.cpp', 'dsr\trace.cpp', 'dsr\watch.cpp', 'dsr\bonfire.cpp')) {
        $object = Join-Path $Temporary ('fps-' + [IO.Path]::GetFileNameWithoutExtension($relative) + '.o')
        & $Compiler --target=x86_64-w64-mingw32 -std=c++17 -fms-extensions -O2 -I (Join-Path $source 'common') -I (Join-Path $source 'dsr') -c (Join-Path $source $relative) -o $object
        if ($LASTEXITCODE -ne 0) { throw "FPS module compilation failed: $relative" }
        $object
    }
}
