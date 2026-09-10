# Type a string into the WinUAE debugger console via WriteConsoleInput (no focus needed).
# Usage: powershell -NoProfile -File .dbg-type.ps1 <WinUaePid> <text>
param(
    [int]$UaePid,
    [string]$Text
)

$src = @"
using System;
using System.Runtime.InteropServices;

public class ConType {
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool FreeConsole();
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool AttachConsole(uint pid);
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)] public static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr sec, uint disp, uint flags, IntPtr tmpl);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool WriteConsoleInput(IntPtr h, INPUT_RECORD[] recs, uint n, out uint written);

    [StructLayout(LayoutKind.Sequential)]
    public struct KEY_EVENT_RECORD {
        public int bKeyDown;
        public ushort wRepeatCount;
        public ushort wVirtualKeyCode;
        public ushort wVirtualScanCode;
        public char UnicodeChar;
        public uint dwControlKeyState;
    }

    [StructLayout(LayoutKind.Explicit)]
    public struct INPUT_RECORD {
        [FieldOffset(0)] public ushort EventType;
        [FieldOffset(4)] public KEY_EVENT_RECORD KeyEvent;
    }

    public static string Type(uint pid, string text) {
        FreeConsole();
        if (!AttachConsole(pid)) return "AttachConsole failed: " + Marshal.GetLastWin32Error();
        IntPtr h = CreateFileW("CONIN$", 0x40000000 /*GENERIC_WRITE*/, 0x00000003, IntPtr.Zero, 3 /*OPEN_EXISTING*/, 0, IntPtr.Zero);
        if (h == (IntPtr)(-1)) { FreeConsole(); return "CreateFileW CONIN$ failed: " + Marshal.GetLastWin32Error(); }

        var recs = new INPUT_RECORD[text.Length];
        for (int i = 0; i < text.Length; i++) {
            char c = text[i];
            recs[i].EventType = 1; /* KEY_EVENT */
            recs[i].KeyEvent.bKeyDown = 1;
            recs[i].KeyEvent.wRepeatCount = 1;
            recs[i].KeyEvent.wVirtualKeyCode = (c == '\r') ? (ushort)13 : (ushort)0;
            recs[i].KeyEvent.UnicodeChar = c;
        }
        uint written;
        if (!WriteConsoleInput(h, recs, (uint)recs.Length, out written)) {
            FreeConsole();
            return "WriteConsoleInput failed: " + Marshal.GetLastWin32Error();
        }
        FreeConsole();
        return "OK wrote " + written + " records";
    }
}
"@
Add-Type -TypeDefinition $src

# map \n to \r for console input
$Text = $Text -replace "`n", "`r"
$res = [ConType]::Type([uint32]$UaePid, $Text + "`r")
Write-Host $res
