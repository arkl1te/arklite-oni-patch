---
name: summarize-changes
description: Summarize all uncommitted (unstaged) changes in the working tree, skipping the .claude directory. Use when asked to summarize, review or describe the current unstaged changes / diff / working tree, e.g. "summarize all unstaged changes", "what changed?", "what about now?" after a previous summary.
---

# Summarize unstaged changes

Always re-read the repository state; never reuse an earlier summary from the conversation,
since the working tree may have changed (or been committed) since.

## 1. Gather the state

Run in one call:

```bash
git status --short && git log --oneline -3 && git diff --stat -- . ':!.claude'
```

- If changes summarized earlier in the conversation now appear in a commit, say so in one line
  (give the commit hash).
- Untracked files outside `.claude/` count as changes: read them and include them.

## 2. Read the full diff

```bash
git diff -- . ':!.claude'
```

If the output is too large and gets persisted to a file, Read that file in full. Don't
summarize from the preview alone.

## 3. Write the summary

Write it as text worth of a commit, in one code block, with exactly these three lists in this
order. No title line goes above a list: each one ends with its `=== End of ... ===` line, followed
by a blank line between lists:

```
Make F2 list every character, mods' included, as portraits.
Make force fields and phase cloaks respawn after 60 seconds.
Ship server.bat and server.sh in the zip; the installer puts them in AE.
Bump the version to 0.5.0.
=== End of Changes ===

Add dedicated servers: no window or sound, with an admin console.
Add server console commands: say, players, kick, ban, start, quit.
Add photo mode on F10 in single player, cutscenes included.
=== End of New ===

Fix sounds looping forever after a rollback.
Fix F10 swallowing the next key.
=== End of Fixes ===
```

- Keep all three end lines, even for a list with no items.
- **Short lines: about 60-80 characters, never much more.** Brief, concise, straight. When an
  item needs more, split it into several items (one per aspect: the feature, its options, its
  UI...) instead of packing lists of options, examples, parentheses or env vars into one line.
  Details belong in the docs, not here.
- **Changes**: existing behavior that now works differently, plus refactors, docs and the version.
- **New**: features, options, shortcuts and tools that didn't exist before.
- **Fixes**: anything that was broken and now isn't, whichever verb describes it best.
- Sort each list from major to minor: what users will notice most goes first; internals, docs and
  the version go last.
- One line per item, ending in a period.
- Every item must begin with a verb in the imperative: Fix, Add, Make, Show, Separate, Clarify...
- Describe user-visible behavior (menus, shortcuts, buttons, OSD messages, saved files) in plain
  terms. Name a proc, file or setting only when there is no better way to say it.
- Split unrelated changes into separate items, and leave out steps of a single change.
- Don't list patch/diff noise (whitespace, reordering) unless that's all there is.
- Don't include `.claude/` in the summary.
- After the code block, flag anything that shouldn't go into the commit unnoticed, such as leftover
  debug code or untracked personal notes. Leave these out of the list itself.
- Number those flags (1., 2., 3. ...), one per flag, so the user can answer by number. Only list
  things that need a decision; leave out "nothing found" notes, or they take up a number.
