---
name: screenshots
description: Document with screenshots what a change adds to the screen, in docs/showcase.md and in the feature's plan. Use for every change that is visible in the session (a new feature, a new icon, a fixed look) before committing it, and when asked for pictures of the project.
---

# Documenting with screenshots

This project is shown at a retro computing event, so what it adds must be seen, not only described. **Every change that is
visible in the session gets a picture in the commit that makes it**, and so does every bug fix that changes the look. A change
with nothing to see (a speed-up, a leak) gets its numbers instead.

## Where the pictures go

- `docs/images/showcase/TOPIC-WHAT.png`, e.g. `linux-terminal-menu.png`. The pre-commit check accepts PNGs only under
  `docs/images/`.
- A section in [docs/showcase.md](../../../docs/showcase.md), newest first: a sentence on what it does and how to get there,
  the picture(s) with `alt` text that says what is in them, and a line in italics with where it was taken, the date and the repo
  commit (`*EeePC, 2026-12-01 (repo abc1234).*`). Remove the feature from "Not pictured yet" when it gets its picture.
- The same picture in the feature's plan (`plans/*.md`) and, for something important, the README. Link the file, do not
  copy it.
- A picture of a bug (before and after) goes into BUGS.md's entry only if the user wants it. BUGS.md is not committed.

## Taking them

| Where | How |
|---|---|
| bench (this machine) | `tools/bench/bench.sh shot NAME OUT.png [WxH+X+Y]` (`bench` skill) |
| EeePC | `tools/eeepc/eee.sh shot OUT.png [WxH+X+Y]` (`eeepc` skill). Ask before driving its session to set up the scene |

Both stack each window's own image, so the taskbar, menus and the Start menu show (`xwd -root` misses them). Both write a
stripped PNG with `magick`.

- **Prefer the EeePC**: it is the machine people will see. Take bench pictures while building, and replace them with EeePC
  ones once the feature is deployed. Say which it is in the caption.
- **Set the scene with care.** Show the feature in use, with real-looking content (a folder with files, a terminal with output),
  and close what does not belong. No personal data: no real names beyond the user's account, no mail, no private network names
  in the picture.
- **Crop to what matters** (a menu, an icon, a dialog) with the crop argument, and keep a whole-screen picture for features
  that are about the whole desktop. Find the coordinates in a whole-screen shot first.
- **Read every picture** (the Read tool shows it) before it is committed: is it the right moment, is the menu open, nothing
  cut off?
- **Size**: aim for under 300 KB a picture. Most XP screens come out at 40 to 120 KB as plain PNG. Large areas of the Bliss
  wallpaper cost the most: crop them away, or set a plain background for the shot.
- Show them at their size or less in Markdown: `<img src="..." alt="..." width="800" />` for whole screens (1024x600), the
  crop's own width for crops.

## Before committing

1. The picture(s) in `docs/images/showcase/`, the section in `docs/showcase.md`, links from the plan.
2. `git add` the PNGs with the change. The pre-commit hook (`git config core.hooksPath .githooks`) checks them.
3. Mention the picture in the commit body ("Pictured in docs/showcase.md").
