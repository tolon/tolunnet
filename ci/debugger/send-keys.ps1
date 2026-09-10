# Send keystrokes to the WinUAE debugger console.
# Usage: powershell -NoProfile -File .dbg-send.ps1 <WinUaePid> <text...>
param(
    [int]$UaePid,
    [Parameter(ValueFromRemainingArguments=$true)][string[]]$Text
)
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class W4 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
}
"@
Add-Type -AssemblyName System.Windows.Forms

$script:con = [IntPtr]::Zero
$cb = {
    param($h, $l)
    $p = 0; [W4]::GetWindowThreadProcessId($h, [ref]$p) | Out-Null
    if ($p -eq $UaePid) {
        $sb = New-Object System.Text.StringBuilder 256
        [W4]::GetClassName($h, $sb, 256) | Out-Null
        if ($sb.ToString() -eq "ConsoleWindowClass") { $script:con = $h }
    }
    return $true
}
[W4]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
if ($script:con -eq [IntPtr]::Zero) { Write-Host "NO CONSOLE WINDOW"; exit 1 }
Write-Host ("console hwnd: 0x" + $script:con.ToString("X"))
[W4]::ShowWindow($script:con, 9) | Out-Null
[W4]::SetForegroundWindow($script:con) | Out-Null
Start-Sleep -Milliseconds 500
foreach ($t in $Text) {
    [System.Windows.Forms.SendKeys]::SendWait($t)
    Start-Sleep -Milliseconds 150
    [System.Windows.Forms.SendKeys]::SendWait("{ENTER}")
    Start-Sleep -Milliseconds 600
}
Write-Host "sent."
