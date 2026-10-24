$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$repository = Split-Path -Parent $PSScriptRoot
$images = @()
foreach ($size in @(16, 24, 32, 48, 64, 128, 256)) {
    $bitmap = New-Object Drawing.Bitmap($size, $size)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $font = New-Object Drawing.Font('Segoe UI', ($size * 0.32), [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
    $pen = New-Object Drawing.Pen([Drawing.Color]::FromArgb(201, 164, 88), ([Math]::Max(1, $size / 32)))
    $brush = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(236, 205, 137))
    $format = New-Object Drawing.StringFormat
    $memory = New-Object IO.MemoryStream
    try {
        $graphics.Clear([Drawing.Color]::FromArgb(28, 29, 33))
        $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit
        $inset = [Math]::Max(1, $size / 14)
        $graphics.DrawRectangle($pen, $inset, $inset, ($size - 2 * $inset - 1), ($size - 2 * $inset - 1))
        $format.Alignment = [Drawing.StringAlignment]::Center
        $format.LineAlignment = [Drawing.StringAlignment]::Center
        $graphics.DrawString('QS1', $font, $brush, (New-Object Drawing.RectangleF(0, 0, $size, $size)), $format)
        $bitmap.Save($memory, [Drawing.Imaging.ImageFormat]::Png)
        $images += ,@{ Size = $size; Bytes = $memory.ToArray() }
    } finally { $memory.Dispose(); $format.Dispose(); $brush.Dispose(); $pen.Dispose(); $font.Dispose(); $graphics.Dispose(); $bitmap.Dispose() }
}
$path = Join-Path $repository 'launcher\QS1.ico'
$stream = [IO.File]::Create($path)
$writer = New-Object IO.BinaryWriter($stream)
try {
    $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$images.Count)
    $offset = 6 + 16 * $images.Count
    foreach ($entry in $images) {
        $dimension = if ($entry.Size -eq 256) { 0 } else { $entry.Size }
        $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
        $writer.Write([byte]0); $writer.Write([byte]0); $writer.Write([uint16]1); $writer.Write([uint16]32)
        $writer.Write([uint32]$entry.Bytes.Length); $writer.Write([uint32]$offset)
        $offset += $entry.Bytes.Length
    }
    foreach ($entry in $images) { $writer.Write([byte[]]$entry.Bytes) }
} finally { $writer.Dispose(); $stream.Dispose() }
