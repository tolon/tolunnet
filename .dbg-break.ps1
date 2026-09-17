param([int]$TargetPid)
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class WD {
  public delegate bool CB(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(CB cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  public struct RECT { public int L, T, R, B; }
}
'@
Add-Type -AssemblyName System.Drawing
$script:found = $null
$script:targetPid = $TargetPid
function Find-Win([string]$pattern) {
  $script:found = $null
  $cb = {
    param($h, $l)
    $procId = 0
    [WD]::GetWindowThreadProcessId($h, [ref]$procId) | Out-Null
    if ($procId -eq $script:targetPid -and [WD]::IsWindowVisible($h)) {
      $sb = New-Object System.Text.StringBuilder 256
      [WD]::GetWindowText($h, $sb, 256) | Out-Null
      if ($sb.ToString() -like $pattern) {
        $r = New-Object WD+RECT
        [WD]::GetWindowRect($h, [ref]$r) | Out-Null
        $script:found = @{H=$h; T=$sb.ToString(); R=$r}
      }
    }
    return $true
  }
  [WD]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
  return $script:found
}
$main = Find-Win '*tolunnet-68000*'
if (-not $main) { Write-Host "NO_MAIN_WINDOW"; exit 1 }
[WD]::SetForegroundWindow($main.H) | Out-Null
Start-Sleep -Milliseconds 600
$ws = New-Object -ComObject WScript.Shell
$ws.SendKeys('+{F12}')
Start-Sleep -Milliseconds 1200
$dbg = Find-Win '*Arabuusimiehet*'
if (-not $dbg) { $dbg = Find-Win '*WinUAE' }
if (-not $dbg) { Write-Host "NO_DEBUGGER_WINDOW"; exit 2 }
[WD]::SetForegroundWindow($dbg.H) | Out-Null
Start-Sleep -Milliseconds 500
$ws.SendKeys('r')
$ws.SendKeys('{ENTER}')
Start-Sleep -Milliseconds 900
$w = $dbg.R.R - $dbg.R.L; $h = $dbg.R.B - $dbg.R.T
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($dbg.R.L, $dbg.R.T, 0, 0, $bmp.Size)
$bmp.Save('D:\Projeler\tolunnet\.dbg-regs.png')
Write-Host ("CAPTURED {0}x{1} title='{2}'" -f $w, $h, $dbg.T)
