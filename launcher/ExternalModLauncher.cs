using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

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
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll")]
    static extern bool FreeLibrary(IntPtr module);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr CreateRemoteThread(IntPtr process, IntPtr attributes, UIntPtr stackSize,
        IntPtr start, IntPtr parameter, uint flags, out uint threadId);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetExitCodeThread(IntPtr thread, out uint code);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetExitCodeProcess(IntPtr process, out uint code);
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

    static uint RemoteCall(IntPtr process, IntPtr function, IntPtr argument)
    {
        uint threadId;
        IntPtr thread = CreateRemoteThread(process, IntPtr.Zero, UIntPtr.Zero,
            function, argument, 0, out threadId);
        Check(thread != IntPtr.Zero, "Could not start the runtime loader.");
        try
        {
            if (WaitForSingleObject(thread, 30000) != 0)
                throw new InvalidOperationException("Runtime loading failed or timed out; the game was stopped.");
            uint code;
            Check(GetExitCodeThread(thread, out code), "Could not read the runtime loader result.");
            return code;
        }
        finally { CloseHandle(thread); }
    }

    static IntPtr LoadMod(ProcessInfo process, string dll)
    {
        byte[] path = Encoding.Unicode.GetBytes(dll + "\0");
        IntPtr memory = VirtualAllocEx(process.process, IntPtr.Zero, (UIntPtr)path.Length, 0x3000, 4);
        Check(memory != IntPtr.Zero, "Could not allocate the mod path.");
        // On failure Launch terminates the child, which releases any outstanding allocation.
        UIntPtr written;
        Check(WriteProcessMemory(process.process, memory, path, (UIntPtr)path.Length, out written)
            && written.ToUInt64() == (ulong)path.Length, "Could not write the mod path.");
        IntPtr loadLibrary = GetProcAddress(GetModuleHandle("kernel32.dll"), "LoadLibraryW");
        Check(loadLibrary != IntPtr.Zero, "Could not locate LoadLibraryW.");
        RemoteCall(process.process, loadLibrary, memory);
        VirtualFreeEx(process.process, memory, UIntPtr.Zero, 0x8000);
        // Thread exit codes truncate x64 handles; use the actual remote module address.
        using (var child = System.Diagnostics.Process.GetProcessById((int)process.processId))
            foreach (System.Diagnostics.ProcessModule module in child.Modules)
                if (string.Equals(module.FileName, dll, StringComparison.OrdinalIgnoreCase))
                    return module.BaseAddress;
        throw new InvalidOperationException("Could not load " + Path.GetFileName(dll) + "; the game was stopped.");
    }

    static void InitializeRuntime(IntPtr process, IntPtr remoteModule, string dll)
    {
        // Map only to resolve the export offset; do not execute the DLL in PowerShell.
        IntPtr localModule = LoadLibraryEx(dll, IntPtr.Zero, 1);
        Check(localModule != IntPtr.Zero, "Could not inspect the Q-Souls runtime.");
        try
        {
            IntPtr entry = GetProcAddress(localModule, "InitializeQSouls");
            Check(entry != IntPtr.Zero, "Q-Souls runtime initialization export is missing.");
            IntPtr remoteEntry = new IntPtr(remoteModule.ToInt64() + entry.ToInt64() - localModule.ToInt64());
            uint code = RemoteCall(process, remoteEntry, IntPtr.Zero);
            if (code != 0) throw new InvalidOperationException("Title music initialization failed (code " + code +
                "). Check .local/title-music.log.");
        }
        finally { FreeLibrary(localModule); }
    }

    public static int Launch(string executable, string directory, string[] dlls)
    {
        if (IntPtr.Size != 8) throw new InvalidOperationException("Use 64-bit Windows PowerShell.");
        var startup = new StartupInfo();
        startup.cb = Marshal.SizeOf(startup);
        ProcessInfo process;
        Check(CreateProcess(executable, new StringBuilder("\"" + executable + "\""),
            IntPtr.Zero, IntPtr.Zero, false, 4, IntPtr.Zero, directory, ref startup, out process),
            "Could not start Dark Souls Remastered.");
        bool success = false;
        try
        {
            foreach (string dll in dlls)
            {
                IntPtr module = LoadMod(process, dll);
                if (string.Equals(Path.GetFileName(dll), "QSoulsRuntime.dll", StringComparison.OrdinalIgnoreCase))
                    InitializeRuntime(process.process, module, dll);
            }
            Check(ResumeThread(process.thread) != uint.MaxValue, "Could not resume the game.");
            if (WaitForSingleObject(process.process, 2000) == 0)
            {
                uint code;
                GetExitCodeProcess(process.process, out code);
                throw new InvalidOperationException("The game exited during startup (code 0x" + code.ToString("X8") + ").");
            }
            success = true;
            return (int)process.processId;
        }
        finally
        {
            if (!success) TerminateProcess(process.process, 1);
            CloseHandle(process.thread);
            CloseHandle(process.process);
        }
    }
}
