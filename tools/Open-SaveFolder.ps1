$ErrorActionPreference = 'Stop'
try {
    $saveRoot = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'NBGI\DARK SOULS REMASTERED'
    if (-not (Test-Path -LiteralPath $saveRoot -PathType Container)) {
        throw 'No Dark Souls Remastered save folder found. Create a character first.'
    }
    $folders = @(Get-ChildItem -LiteralPath $saveRoot -Directory)
    if ($folders.Count -eq 1) { $saveRoot = $folders[0].FullName }
    Invoke-Item -LiteralPath $saveRoot
    exit 0
}
catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
