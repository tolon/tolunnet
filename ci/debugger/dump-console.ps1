# Dump a console window's screen buffer text via AttachConsole + ReadConsoleOutputCharacter
# Usage: powershell -NoProfile -File .dbg-dumpconsole.ps1 <ConhostPid> <OutFile>
param(
    [int]$ConhostPid = 0,
    [string]$OutFile = "D:\Projeler\tolunnet\.dbg-console.txt"
)

$src = @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public class ConDump {
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool FreeConsole();
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool AttachConsole(uint pid);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr GetStdHandle(int which);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool GetConsoleScreenBufferInfo(IntPtr h, out CONSOLE_SCREEN_BUFFER_INFO info);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadConsoleOutputCharacter(IntPtr h, [Out] StringBuilder buf, uint len, COORD origin, out uint read);
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)] public static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr sec, uint disp, uint flags, IntPtr tmpl);

    [StructLayout(LayoutKind.Sequential)] public struct COORD { public short X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct SMALL_RECT { public short L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct CONSOLE_SCREEN_BUFFER_INFO {
        public COORD dwSize; public COORD dwCursorPosition; public short wAttributes;
        public SMALL_RECT srWindow; public COORD dwMaximumWindowSize;
    }

    public static string Dump(uint pid) {
        FreeConsole();
        if (!AttachConsole(pid)) return "AttachConsole failed: " + Marshal.GetLastWin32Error();
        IntPtr h = CreateFileW("CONOUT$", 0xC0000000 /*GENERIC_READ|WRITE*/, 0x00000003 /*READ|WRITE share*/, IntPtr.Zero, 3 /*OPEN_EXISTING*/, 0, IntPtr.Zero);
        if (h == (IntPtr)(-1)) { FreeConsole(); return "CreateFileW CONOUT$ failed: " + Marshal.GetLastWin32Error(); }
        CONSOLE_SCREEN_BUFFER_INFO info;
        if (!GetConsoleScreenBufferInfo(h, out info)) { FreeConsole(); return "GetConsoleScreenBufferInfo failed: " + Marshal.GetLastWin32Error(); }
        var sb = new StringBuilder();
        uint read;
        for (short y = 0; y < info.dwSize.Y; y++) {
            var line = new StringBuilder(info.dwSize.X + 1);
            if (ReadConsoleOutputCharacter(h, line, (uint)info.dwSize.X, new COORD { X = 0, Y = y }, out read)) {
                sb.AppendLine(line.ToString(0, (int)read).TrimEnd());
            } else {
                sb.AppendLine("<read fail y=" + y + " err=" + Marshal.GetLastWin32Error() + ">");
            }
        }
        FreeConsole();
        return sb.ToString();
    }
}
"@
Add-Type -TypeDefinition $src

$txt = [ConDump]::Dump([uint32]$ConhostPid)
[System.IO.File]::WriteAllText($OutFile, $txt)
Write-Host ("dumped " + $txt.Length + " chars to " + $OutFile)
