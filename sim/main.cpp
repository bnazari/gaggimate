// Desktop simulator entry point. Runs the real display Controller on a single
// cooperative loop on the main thread (LVGL/SDL must stay on the main thread on
// macOS), driving the firmware's loop methods directly since the FreeRTOS tasks
// are no-ops in the simulator.
#include "ESPAsyncWebServer.h"
#include "SdlDriver.h"
#include "machine/MachinePanel.h"
#include <Arduino.h>
#include <cstdlib>
#include <cstring>
#include <display/core/Controller.h>
#include <display/plugins/ShotHistoryPlugin.h>
#include <display/ui/default/DefaultUI.h>

// The generated UI event handlers reference this global (see main.h on device).
Controller controller;

// Scripted panel input for headless runs: `--script b@1500,s@3000,v@4000`
// feeds the machine panel the given key at the given ms after start.
struct ScriptEvent {
    SDL_Keycode key;
    unsigned long atMs;
    unsigned long holdMs = 0; // 0 = default tap
    bool fired = false;
};
static ScriptEvent s_script[32];
static int s_scriptLen = 0;

static void parseScript(const char *arg) {
    const char *p = arg;
    while (*p && s_scriptLen < 32) {
        const char key = *p++;
        if (*p == '@') {
            p++;
            const unsigned long at = strtoul(p, (char **)&p, 10);
            unsigned long hold = 0;
            if (*p == ':') {
                p++;
                hold = strtoul(p, (char **)&p, 10);
            }
            s_script[s_scriptLen++] = {(SDL_Keycode)key, at, hold};
        }
        if (*p == ',')
            p++;
    }
}

int main(int argc, char **argv) {
    // Optional: `--screenshot <path> [delayMs]` renders for a bit, saves a BMP, exits.
    const char *shotPath = nullptr;
    unsigned long shotDelayMs = 4000;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            shotPath = argv[++i];
            if (i + 1 < argc)
                shotDelayMs = strtoul(argv[i + 1], nullptr, 10);
        } else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            parseScript(argv[++i]);
        }
    }

    controller.setup(); // builds the UI, installs the SDL driver, marks screen ready

    // The sim has a real network (the WebUI is reachable), so present as Wi-Fi
    // connected: seeding credentials sends setupWifi() down the STA path, and the
    // WiFi shim's begin() reports WL_CONNECTED. This makes the standby screen show
    // the clock and Wi-Fi icon (and avoids captive-portal AP mode). Seed only once.
    Settings &settings = controller.getSettings();
    if (settings.getWifiSsid().isEmpty())
        settings.setWifiSsid("GaggiMate-Sim");
    if (settings.getWifiPassword().isEmpty())
        settings.setWifiPassword("simulator");

    SdlDriver *drv = SdlDriver::getInstance();
    DefaultUI *ui = controller.getUI();
    const unsigned long start = millis();
    bool shotTaken = false;

    while (!drv->shouldQuit()) {
        controller.loop();      // connection lifecycle, comms pump, plugins
        controller.loopLogic(); // process + control logic (normally a FreeRTOS task)

        // Shot history sampling normally runs in its own FreeRTOS task (a no-op in
        // the sim), so drive record() here at its native cadence.
        {
            static unsigned long lastShotSample = 0;
            if (millis() - lastShotSample >= SHOT_LOG_SAMPLE_INTERVAL_MS) {
                lastShotSample = millis();
                ShotHistory.record();
            }
        }

        if (ui) {
            ui->loop();
            ui->loopProfiles();
        }
        gm_web_pump(); // service the embedded WebUI HTTP/WS server

        // Settings persistence normally runs in a deferred save task (a no-op in the
        // sim), so flush dirty settings to NVS periodically. save(true) is a cheap
        // no-op when nothing changed.
        {
            static unsigned long lastSave = 0;
            if (millis() - lastSave >= 2000) {
                lastSave = millis();
                controller.getSettings().save(true);
            }
        }

        for (int i = 0; i < s_scriptLen; i++) {
            if (!s_script[i].fired && millis() - start >= s_script[i].atMs) {
                s_script[i].fired = true;
                MachinePanel::getInstance()->tap(s_script[i].key, s_script[i].holdMs);
            }
        }

        drv->pumpAndRender();

        if (shotPath && !shotTaken && millis() - start >= shotDelayMs) {
            drv->screenshot(shotPath);
            shotTaken = true;
            break;
        }
        delay(5);
    }
    return 0;
}
