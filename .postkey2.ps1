param([int]$TargetPid)
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class PK2 {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
}
"@
Add-Type -AssemblyName System.Windows.Forms
$script:main = [IntPtr]::Zero
$cb = {
  param($h, $l)
  $p = 0; [PK2]::GetWindowThreadProcessId($h, [ref]$p) | Out-Null
  if ($p -eq $TargetPid) {
    $sb = New-Object System.Text.StringBuilder 256
    [PK2]::GetClassName($h, $sb, 256) | Out-Null
    if ($sb.ToString() -eq "PCsuxRox") { $script:main = $h }
  }
  return $true
}
[PK2]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
Write-Host ("main: 0x" + $script:main.ToString("X"))
# become the last-input process with a harmless key (F13)
[System.Windows.Forms.SendKeys]::SendWait("{F13}")
Start-Sleep -Milliseconds 100
[PK2]::ShowWindow($script:main, 9) | Out-Null
$ok = [PK2]::SetForegroundWindow($script:main)
Start-Sleep -Milliseconds 150
$fg = [PK2]::GetForegroundWindow()
Write-Host ("fg-ok=" + $ok + " isfg=" + ($fg -eq $script:main))
if ($fg -eq $script:main) {
  [System.Windows.Forms.SendKeys]::SendWait("+{F12}")
  Write-Host "sent-shift-f12"
}
