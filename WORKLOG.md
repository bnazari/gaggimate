# Worklog

Reverse-chronological log of all work on this fork. Every working session gets an entry:
what changed, why, decisions (with rejected alternatives), verification results, and
pending items. Maintained by Claude Code — see CLAUDE.md.

---

## 2026-08-19 (evening) — "Water button not working" root cause: LVGL imgbtn never repaints on programmatic state change

**Symptom (bench):** water button appeared dead — mode switched but the play icon
never changed, so the pump looked like it never started. Worked in the sim.

**Evidence chain (throwaway diagnostic build with UIDBG logging, since reverted):**
1. Live WS telemetry: physical taps DID arrive and DID toggle the pump process
   (process a:1, pump command 100%) — the entire button→BLE→activate() pipeline is
   healthy on hardware.
2. UIDBG logs: `ui_flags.active` propagated, `evalBooleanProperty` returned the right
   value, and `lv_obj_add_state(LV_STATE_CHECKED)` was applied — LVGL *state* was
   correct the whole time.
3. Controlled blink test (remote activate/deactivate, 6 s cycles): state toggled,
   physical icon frozen on play. Touching the on-screen button DID flip the icon
   (user-confirmed) — the discriminating fact.

**Root cause (upstream-relevant, LVGL 8.4):** `lv_imgbtn` refreshes its drawn source
on touch events (PRESSED/RELEASED) and on STYLE_CHANGED, but a programmatic
`lv_obj_add_state(LV_STATE_CHECKED)` emits STYLE_CHANGED only if some style property
differs between states. The EEZ-generated water/grind start buttons have identical
styles in both states (images are widget-internal, not styles), so the state change
never invalidates the widget. The sim's SDL renderer effectively redraws full frames,
masking the bug; the AMOLED panel's partial refresh preserves the stale icon forever.

**Fix (this fork):** `DefaultUI::loop()` invalidates `water_start_button` /
`grind_start_button` whenever their LVGL checked state flips (checked *after*
`ui_tick()` so the state is already applied); tracking members in DefaultUI.h.
Lives in hand-written code so EEZ regeneration can't clobber it. The grind button
had the identical latent bug. Alternative proper fix for later: give the buttons a
distinct CHECKED-state style in EEZ Studio so LVGL sees a style diff.

**Verification:** blink test repeated on the fixed build — icon alternates; physical
water-button taps toggle pump + icon (user-confirmed "working now").

**Also learned along the way (recorded for future bench work):**
- Opening the display's USB serial port resets it (USB-CDC) — every capture reboots
  the display and clears any latched error state; don't let that confound tests.
- The 5-minute free-test capture recorded ~30 s of continuous active pump with
  correct internal state — consistent with the rendering diagnosis.
- Earlier suspicion of ghost water-button events is unproven; all logged button
  events were clean press/release pairs. Keep an eye on it once mains/pump EMI
  exists.

---

## 2026-08-19 (later) — Water-mode fixes: pump toggle "not working" + pump-off on mode exit

**Bench report:** (1) in water mode, second button tap doesn't start/stop the pump
(works in sim); (2) pump should stop when switching out of water mode.

**(1) is configuration, not code:** `momentaryButtons` defaults to false
(Settings.h `Property<bool> momentaryButtons{registry, "mb", false}`) and the real
display has fresh NVS — in latching mode a momentary push button starts the pump on
press and stops it on release ~150 ms later, i.e. "nothing happens". The sim's
sim_data has momentary=true, hence the difference. Remedy: enable web UI → Settings →
General → "Momentary buttons". NOTE: did NOT set it via a partial POST to
/api/settings — WebUIPlugin sets booleans by arg *presence* (homekit, boilerFill,
momentaryButtons, ...), so a partial POST would silently disable the others. Full-form
save from the web UI only.

**(2) fixed in `setMode()` (this fork):** leaving MODE_WATER with an active process
now deactivates first. The brew button and web UI change-mode already deactivated
before switching; the steam button (`handleSteamButton` → `setMode` directly) and the
touch UI did not, leaving the pump running into the new mode. Guard placed in
`setMode()` so every exit path is covered; double-deactivate is harmless (no-op on
null process).

