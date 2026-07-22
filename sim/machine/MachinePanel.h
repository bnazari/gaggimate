// Simulated "machine" side panel for the desktop simulator: the Silvia's three
// latching rockers (brew / water / steam, with their position-driven lamps), the
// manual steam/hot-water wand valve, and an animated front view that shows what
// the machine is physically doing (brewing into the cup, steaming, dispensing
// hot water, heating).
//
// Rocker edges are injected into the display firmware through
// GaggiMateClient::simInjectButton() using the same button indices the real
// controller board sends (0 = brew, 1 = steam, 2 = water). The wand valve is
// purely physical on a Silvia — the firmware never sees it — so it only feeds
// the MockController's hydraulic model.
#pragma once

#include <SDL.h>
#include <cstdint>

class MachinePanel {
  public:
    static constexpr int WIDTH = 300; // panel width, appended right of the 480px display

    static MachinePanel *getInstance();

    // Mouse handling; x/y are panel-local (0..WIDTH-1, 0..479).
    void onMouseDown(int x, int y);
    void onMouseUp(); // releases a held momentary switch wherever the cursor is

    // Keyboard shortcuts (window-global): b/w/s operate rockers, v toggles valve.
    void onKeyDown(SDL_Keycode key);
    void onKeyUp(SDL_Keycode key);

    // Scripted input (--script): one tap. Latching: toggle. Momentary: press,
    // auto-released ~200ms later by render().
    void tap(SDL_Keycode key);

    // Draw the panel; offsetX is where the panel starts inside the window.
    void render(SDL_Renderer *r, int offsetX, uint32_t nowMs);

  private:
    MachinePanel() = default;
    static MachinePanel *instance;

    // The panel mirrors the firmware's momentaryButtons setting: latching
    // rockers toggle per click, momentary switches follow press/release and
    // spring back — the same physical difference as the spring mod.
    bool isMomentary() const;
    void setSwitch(int index, bool pressed); // inject edge if state changes
    void toggleSwitch(int index);            // 0 brew, 1 steam, 2 water
    void toggleValve();
    int switchAt(int x, int y) const; // hit test, -1 = none
    void serviceMode(uint32_t nowMs); // mode-change cleanup + scripted releases

    bool sw[3] = {false, false, false}; // current electrical state
    bool valveOpen = false;             // manual wand valve
    int mouseHeld = -1;                 // switch index held by mouse (momentary)
    uint32_t tapRelease[3] = {0, 0, 0}; // pending scripted release times
    bool lastMomentary = false;
};
