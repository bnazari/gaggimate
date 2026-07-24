# Worklog

Reverse-chronological log of all work on this fork. Every working session gets an entry:
what changed, why, decisions (with rejected alternatives), verification results, and
pending items. Maintained by Claude Code — see CLAUDE.md.

---

## 2026-07-24 — First hardware: display arrived, flashed and verified

**Context:** Display unit arrived today (controller board still pending). First time any
fork firmware runs on real hardware.

**Done:**
- Built display firmware (`pio run -e display`, clean, ~51 s) and flashed over USB
  (`/dev/cu.usbmodem2101`). Web assets in `src/display/webassets/` were current
  (built Jul 22, nothing in `web/` newer), so no `build_webui.sh` rerun needed.
- Verified boot via serial capture: display initializes, Rancilio boot logo shows,
  UI lands on "waiting for connection" (BLE scan for the absent controller — expected).
- **Hardware finding:** the unit is NOT a LilyGo T-RGB — firmware autodetected a
  **466×466 round AMOLED panel with CST9217 touch** (Waveshare/LilyGo AMOLED variant,
  chip type 0x9217, ProjectID 0x5734, `AmoledDisplayDriver`). Autodetect handled it;
  no config change needed. CLAUDE.md's "board LilyGo-T-RGB" note is about the build
  env default, but actual hardware differs — worth remembering for driver-related work.
- First-boot state confirmed: fresh NVS (all `NOT_FOUND` reads, expected), AP mode
  up — SSID `GaggiMate`, generated password `vHpSWjvjQo`, web UI at `http://4.4.4.1/`
  with captive portal. NetworkWatchdog healthy (heap ~96 KB free, egress ok).
- Benign boot errors noted: SD card init fail 0x107 (no card present) and missing
  `/littlefs/h/recent.bin` (no shot history yet).

**Serial-capture gotcha (macOS, native USB CDC):** pulsing RTS resets the ESP32-S3,
which drops the USB port mid-read (`Errno 6 Device not configured`); the port
re-enumerates a few seconds later. To capture a boot log: open port, pulse
DTR=0/RTS=1→0, then reopen after re-enumeration and keep reading.

**Pending:**
- Controller board still not arrived — BLE pairing, buttons/LEDs, and brew flow
  untestable on hardware until then.
- No code changes this session; this worklog entry is the only commit. Push needed
  (hash below once committed).

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