**Verification:** display build clean; flash + bench test pending (water pump on →
press steam button → pump must stop; also re-test water toggle after enabling
momentary buttons).

---

## 2026-08-19 — Mode-LED audit: stale lamp after mode switch (bench report)

**Report (user, hardware bench):** pressing a button switches the mode, but the lamp
keeps blinking on the *previous* mode. Not observed in the sim. First hardware test
was the morning of 8/18 — i.e. overlapping the pre-9a5afe94 window where the
controller bounced BLE every 250 ms.

**Audit (code review + scripted sim repros, before any change):**
- Display logic (`updateModeLeds`, Controller.cpp:762) is sound: recomputes all three
  channels from `getMode()` each control tick, sends the full 3-channel state in one
  message on any change; every mode-switch path (buttons/touch/web) goes through the
  synchronous `setMode()`.
- Empirical: drove the sim over its WS API (`req:change-mode`, same `setMode` path)
  standby→brew→steam and steam→water with `--screenshot` captures at blink-ON phase
  (t=14 s/16 s, even half-second). Both correct: only the active mode's lamp lit,
  matching thermal state (steam slow-blink heating; water solid at 82/80). Repro
  scripts kept in the session scratchpad (raw-WS client, no deps).
- Transport audit: one message carries all 3 channels; CoalescingPrioQueue replaces
  older LED msgs with newer full-state ones (key = tag, index 0); delivery ack'd with
  retries. Controller applies all channels (GaggiMateServer loops them) and drives
  ext pins every 250 ms.

**Findings:**
1. Primary cause of the bench symptom: the pre-fix BLE bounce (see 8/18 entry).
   `Endpoint::handleConnection()` wipes the outbound queue on every (re)connect
   (Endpoint.cpp:264), so each LED update sent during the bounce died; button
   presses got through because the user retries until the mode visibly changes.
2. Structural hole (fixed today): no LED resync after reconnect. `lastLedState` is
   updated optimistically at send time (no delivery confirmation; queue-full upsert
   failures also silent), and on disconnect the display drops to standby →
   computes all-off → queues it while disconnected → the connect-time queue wipe
   destroys exactly that message. Reconnect resends PID/pressure/pump/control state
   but never LEDs. Self-heals only on the next mode/thermal transition.
   Controller-reboot variant: lamps reset to 0 on the controller, display cache
   claims they're lit → lamps stay dark.

**Fix:** in `onConnectionChanged(connected)` reset `lastLedState` to the {1,1,1}
sentinel (same pattern as `controlStateSent = false`), so the first
`updateModeLeds()` tick after every (re)connect resends full lamp state.

**Verification:** display firmware rebuilt + reflashed; bench re-test by user pending
(switch modes via buttons; also power-cycle the controller mid-mode to exercise the
resync).

---

## 2026-08-18 (later still) — WebUI served garbage: stale .incbin blob object (GM-106 embed path)

**Symptom:** display web server up (HTTP 200, /api/status fine — and it showed the
thermocouple installed and reading 26.6 °C) but GET / returned 791 bytes of firmware
.rodata (log format strings), not HTML.

**Root cause:** `web_ui_blob.S` `.incbin`s `web_ui.bin`, but the build system
fingerprints only the .S text and doesn't know the .bin is a dependency. Timeline:
display env built 8/13 16:23 (old bundle) → webassets regenerated 16:28 (sim prep) →
today's display build recompiled the new `web_ui_manifest.h` into WebUIPlugin but
reused the stale 16:23 blob object. Manifest offsets pointed into a different bundle →
arbitrary flash bytes served as pages. Upstream-relevant: anyone rebuilding the web UI
between firmware builds can hit this.

**Fix:** `scripts/embed_webui.py` now stamps a `bundle sha256:` comment into the
generated .S ("(this fork)" comment in the script), so the .S content — and therefore
its fingerprint — changes with every bundle. Also deleted the stale object once by
hand. Regenerated (bundle 77983e29…), rebuilt, reflashed display.

