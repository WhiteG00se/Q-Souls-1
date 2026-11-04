@AGENTS.md

All skills live in `.agents/skills` (shared with other agents); if `.claude/skills` is missing in this repo, create it as a directory junction to `.agents/skills` (`cmd /c mklink /J .claude\skills .agents\skills`) and make sure `.claude/skills` is in `.gitignore`
