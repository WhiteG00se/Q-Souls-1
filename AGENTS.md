# Working agreement

- Keep documentation small and short
- Wait for requested answers. Silence is not approval; never auto-resolve unanswered questions.
- Keep the repository, launcher, mod files, and generated files outside the game directory. Never add, change, move, or delete files in the game directory.
- For development, read game components from `D:\Games\Steam\steamapps\common\DARK SOULS REMASTERED`. If this directory or a required component is missing, ask the user for the correct path or to install the game. Do not search for another installation.
- This game path is used during development, but the distributed launcher (on the user's machine) should detect the user's Steam installation through Steam's installation records
- Target the legitimate Steam release of Dark Souls Remastered. Require Steam and preserve normal Steam authentication.
- The mod will be distributed as the whole repository as a GitHub ZIP or repo clone, runnable through `_launch-Q-Souls-1.exe` without requiring a compiler.
- Do not add game files or third-party mods without permission. Keep user-supplied optional mods and extracted game data out of Git.
- Shared SDKs/tools live under D:/AI. Keep temporary files under `D:\AI\Temp\Q-Souls-1`, outside Git.
- For uncommitted changes, compare the version in `VERSION.txt` with its value in `HEAD`. If it is not already higher, increment its minor version once.
- Never commit or push unless requested.
- Only the user performs E2E testing. Do not launch or control the game for testing; limit agent checks to builds, static checks, and isolated tests.
- Preserve the user-verified v0.15 RTSS fix: render through the existing pre-Present bridge with explicit D3D11 pipeline state save/restore. Do not use `CreateDeviceContextState`/`SwapDeviceContextState`; they made RTSS detect D3D10 instead of D3D11 and broke Afterburner's OSD.
- Keep F12 working, Respec intact, and the title label independently visible with its original font/placement. Never couple mod UI to RTSS shared-memory OSD visibility or change RTSS/Afterburner settings as a workaround.
- After rendering/input changes, rebuild the runtime and run `tools/Test-Runtime.ps1` plus `tools/Test-RTSS.ps1` with RTSS running (must report D3D11/API 7 and measured frames). These checks do not prove in-game F12 compatibility; request user E2E verification before declaring it confirmed.