**Verification:** GET / returns real `<!doctype html>` bundle; /api/status live
(ct=26.75 °C, standby). BLE link to controller unaffected.

---

## 2026-08-18 (later) — BLE-bounce bug: ping timeout × thermal runaway; display couldn't connect

**Symptom:** display flashed OK but could never hold a connection to the real board —
"Service not found" / `serviceDiscoveredCB() rc=7` / connect status 531/574, endless
rescan loop. Display briefly showed "Version mismatch, update controller" — that is the
*protocol* screen (DefaultUI.cpp:753), which only a proto<4 device can trigger; the real
board is proto 4, so that pairing was with the July XIAO mock (proto 3) — keep the mock
unpowered on the bench.

**Root cause (controller firmware, upstream-relevant):** bare bench board has no
thermocouple → `thermalRunawayShutdown()` re-fires every 250 ms loop, setting
`errorState = RUNAWAY`. After the 20 s ping timeout, `handlePingTimeout()`'s
disconnect-once-on-transition guard (`errorState != ERROR_CODE_TIMEOUT`) never holds —
RUNAWAY overwrites TIMEOUT between iterations — so `_comms.disconnect()` fired every
250 ms forever, killing every display connection mid-service-discovery. Verified by
45 s serial capture: "Ping timeout detected" spam at 250 ms cadence from t≈29 s.
Real-world impact: a machine with a broken thermocouple could never show the error on
the display. Candidate for upstream (needs CLA).

**Fix:** dedicated `pingTimedOut` flag (GaggiMateController.{h,cpp}, "(this fork)")
tracks the link-drop transition; `handlePing()` clears it. `errorState` writes left
as-is (RUNAWAY/TIMEOUT can still ping-pong in logs while disconnected; harmless).

**Verification (reflashed 9a5afe94, dual 50 s serial captures):**
- controller: "Client connected" at t≈10 s, **zero** ping-timeout lines in 50 s
  (previously 250 ms spam from t≈29 s), link stable throughout;
- display: "Connected, MTU: 256" → `System info: GaggiMate Pro Rev 1.1
  v1.8.1-183-g9a5afe94 (proto=4 local=4 dm=1 ps=1 led=0 tof=0)` — real board,
  protocol match, no mismatch trigger, no disconnects. Thermal-runaway error on
  the display is *correct* until a thermocouple is wired to T+/T−.
- Mock note: display's earlier "Version mismatch, update controller" screen can only
  come from a proto<4 device = the July XIAO mock; keep it unpowered (reflash it from
  this branch before its next use).

---

## 2026-08-18 — Controller board flashed, first boot on real hardware

**Done:** flashed `hw_scale` controller firmware to the real Pro board over USB
(`/dev/cu.usbmodem2101`, `pio run -e controller -t upload`; 694,880 B, hash verified).
Serial boot capture (pyserial with DTR/RTS held false — same C3-style bootloader trap
avoided): normal `SPI_FAST_FLASH_BOOT`, **autodetect ID=4 (445 mV) → "GaggiMate Pro
Rev 1.1"** — matches every pin mapping we've assumed. Installing without load cells
for now, so HardwareScale is expected to abort setup and stay dormant.

