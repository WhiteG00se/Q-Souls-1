# Working agreement

- Keep documentation small and short
- Wait for requested answers. Silence is not approval; never auto-resolve unanswered questions.
- Keep the repository, launcher, mod files, and generated files outside the game directory. Never add, change, move, or delete files in the game directory.
- For development, read game components from `D:\Games\Steam\steamapps\common\DARK SOULS REMASTERED`. If this directory or a required component is missing, ask the user for the correct path or to install the game. Do not search for another installation.
- This game path is used during development, but the distributed launcher (on the user's machine) should detect the user's Steam installation through Steam's installation records
- Target the legitimate Steam release of Dark Souls Remastered. Require Steam and preserve normal Steam authentication.
- The mod will be distributed as the whole repository as a GitHub ZIP or repo clone, runnable through `_launch-Q-Souls-1.bat` without requiring a compiler.
- Do not add game files or third-party mods without permission. Keep user-supplied optional mods and extracted game data out of Git.
- Shared SDKs/tools live under D:/AI. Keep temporary files under `D:\AI\Temp\Q-Souls-1`, outside Git.
- Never commit or push unless requested.
