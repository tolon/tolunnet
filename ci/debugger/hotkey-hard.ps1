# Hard-focus hotkey sender: AttachThreadInput + SetForegroundWindow + SendInput.
# Usage: hotkey-hard.ps1 <pid> [retries]
param(
    [int]$TargetPid,
    [int]$Retries = 4
)
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class HK {
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr dummy);
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
}
"@
Add-Type -AssemblyName System.Windows.Forms

$script:main = [IntPtr]::Zero
$cb = {
    param($h, $l)
    $p = 0; [HK]::GetWindowThreadProcessId($h, [ref]$p) | Out-Null
    if ($p -eq $TargetPid) {
        $sb = New-Object System.Text.StringBuilder 256
        [HK]::GetClassName($h, $sb, 256) | Out-Null
        if ($sb.ToString() -eq "PCsuxRox") { $script:main = $h }
    }
    return $true
}
[HK]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
if ($script:main -eq [IntPtr]::Zero) { Write-Host "NO-MAIN"; exit 1 }

for ($i = 0; $i -lt $Retries; $i++) {
    $fg = [HK]::GetForegroundWindow()
    $fgT = 0; [HK]::GetWindowThreadProcessId($fg, $fgT) | Out-Null
    $tgtT = 0; [HK]::GetWindowThreadProcessId($script:main, $tgtT) | Out-Null
    $me = [HK]::GetCurrentThreadId()
    [HK]::AttachThreadInput($me, $fgT, $true) | Out-Null
    [HK]::AttachThreadInput($me, $tgtT, $true) | Out-Null
    [HK]::ShowWindow($script:main, 9) | Out-Null
    [HK]::SetForegroundWindow($script:main) | Out-Null
    Start-Sleep -Milliseconds 120
    [HK]::AttachThreadInput($me, $fgT, $false) | Out-Null
    [HK]::AttachThreadInput($me, $tgtT, $false) | Out-Null

    $ok = ([HK]::GetForegroundWindow() -eq $script:main)
    Write-Host ("attempt " + $i + " foreground=" + $ok)
    if ($ok) {
        [System.Windows.Forms.SendKeys]::SendWait("+{F12}")
        Start-Sleep -Milliseconds 400
        Write-Host "sent"
        exit 0
    }
    Start-Sleep -Milliseconds 250
}
Write-Host "FOREGINT-FAILED"
exit 2
