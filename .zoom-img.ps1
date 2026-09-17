# Crop a photo region and save as JPEG: .zoom-img.ps1 <in> <out.jpg> <x%> <y%> <w%> <h%> [outwidth]
param(
    [string]$In,
    [string]$Out,
    [double]$Xp, [double]$Yp, [double]$Wp, [double]$Hp,
    [int]$OutW = 1800
)
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Image]::FromFile($In)
$w = $src.Width; $h = $src.Height
$x = [int]($w * $Xp / 100.0); $y = [int]($h * $Yp / 100.0)
$cw = [int]($w * $Wp / 100.0); $ch = [int]($h * $Hp / 100.0)
$ow = $OutW
$oh = [int]($ch * $ow / $cw)
$bmp = New-Object System.Drawing.Bitmap $ow, $oh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.DrawImage($src, (New-Object System.Drawing.Rectangle(0, 0, $ow, $oh)),
    (New-Object System.Drawing.Rectangle($x, $y, $cw, $ch)), [System.Drawing.GraphicsUnit]::Pixel)
$g.Dispose()
$codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq "image/jpeg" }
$ep = New-Object System.Drawing.Imaging.EncoderParameters(1)
$ep.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter([System.Drawing.Imaging.Encoder]::Quality, [long]90)
$bmp.Save($Out, $codec, $ep)
$bmp.Dispose(); $src.Dispose()
Write-Host "saved $Out ${ow}x${oh} from ${x},${y} ${cw}x${ch}"