**Expected/benign boot errors on the bare board:** `PCA9634` init failure (no
Sunrise/Alba LED board on the peripheral port; panel LEDs 8–10 are driven on ext pins
and don't use it) and a PSRAM-not-found bail (build enables PSRAM, module has none).

**Pending:** tabletop test — buttons/LEDs on J6, display pairing (keep XIAO mock
unpowered), momentary-mode setting. Push needed: this commit.

---

## 2026-08-13 — Merged dragm83's hardware-scales feature; review + build verification

**Context:** User merged `dragm83/features/hw-scales` (new remote, github.com/dragm83/gaggimate)
into new branch `hw_scale` (merge commit 1873c9b5, parents 0f49be02 = our switch_mods tip
and ae94fb0e = dragm83's tip; already pushed to origin). This session: full review of what
came in, whether our fork's features survived, and compile verification.

**What the feature is:** built-in drip-tray scale — two HX711 load-cell amplifiers
bit-banged on the Pro board's "screen header" pins (shared clock GPIO17 = `scaleSclPin`,
data GPIO18/GPIO39 = `scaleSdaPin`/`scaleSda1Pin`; upstream's config already reserved
these names, only `HardwareScale` uses them). Ten commits from dragm83:

- `lib/GaggiMateController/src/peripherals/HardwareScale.{h,cpp}` (new): 100 ms read
  task, outlier rejection + EMA (α 0.70), idle display quantization (0.1 g steps),
  idle-only zero-drift tracking (disabled while pump/valve active via
  `setBrewingActive()`, driven from `GaggiMateController::loop()`), read-failure
  recovery, tare, per-cell calibration. If HX711s aren't ready at boot, setup aborts
  cleanly → `isAvailable()` false → **no addon advertised, feature dormant. Safe on our
  machine, which has no load cells connected.**
- Protocol: `PROTOCOL_VERSION` 3 → 4. New messages `ScaleFactors` (display→controller,
  tag 12) and `ScaleMeasurement` (controller→display, tag 27, PRIO_LOW/unreliable) —
  hardware weight is a channel separate from pump-derived `VolumetricMeasurement`, so
  both coexist. Addon id 8 = HW scale (7 = gearpump). Sim client bumped to 4 too.
- Display: `VolumetricMeasurementSource::HARDWARE` added; source arbitration
  (`getActiveScaleSource()`: preferred source if healthy → fall back hardware → BT;
  grind always BT — hardware scale is under the drip tray, not the grinder). New
  settings `sf1`/`sf2`/`pss` (preferred source, default "hardware"). New event
  `controller:volumetric-measurement:active:change` consumed by DefaultUI/WebUI/
  ShotHistory instead of the bluetooth-specific event. Health = measurement seen in
  last 1.5 s (`HARDWARE_GRACE_PERIOD_MS`).
- Web UI: Settings → Machine gets a "Scales" section (preferred source dropdown always;
  tare + two-point per-cell calibration UI only when controller advertises the addon).
  System tab shows "Installed distribution: Hardware Scales fork" banner and a confirm
  dialog before OTA (OTA source is still official GaggiMate → would replace the fork).
- Release infra (inert for us): `.github/workflows/hardware-scales.yml` triggers only on
  branch `features/hw-scales` or tags `v*-hwscales.*`; `docs/hardware-scales-flasher/`
  esp-web-tools flasher; `auto_firmware_version.py` falls back to
  `v0.0.0-hwscales.N+gSHA` when no tag reachable; upstream `build.yml` skips
  `-hwscales.` tags. `src/firmware_brand.h` defines flavor strings shown in the web UI.

**Merge quality — our fork's delta survived intact** (verified by grep + diff):
waterBtn/button index 2, LED channels 8–10 + `updateModeLedOutputs()`, `detectAddon()`
removal, momentary long-press flush + `checkBrewButtonLongPress()`, `updateModeLeds()`
hysteresis, sim MachinePanel. Manual conflict resolutions were in
`GaggiMateController.{cpp,h}`, `Controller.{cpp,h}`, `sim/comms/GaggiMateClient.h` —
all look correct. dragm83's tare handler composes with ours (hardware scale tares
first, then DimmedPump volumetric tare).

**Review findings (small, none blocking):**
1. `DefaultUI::updateSystemStatus()` lost upstream's `stringChanged()` guards on
   `controller_version`/`display_version` — dragm83 removed them in their branch, and
   the 3-way merge correctly propagated that. Now re-sets two EEZ strings every render
   tick; candidate fixup (string churn was guarded upstream for heap-fragmentation
   reasons).
2. `Controller::currentCoffeeVolume` is now dead — `onVolumetricMeasurement` no longer
   writes it and nothing calls `getCurrentCoffeeVolume()`. Dead code, candidate cleanup.
3. `ShotHistoryPlugin.cpp:92` has a stray-indentation artifact from dragm83 (cosmetic;
   `scripts/format.sh` would fix).
