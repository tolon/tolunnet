# Tight crop + high upscale + contrast: .zoom2.ps1 <in> <out.jpg> <x%> <y%> <w%> <h%>
param(
    [string]$In,
    [string]$Out,
    [double]$Xp, [double]$Yp, [double]$Wp, [double]$Hp
)
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Image]::FromFile($In)
$w = $src.Width; $h = $src.Height
$x = [int]($w * $Xp / 100.0); $y = [int]($h * $Yp / 100.0)
$cw = [int]($w * $Wp / 100.0); $ch = [int]($h * $Hp / 100.0)
$ow = 2200
$oh = [int]($ch * $ow / $cw)
$bmp = New-Object System.Drawing.Bitmap $ow, $oh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.DrawImage($src, (New-Object System.Drawing.Rectangle(0, 0, $ow, $oh)),
    (New-Object System.Drawing.Rectangle($x, $y, $cw, $ch)), [System.Drawing.GraphicsUnit]::Pixel)
$g.Dispose()
# contrast stretch via ColorMatrix scale+offset (2.2x contrast around 0.5)
$cm = New-Object System.Drawing.Imaging.ColorMatrix
for ($i = 0; $i -lt 3; $i++) {
    $cm.Matrix00 = 2.2; $cm.Matrix11 = 2.2; $cm.Matrix22 = 2.2
}
$cm.Matrix33 = 1.0; $cm.Matrix44 = 1.0
# offset row (Matrix40..42) = -0.55 pulls 0.5 mid to 0
$cm.Matrix40 = -0.55; $cm.Matrix41 = -0.55; $cm.Matrix42 = -0.55
$ia = New-Object System.Drawing.Imaging.ImageAttributes
$ia.SetColorMatrix($cm)
$bmp2 = New-Object System.Drawing.Bitmap $ow, $oh
$g2 = [System.Drawing.Graphics]::FromImage($bmp2)
$g2.DrawImage($bmp, (New-Object System.Drawing.Rectangle(0, 0, $ow, $oh)), 0, 0, $ow, $oh, [System.Drawing.GraphicsUnit]::Pixel, $ia)
$g2.Dispose()
$codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq "image/jpeg" }
$ep = New-Object System.Drawing.Imaging.EncoderParameters(1)
$ep.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter([System.Drawing.Imaging.Encoder]::Quality, [long]92)
$bmp2.Save($Out, $codec, $ep)
$bmp.Dispose(); $bmp2.Dispose(); $src.Dispose()
Write-Host "saved $Out ${ow}x${oh} from ${x},${y} ${cw}x${ch}"
