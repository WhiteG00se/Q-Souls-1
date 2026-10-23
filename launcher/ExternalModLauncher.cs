using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

// Start the installed game, loading an external DLL before its main thread runs.
public static class ExternalModLauncher
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct StartupInfo
    {
        public int cb;
        public string reserved, desktop, title;
        public int x, y, width, height, xChars, yChars, fill, flags;
        public short show, reservedSize;
        public IntPtr reservedData, stdin, stdout, stderr;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct ProcessInfo { public IntPtr process, thread; public uint processId, threadId; }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool CreateProcess(string app, StringBuilder command, IntPtr processAttributes,
        IntPtr threadAttributes, bool inherit, uint flags, IntPtr environment, string directory,
        ref StartupInfo startup, out ProcessInfo process);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr VirtualAllocEx(IntPtr process, IntPtr address, UIntPtr size, uint type, uint protect);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool VirtualFreeEx(IntPtr process, IntPtr address, UIntPtr size, uint type);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool WriteProcessMemory(IntPtr process, IntPtr address, byte[] data, UIntPtr size, out UIntPtr written);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    static extern IntPtr GetModuleHandle(string name);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)]
    static extern IntPtr GetProcAddress(IntPtr module, string name);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr CreateRemoteThread(IntPtr process, IntPtr attributes, UIntPtr stackSize,
        IntPtr start, IntPtr parameter, uint flags, out uint threadId);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetExitCodeThread(IntPtr thread, out uint code);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern uint ResumeThread(IntPtr thread);
    [DllImport("kernel32.dll")]
    static extern bool TerminateProcess(IntPtr process, uint code);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr handle);

    static void Check(bool success, string action)
    {
        if (!success) throw new Win32Exception(Marshal.GetLastWin32Error(), action);
    }

    public static int Launch(string executable, string directory, string dll)
    {
        if (IntPtr.Size != 8) throw new InvalidOperationException("Use 64-bit Windows PowerShell.");
        var startup = new StartupInfo();
        startup.cb = Marshal.SizeOf(startup);
        ProcessInfo process;
        Check(CreateProcess(executable, new StringBuilder("\"" + executable + "\""),
            IntPtr.Zero, IntPtr.Zero, false, 4, IntPtr.Zero, directory, ref startup, out process),
            "Could not start Dark Souls Remastered.");
        IntPtr memory = IntPtr.Zero, loaderThread = IntPtr.Zero;
        bool resumed = false;
        try
        {
            byte[] path = Encoding.Unicode.GetBytes(dll + "\0");
            memory = VirtualAllocEx(process.process, IntPtr.Zero, (UIntPtr)path.Length, 0x3000, 4);
            Check(memory != IntPtr.Zero, "Could not allocate the mod path.");
            UIntPtr written;
            Check(WriteProcessMemory(process.process, memory, path, (UIntPtr)path.Length, out written)
                && written.ToUInt64() == (ulong)path.Length, "Could not write the mod path.");
            IntPtr loadLibrary = GetProcAddress(GetModuleHandle("kernel32.dll"), "LoadLibraryW");
            Check(loadLibrary != IntPtr.Zero, "Could not locate LoadLibraryW.");
            uint threadId;
            loaderThread = CreateRemoteThread(process.process, IntPtr.Zero, UIntPtr.Zero,
                loadLibrary, memory, 0, out threadId);
            Check(loaderThread != IntPtr.Zero, "Could not start the mod loader.");
            if (WaitForSingleObject(loaderThread, 30000) != 0)
                throw new InvalidOperationException("Seamless Co-op loading failed or timed out; the game was stopped.");
            uint result;
            Check(GetExitCodeThread(loaderThread, out result), "Could not read the mod loader result.");
            // The thread's DWORD result truncates x64 module handles; verify the actual module too.
            bool loaded = false;
            using (var child = System.Diagnostics.Process.GetProcessById((int)process.processId))
                foreach (System.Diagnostics.ProcessModule module in child.Modules)
                    if (string.Equals(module.FileName, dll, StringComparison.OrdinalIgnoreCase)) loaded = true;
            if (!loaded) throw new InvalidOperationException("Seamless Co-op DLL did not load; the game was stopped.");
            VirtualFreeEx(process.process, memory, UIntPtr.Zero, 0x8000);
            memory = IntPtr.Zero;
            Check(ResumeThread(process.thread) != uint.MaxValue, "Could not resume the game.");
            resumed = true;
            return (int)process.processId;
        }
        finally
        {
            if (!resumed) TerminateProcess(process.process, 1);
            if (loaderThread != IntPtr.Zero) CloseHandle(loaderThread);
            CloseHandle(process.thread);
            CloseHandle(process.process);
        }
    }
}
