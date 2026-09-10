# Open WinUAE debugger with verification, then dump console buffer.
# Usage: powershell -NoProfile -File .dbg-break2.ps1 <WinUaePid> [cmd1 cmd2 ...]
param(
    [int]$UaePid = 32264,
    [string[]]$Cmds = @("r")
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class W3 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
}
"@
Add-Type -AssemblyName System.Windows.Forms

# Find the main emulation window (class PCsuxRox) of the pid
$script:main = [IntPtr]::Zero
$cb = {
    param($h, $l)
    $p = 0; [W3]::GetWindowThreadProcessId($h, [ref]$p) | Out-Null
    if ($p -eq $UaePid) {
        $sb = New-Object System.Text.StringBuilder 256
        [W3]::GetClassName($h, $sb, 256) | Out-Null
        if ($sb.ToString() -eq "PCsuxRox") { $script:main = $h }
    }
    return $true
}
[W3]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
if ($script:main -eq [IntPtr]::Zero) { Write-Host "NO MAIN WINDOW"; exit 1 }
Write-Host ("main window: 0x" + $script:main.ToString("X"))

[W3]::ShowWindow($script:main, 9) | Out-Null  # SW_RESTORE
[W3]::SetForegroundWindow($script:main) | Out-Null
Start-Sleep -Milliseconds 600

[System.Windows.Forms.SendKeys]::SendWait("+{F12}")
Write-Host "sent Shift+F12"
Start-Sleep -Milliseconds 1500

# type commands
foreach ($c in $Cmds) {
    [System.Windows.Forms.SendKeys]::SendWait($c)
    Start-Sleep -Milliseconds 120
    [System.Windows.Forms.SendKeys]::SendWait("{ENTER}")
    Start-Sleep -Milliseconds 700
}

# dump console
& powershell.exe -NoProfile -File "D:\Projeler\tolunnet\.dbg-dumpconsole.ps1" $UaePid "D:\Projeler\tolunnet\.dbg-console.txt"
