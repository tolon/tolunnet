param([int]$TargetPid)
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class PK {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
}
"@
$script:main = [IntPtr]::Zero
$cb = {
  param($h, $l)
  $p = 0; [PK]::GetWindowThreadProcessId($h, [ref]$p) | Out-Null
  if ($p -eq $TargetPid) {
    $sb = New-Object System.Text.StringBuilder 256
    [PK]::GetClassName($h, $sb, 256) | Out-Null
    if ($sb.ToString() -eq "PCsuxRox") { $script:main = $h }
  }
  return $true
}
[PK]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
Write-Host ("main: 0x" + $script:main.ToString("X"))
# Shift down, F12 down/up, Shift up via WM_KEYDOWN/UP
[PK]::PostMessage($script:main, 0x0100, [IntPtr]0x10, [IntPtr]0x002A0001) | Out-Null  # VK_SHIFT down
Start-Sleep -Milliseconds 80
[PK]::PostMessage($script:main, 0x0100, [IntPtr]0x77, [IntPtr]0x00580001) | Out-Null  # VK_F12 down
Start-Sleep -Milliseconds 80
[PK]::PostMessage($script:main, 0x0101, [IntPtr]0x77, [IntPtr]0xC0580001) | Out-Null  # F12 up
[PK]::PostMessage($script:main, 0x0101, [IntPtr]0x10, [IntPtr]0xC02A0001) | Out-Null  # Shift up
Write-Host "posted"
