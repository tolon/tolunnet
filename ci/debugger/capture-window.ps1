# Capture WinUAE debugger console via PrintWindow(PW_RENDERFULLCONTENT)
# Usage: powershell -NoProfile -File .dbg-capture.ps1 <Pid> <OutFile>
param(
    [int]$TargetPid = 0,
    [string]$OutFile = "D:\Projeler\tolunnet\.dbg-pw.png"
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class WinCap {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@

$script:found = @()

$cb = {
    param($h, $l)
    $pid2 = 0
    [WinCap]::GetWindowThreadProcessId($h, [ref]$pid2) | Out-Null
    if ($TargetPid -eq 0 -or $pid2 -eq $TargetPid) {
        $sb = New-Object System.Text.StringBuilder 256
        [WinCap]::GetClassName($h, $sb, 256) | Out-Null
        $cls = $sb.ToString()
        $st = New-Object System.Text.StringBuilder 256
        [WinCap]::GetWindowText($h, $st, 256) | Out-Null
        $ttl = $st.ToString()
        if ($cls -like "*WinUAE*" -or $cls -like "*Arabuusimiehet*" -or $ttl -like "*Arabuusimiehet*" -or $ttl -like "*WinUAE*") {
            $script:found += ,@($h, ($cls + " | " + $ttl), $pid2)
        }
    }
    return $true
}
[WinCap]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null

Write-Host ("windows found: " + $script:found.Count)
foreach ($entry in $script:found) {
    $h = [IntPtr]$entry[0]
    $cls = $entry[1]
    $p = $entry[2]
    $r = New-Object WinCap+RECT
    [WinCap]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.R - $r.L
    $ht = $r.B - $r.T
    Write-Host ("hwnd=0x" + $h.ToString("X") + " class=" + $cls + " pid=" + $p + " rect=" + $w + "x" + $ht)
    if ($w -lt 10 -or $ht -lt 10) { continue }

    $bmp = New-Object System.Drawing.Bitmap $w, $ht
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    # flag 2 = PW_RENDERFULLCONTENT (renders DirectX/DWM content)
    $ok = [WinCap]::PrintWindow($h, $hdc, 2)
    $g.ReleaseHdc($hdc)
    $g.Dispose()
    Write-Host ("  PrintWindow(2): " + $ok)
    if (-not $ok) {
        $g2 = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc2 = $g2.GetHdc()
        $ok2 = [WinCap]::PrintWindow($h, $hdc2, 0)
        $g2.ReleaseHdc($hdc2)
        $g2.Dispose()
        Write-Host ("  PrintWindow(0): " + $ok2)
    }
    $name = $OutFile
    if ($script:found.Count -gt 1) {
        $name = $OutFile.Replace(".png", ("-" + $p + "-" + [Math]::Abs($h.ToInt64() % 10000) + ".png"))
    }
    $bmp.Save($name, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host ("  saved: " + $name)
}
