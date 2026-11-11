---
name: temp-cleanup
description: Free temporary files that belong to Arklite (old Claude session scratch folders in /tmp, clang crash reproducers from Arklite builds, memory-map dumps of exited game processes) and report how much was freed and how much is still held. Use when asked to clean up, clear or free temp/tmp space, scratch files or disk/RAM used by temporary files, e.g. "clean /tmp", "free some temp space", "how much temp are you holding?".
---

# Clean Arklite's temporary files

Only this repo's files. Ignore other projects' folders and other apps' leftovers in /tmp
entirely: don't delete them, and don't list them in the report.

## 1. Dry run

Pass the current session's folder (the parent of your scratchpad directory) as `--keep`:

```bash
bash .claude/skills/temp-cleanup/cleanup.sh --keep <scratchpad dir>/..
```

The script never deletes anything a live process has open (working directory, open file,
or running executable). Other running Claude sessions hold their own folders, so those
are reported as held, not deleted.

## 2. Apply

If the dry run lists only what the user asked to clean, re-run the same command with `--apply`
without asking again. Ask first only if it would delete something unexpected.

## 3. Report

Lead with two numbers: **freed** and **holding onto**. Then one short list of what was
freed by kind (session folders, crash reproducers, …) and what is held and why (for example
"in use by N running sessions; clearable after closing them").

Notes:
- /tmp is RAM-backed and wiped on reboot.
- If the script finds a new kind of Arklite leftover in /tmp, add a rule for it to
  `cleanup.sh` rather than deleting it by hand.