4. Non-nightly behavior change: flow-estimation measurements now early-return in
   `onVolumetricMeasurement` (they only feed shot-history estimation events on nightly
   builds). Intentional in dragm83's design; noting since brew-by-weight without any
   scale still falls back to time-based targets.

**Verification:** `pio run -e controller` and `pio run -e display` both build clean on
the merge. WebUI not rebuilt yet — `src/display/webassets/` is stale; **run
`scripts/build_webui.sh` before flashing the display** or the new Scales settings UI
won't be embedded.

**Sim fix (same day, follow-up commit):** `display-sim` didn't build after the merge —
the merge resolution added `onScaleMeasurement` to the sim's mock BLE client but not
`sendScaleFactors`, which `Controller::setScaleFactors()` now calls. Added it as a
documented no-op in `sim/comms/GaggiMateClient.h` (the mock never advertises addon 8,
so the firmware has no factors to send; a full sim-side hardware scale wasn't worth it).
Rebuilt webassets via `scripts/build_webui.sh` (67 assets, 641,905 B) and launched the
sim: boots clean, `proto=4 local=4`, no protocol mismatch, WebUI on localhost:8080.

**Pending:**
- Push needed: this worklog commit (hash in git log) — branch `hw_scale` itself is
  already on origin.
- Run `scripts/build_webui.sh` + rebuild display before next flash.
- Optional fixups: findings 1–3 above.
- Mock controller (XIAO C3) reports protocol 4 only after reflashing it from this
  branch; an old mock (proto 3) will now trip the display's protocol-mismatch path.

**Hardware status (2026-08-13):** controller board, display, and load sensors arrive
2026-08-14. Plan: modify the Silvia front-panel buttons to momentary-with-LED per the
J6 wiring diagram (CLAUDE.md), tabletop-test the full stack (buttons, LEDs, hardware
scale, calibration) before installing in the machine. 3D-printed parts for the
hardware-scale (drip tray) mount still printing.

**Update (2026-08-18):** installing *without* load cells for now (scale-mount prints
not done). Safe: HardwareScale probes only at boot — no HX711s → addon dormant, no
behavior change vs pre-merge. When cells are added later: wire, then power-cycle the
controller; scale UI appears in web Settings → Machine once addon 8 is advertised.

---

## 2026-07-24 (evening) — Display boot-loop postmortem: semver abort on mock's "dev" version

**Symptom:** After the user configured WiFi via the display's web UI, the display
entered a boot loop. WiFi looked like the culprit (timing), but serial capture
showed WiFi connecting fine (STA + IP) — the abort() fired the moment the display
received the mock controller's system info, every boot.

**Root cause (display firmware, upstream code):** the mock advertised version
`"dev"` (the `BUILD_GIT_VERSION` define never reached the mock build — my
fallback kicked in; `auto_firmware_version.py` writes `src/version.h`, which
`controller-mock/main.cpp` didn't include). On connect, `WebUIPlugin` calls
`GitHubOTA::setControllerVersion("dev")` → `from_string("ev")` in
`lib/OTA/src/semver_extensions.cpp` → `split(...)` yields 1 element →
`numbers.at(1)` throws `std::out_of_range` → `abort()` → reboot → BLE
reconnect → repeat. Any controller reporting a non-semver version boot-loops
the display; **upstream-relevant bug.**

**Fixes (commit below):**
- `semver_extensions.cpp`: guard `numbers.size() < 3` → return 0.0.0 instead of
  throwing. Display now survives any malformed version string.
- `controller-mock/main.cpp`: `#include "version.h"` so the mock reports the
  real `git describe` version (`v1.8.1-166-g…`), fallback changed to the
  semver-shaped `v0.0.0-mock`.

**Verification:** both firmwares rebuilt + reflashed. Display up >98 s, no
abort; `curl http://10.0.1.114/api/status` → `{"mode":0,"tt":0,"ct":21}` —
live sensor stream from the mock (21 °C = MockController ambient). WiFi
(AirCanada) + web UI on LAN confirmed working; the "password caused it" theory
was a timing coincidence.

**Pending:**
- Push needed: 2f90dcca (mock controller) + this commit.
- Consider offering the semver guard upstream (needs CLA via Discord).

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
