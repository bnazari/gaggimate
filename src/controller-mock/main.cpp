// Mock machine controller (this fork): a Seeed XIAO ESP32-C3 that pretends to
// be the GaggiMate Pro board so the display can be bench-tested without the
// real controller/machine. Runs the real protocol stack (GaggiMateServer over
// NimBLE, advertising as "GPBLS") glued to the simulator's thermal/hydraulic
// model (sim/comms/MockController, compiled in via build_src_filter). A serial
// console stands in for the Silvia front panel.
//
// Console keys:
//   b / s / w   tap brew / steam / water (press + release 120 ms later)
//   B / S / W   toggle a held press (for the 2 s long-press flush)
//   v           toggle the manual steam-wand valve (bleeds boiler heat)
//   t           print machine state
//   h / ?       help
#include "GaggiMateServer.h"
#include "MockController.h"
#include <Arduino.h>

#ifndef BUILD_GIT_VERSION
#define BUILD_GIT_VERSION "dev"
#endif

namespace {
// Longer than the endpoint's 15 ms send pump so a tap's press frame is
// transmitted before the release upserts the same coalescing key.
constexpr uint32_t TAP_RELEASE_MS = 120;

GaggiMateServer server;
MockController mock;

const char *BTN_NAME[3] = {"brew", "steam", "water"};
bool btnHeld[3] = {false, false, false};
uint32_t tapReleaseAt[3] = {0, 0, 0};
bool wandOpen = false;

void sendButton(uint8_t i, bool pressed) {
    btnHeld[i] = pressed;
    if (server.isConnected())
        server.sendButtonState(i, pressed);
    Serial.printf("[btn] %s %s%s\r\n", BTN_NAME[i], pressed ? "pressed" : "released",
                  server.isConnected() ? "" : " (display not connected, dropped)");
}

void tapButton(uint8_t i) {
    if (btnHeld[i]) { // a scheduled tap-release or toggled hold is pending
        sendButton(i, false);
        tapReleaseAt[i] = 0;
        return;
    }
    sendButton(i, true);
    tapReleaseAt[i] = millis() + TAP_RELEASE_MS;
}

void toggleHold(uint8_t i) {
    tapReleaseAt[i] = 0;
    sendButton(i, !btnHeld[i]);
}

const char *ledName(uint8_t v) {
    // Fork LED semantics: 0 = off, 255 = solid, 64 = fast blink, else 1 Hz.
    switch (v) {
    case 0:
        return "off";
    case 255:
        return "solid";
    case 64:
        return "fast-blink";
    default:
        return "blink";
    }
}

void printStatus() {
    const auto s = mock.getSimState();
    Serial.printf("[state] boiler %.1f degC (target %.1f) | %.1f bar %.1f ml/s | pump %s | brew valve %s | wand %s\r\n",
                  s.temperature, s.targetTemp, s.pressure, s.flow, s.pumpActive ? "ON" : "off",
                  s.brewValveOpen ? "OPEN" : "closed", s.wandValveOpen ? "OPEN" : "closed");
    Serial.printf("[state] LEDs: brew=%s steam=%s water=%s | display %s\r\n", ledName(s.led[0]), ledName(s.led[1]),
                  ledName(s.led[2]), server.isConnected() ? "CONNECTED" : "not connected");
}

void printHelp() {
    Serial.println("GaggiMate mock controller (XIAO ESP32-C3) -- advertising as GPBLS");
    Serial.println("  b/s/w = tap brew/steam/water   B/S/W = toggle hold (2s hold = flush)");
    Serial.println("  v = toggle wand valve   t = status   h = help");
}

void handleSerial() {
    while (Serial.available()) {
        switch (Serial.read()) {
        case 'b':
            tapButton(0);
            break;
        case 's':
            tapButton(1);
            break;
        case 'w':
            tapButton(2);
            break;
        case 'B':
            toggleHold(0);
            break;
        case 'S':
            toggleHold(1);
            break;
        case 'W':
            toggleHold(2);
            break;
        case 'v':
            wandOpen = !wandOpen;
            mock.setWandValve(wandOpen);
            Serial.printf("[wand] valve %s\r\n", wandOpen ? "OPEN" : "closed");
            break;
        case 't':
            printStatus();
            break;
        case 'h':
        case '?':
            printHelp();
            break;
        default:
            break;
        }
    }
}
} // namespace

void setup() {
    Serial.begin(115200);

    gm::DeviceCapabilities capabilities = gaggimate_Capabilities_init_zero;
    // Mirror the GaggiMate Pro Rev 1.1: dimming + pressure, no ToF/ledControls
    // (the fork's mode LEDs on channels 8-10 are sent regardless and land in
    // MockController::setLed, same as in the desktop sim).
    capabilities.dimming = true;
    capabilities.pressure = true;
    server.init("GPBLS", "GaggiMate Mock (XIAO ESP32-C3)", BUILD_GIT_VERSION, capabilities);

    server.onBoilerControl([](uint8_t index, BoilerControlMode mode, float setpoint) {
        mock.setBoiler(BoilerCommand{index, mode, setpoint});
        Serial.printf("[cmd] boiler %u mode=%u setpoint=%.1f\r\n", index, static_cast<unsigned>(mode), setpoint);
    });
    server.onPumpControl([](uint8_t index, PumpControlMode mode, float power, float pressure, float flow) {
        mock.setPump(PumpCommand{index, mode, power, pressure, flow});
    });
    server.onRelayControl([](uint8_t index, bool open) {
        mock.setRelay(RelayCommand{index, open});
        Serial.printf("[cmd] relay %u %s\r\n", index, open ? "open" : "closed");
    });
    server.onTare([]() { mock.tareScale(); });
    server.onLedControl([](uint8_t channel, uint8_t brightness) {
        mock.setLed(channel, brightness);
        Serial.printf("[led] ch%u = %u (%s)\r\n", channel, brightness, ledName(brightness));
    });
    server.onPing([]() {});
    // PID / pump-model / pressure-scale settings are accepted and ignored: the
    // mock's thermal model has no PID in the loop.
    server.onAutotune([](uint32_t testTime, uint32_t samples, uint32_t heaterWattage) {
        Serial.printf("[cmd] autotune requested (%us, %u samples, %uW) -- returning canned PID\r\n", testTime, samples,
                      heaterWattage);
        server.sendAutotuneResult(25.0f, 0.6f, 120.0f, 0.0f);
    });

    mock.onSensor = [](float temp, float pressure, float puckFlow, float pumpFlow, float puckResistance, float pumpPower,
                       float heaterPower) {
        if (server.isConnected())
            server.sendSensorData(temp, pressure, puckFlow, pumpFlow, puckResistance, pumpPower, heaterPower);
    };
    mock.onVolumetric = [](float volume) {
        if (server.isConnected())
            server.sendVolumetricMeasurement(volume);
    };
    // No onTof: this board profile doesn't advertise the ToF capability.

    mock.begin();
    printHelp();
}

void loop() {
    mock.update();
    const uint32_t now = millis();
    for (uint8_t i = 0; i < 3; i++) {
        if (tapReleaseAt[i] != 0 && static_cast<int32_t>(now - tapReleaseAt[i]) >= 0) {
            tapReleaseAt[i] = 0;
            sendButton(i, false);
        }
    }
    handleSerial();
    delay(5);
}
