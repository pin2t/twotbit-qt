# Packs the macOS app icon into the Windows icon the executable carries:
# the PNGs of the macOS asset catalog at 16, 32, 64, 128 and 256 pixels, and
# a 48-pixel one scaled from 64, as PNG entries of an .ico file.
#
#     powershell -ExecutionPolicy Bypass -File scripts\make-icon.ps1

param(
    [string]$Source = "$PSScriptRoot\..\..\macos\App\Assets.xcassets\AppIcon.appiconset",
    [string]$Output = "$PSScriptRoot\..\app\resources\twotbit.ico"
)

Add-Type -AssemblyName System.Drawing

function Scaled([string]$path, [int]$side) {
    $image = [System.Drawing.Image]::FromFile($path)
    $bitmap = New-Object System.Drawing.Bitmap $side, $side
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.DrawImage($image, 0, 0, $side, $side)
    $graphics.Dispose()
    $image.Dispose()
    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    return ,$stream.ToArray()
}

$entries = @(
    @{ Side = 16; Data = [System.IO.File]::ReadAllBytes("$Source\icon_16x16.png") },
    @{ Side = 32; Data = [System.IO.File]::ReadAllBytes("$Source\icon_32x32.png") },
    @{ Side = 48; Data = (Scaled "$Source\icon_32x32@2x.png" 48) },
    @{ Side = 64; Data = [System.IO.File]::ReadAllBytes("$Source\icon_32x32@2x.png") },
    @{ Side = 128; Data = [System.IO.File]::ReadAllBytes("$Source\icon_128x128.png") },
    @{ Side = 256; Data = [System.IO.File]::ReadAllBytes("$Source\icon_256x256.png") }
)

$out = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter $out
$writer.Write([UInt16]0)
$writer.Write([UInt16]1)
$writer.Write([UInt16]$entries.Count)
$offset = 6 + 16 * $entries.Count
foreach ($e in $entries) {
    $side = if ($e.Side -ge 256) { 0 } else { $e.Side }
    $writer.Write([Byte]$side)
    $writer.Write([Byte]$side)
    $writer.Write([Byte]0)
    $writer.Write([Byte]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]32)
    $writer.Write([UInt32]$e.Data.Length)
    $writer.Write([UInt32]$offset)
    $offset += $e.Data.Length
}
foreach ($e in $entries) { $writer.Write($e.Data) }
$writer.Flush()
[System.IO.File]::WriteAllBytes($Output, $out.ToArray())
Write-Output "wrote $($entries.Count) icons to $Output"
