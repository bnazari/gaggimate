# Worklog

Reverse-chronological log of all work on this fork. Every working session gets an entry:
what changed, why, decisions (with rejected alternatives), verification results, and
pending items. Maintained by Claude Code — see CLAUDE.md.

---

## 2026-07-24 (later) — Mock controller on spare XIAO ESP32-C3

**Context:** Real controller board still not arrived; user has a spare Seeed XIAO
ESP32-C3. Built a mock controller firmware so the display can be bench-tested
end-to-end over real BLE.

**Done:**
- New `src/controller-mock/main.cpp` (this fork) + `[env:controller-mock]` in
  platformio.ini (board `seeed_xiao_esp32c3`): glues the real protocol stack
  (`GaggiMateServer` from lib/NanoPbComm, advertising as `GPBLS`, capabilities
  dimming+pressure to mirror Pro Rev 1.1) to the desktop sim's thermal/hydraulic
  model — `sim/comms/MockController.cpp` compiled in directly via
  `build_src_filter` (`+<../sim/comms/MockController.cpp>` + `-I sim/comms`),
  no code duplication. Hardware string honestly says "GaggiMate Mock (XIAO
  ESP32-C3)" — display only gates behavior on capabilities/protocol version,
  not the name.
- Serial console stands in for the Silvia front panel: `b`/`s`/`w` tap
  brew/steam/water, `B`/`S`/`W` toggle a held press (tests the 2 s long-press
  flush), `v` wand valve, `t` state dump, `h` help.
- **Protocol gotcha handled:** the endpoint's `CoalescingPriorityQueue` coalesces
  button messages by (tag, index), so a press+release sent back-to-back would
  collapse to just the release. Taps therefore space press→release by 120 ms
  (> the 15 ms send-pump interval).
- Compatibility notes: `GaggiMateServer` pins its pump task to core 0 (fine on
  single-core C3); `ble_ota_dfu` pins its install task to core **1**, which
  would fail on the C3 — but only on the OTA-install path, which the mock never
  exercises. Don't try BLE OTA against the mock.
- Build clean (587 KB flash / 44.8%, 28.8 KB RAM), flashed to the XIAO.
- **Verified end-to-end: display connected over BLE and left "waiting for
  connection" (user-confirmed on the physical display).**

**C3 serial-monitor trap (important):** the ESP32-C3's USB-Serial-JTAG drops
into the ROM bootloader (`boot:0x0 USB_BOOT`, "wait usb download") when the
host toggles DTR/RTS on port open/close — a plain pyserial open/close bricked
the session twice until reset via
`esptool.py --chip esp32c3 --port ... --after hard_reset chip_id`.
Fixed for interactive use with `monitor_dtr = 0` / `monitor_rts = 0` in the
env; use `pio device monitor -e controller-mock` to get the button console.

**Decisions:**
- Reused sim's MockController rather than porting the real GaggiMateController
  lib to the C3: the real lib drives physical peripherals (MAX31855, pumps) and
  would run headless into sensor faults; the sim model is the intended
  "machine-in-software" and keeps one source of truth for mock physics.
- Autotune request answered immediately with canned PID values (25/0.6/120/0)
  so the display UI flow completes instead of hanging.

**Pending:**
- Push needed (commit hash recorded on commit).
- Nice-to-have: physical buttons/LEDs on the XIAO's free GPIOs instead of the
  serial console, if bench-testing the momentary logic gets tedious.

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
