# Worklog

Reverse-chronological log of all work on this fork. Every working session gets an entry:
what changed, why, decisions (with rejected alternatives), verification results, and
pending items. Maintained by Claude Code — see CLAUDE.md.

---

## 2026-07-23 — Migration from claude.ai Project to Claude Code

**Context:** All prior fork development happened in a standard claude.ai Project (patch-based
workflow, no custom instructions there — context lived in conversations + code). Switched to
Claude Code today. No hardware on hand yet — all behavior verification is sim-only until the
GaggiMate Pro Rev 1.1 board arrives.

**Done:**
- Created `CLAUDE.md`: build/sim/test commands, architecture map, hardware pinouts
  (Pro Rev 1.1 main board + J6 expansion header, cross-verified against
  `ControllerConfig.h` and user-provided schematics), and a full "Fork changes vs
  upstream" catalog derived from `git diff upstream/master...master`.
- Ran the diff-vs-upstream analysis (15 commits ahead, fork point upstream PR #827).
  Key finding: sim/autotune-tests/nanopb/embedded-WebUI are **upstream** features
  (GM-xxx tags = upstream's Linear); the fork's true delta is the Silvia front-panel
  integration (water rocker on ext4, mode LEDs on ext1–3 + `detectAddon()` removal,
  momentary-button/2s-hold-flush logic, sim MachinePanel, Rancilio theming).
- Noted merge risk for future upstream syncs: `detectAddon()` removal and LED
  channels 8–10 convention in `GaggiMateController.cpp`.
- Deleted stale `machine-panel.patch` (backup of an older sim panel iteration;
  real code is committed under `sim/machine/`).
- Verified sim end-to-end: `pio run -e display-sim` builds clean; screenshot mode
  renders the Rancilio standby screen + MachinePanel correctly; interactive run OK.

**Decisions:**
- Note-taking convention established (user request: "OCD note taking on everything"):
  this WORKLOG.md at repo root, entries per session, committed together with the
  changes they describe; durable facts promoted to CLAUDE.md.
- Git workflow: Claude commits locally only; user pushes via GitHub Desktop
  (no CLI credentials on this machine — intentional).
- Commit-title caveat recorded: fork commit titles pre-migration are unreliable
  (e.g. "Create Rancilio specific profile" actually introduced momentary long-press
  flush). Trust diffs, not titles.

**Pending:**
- [ ] Push `21a23bfd` (CLAUDE.md + patch deletion) via GitHub Desktop.
- [ ] Push the WORKLOG.md commit that follows.
