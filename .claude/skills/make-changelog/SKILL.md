---
name: make-changelog
description: Turn the commits since the last release into a changelog for the GitHub release page, written for people who play Oni with Arklite and leaving out project technicalities. Use when asked to make, write or translate a changelog, release notes or a release page text, e.g. "make a changelog" or "release notes since v0.1.0".
---

# Make a changelog

Translate commit messages into release notes that players would care to read. This is for the
GitHub release page, so it's written for people who play Oni with Arklite, not for its developers.

## 1. Gather the state

Always read the commits again; never reuse a message from the conversation. Unless told which
commits, cover every commit since the last tag, the last release (all of them when there's no tag):

```bash
tag=$(git describe --tags --abbrev=0 2>/dev/null); echo "since: ${tag:-the first commit}"
git log ${tag:+$tag..HEAD} --reverse --format='%h %ad%n%B' --date=short && cat VERSION
```

Read them all and merge their items into one changelog. When a later commit changes or fixes
something an earlier one added, describe it once, as it ships. Leave out commits with nothing a
player would notice. If `VERSION` is no newer than the tag, say so after the changelog.

If an item is too vague to explain to a player, read that part of the commit with `git show` rather
than guessing what it does. Don't describe anything the commits don't contain.

## 2. Write the changelog

Reply with one line naming the audience, then the changelog in one ```markdown code block, ready
to paste, in this shape:

```markdown
## Arklite v0.4.0

### Highlights

- **Server list:** Join now finds games on the internet, even when the host is behind a provider's shared address.

### New

**Multiplayer**
- F2 in a match picks the character you come back as at your next respawn.

### Improved

- A modded level0 downloads only what differs from yours: 35 MB instead of 197 MB.

### Fixed

- Switching level0 a second time no longer draws every texture and font white.
- Linux: ...
```

- **Title**: `Arklite` and the version in `VERSION`.
- **Highlights**: the few changes most players would upgrade for, each with a bold name. An item
  here can be summarized again in more detail under New, but only when New has more to say.
- **New**: features that didn't exist before, grouped under bold topics (Multiplayer, Custom Game,
  Options, For modders...) when there are enough to group. Leave a section out when it would be
  empty.
- **Improved**: things that already existed and now work better.
- **Fixed**: what was going wrong, told from the player's side. Say what no longer happens or what
  works now. Mark fixes that apply to one system with `Linux:` (Wine) or `Windows:`, and put them
  last.
- Sort each list from major to minor.
- Write plainly for someone who plays Oni and doesn't know how Arklite is built. Say what they can
  now do or what got better, in "you" where it reads naturally. Items don't need to begin with a
  verb here.
- Name the menus, options, keys and chat commands people will actually touch (Options > More...,
  F2, /punch).
- Leave out project technicalities: version bumps, docs, refactors, moved code, network protocol
  numbers, automation or test commands, and internal names such as functions, files, addresses
  and formats (lockstep, OSBD).
- Keep a setting only when players set it themselves, such as `send=0` under `[reports]` in
  `AE/Arklite.ini`, or a file or folder they'd open (`AE/screenshots/`).
- Merge items that mean the same thing to a player.
- Don't publish, tag or create the release. Only write the text.
