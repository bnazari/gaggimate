#include "MachinePanel.h"

#include "../comms/GaggiMateClient.h"
#include <cmath>
#include <cstring>
#include <display/core/Controller.h>

extern Controller controller; // sim/main.cpp; same global the device firmware has

MachinePanel *MachinePanel::instance = nullptr;

MachinePanel *MachinePanel::getInstance() {
    if (instance == nullptr)
        instance = new MachinePanel();
    return instance;
}

// ---------------------------------------------------------------------------
// Tiny 5x7 bitmap font ('#' = pixel). Only the glyphs the panel uses.
// ---------------------------------------------------------------------------
namespace {

struct Glyph {
    char ch;
    const char *rows[7];
};

const Glyph FONT[] = {
    {'A', {" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}},
    {'B', {"#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "}},
    {'C', {" ### ", "#   #", "#    ", "#    ", "#    ", "#   #", " ### "}},
    {'D', {"#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### "}},
    {'E', {"#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"}},
    {'F', {"#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    "}},
    {'G', {" ### ", "#   #", "#    ", "# ###", "#   #", "#   #", " ### "}},
    {'H', {"#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}},
    {'I', {" ### ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "}},
    {'L', {"#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"}},
    {'M', {"#   #", "## ##", "# # #", "# # #", "#   #", "#   #", "#   #"}},
    {'N', {"#   #", "##  #", "# # #", "#  ##", "#   #", "#   #", "#   #"}},
    {'O', {" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}},
    {'P', {"#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "}},
    {'R', {"#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"}},
    {'S', {" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "}},
    {'T', {"#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "}},
    {'U', {"#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}},
    {'V', {"#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  "}},
    {'W', {"#   #", "#   #", "#   #", "# # #", "# # #", "## ##", "#   #"}},
    {'Y', {"#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  "}},
    {'0', {" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "}},
    {'1', {"  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "}},
    {'2', {" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"}},
    {'3', {" ### ", "#   #", "    #", "  ## ", "    #", "#   #", " ### "}},
    {'4', {"   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "}},
    {'5', {"#####", "#    ", "#### ", "    #", "    #", "#   #", " ### "}},
    {'6', {" ### ", "#    ", "#    ", "#### ", "#   #", "#   #", " ### "}},
    {'7', {"#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   "}},
    {'8', {" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "}},
    {'9', {" ### ", "#   #", "#   #", " ####", "    #", "    #", " ### "}},
    {'.', {"     ", "     ", "     ", "     ", "     ", "  ## ", "  ## "}},
    {'-', {"     ", "     ", "     ", " ### ", "     ", "     ", "     "}},
};

const Glyph *findGlyph(char c) {
    for (const auto &g : FONT)
        if (g.ch == c)
            return &g;
    return nullptr;
}

void setColor(SDL_Renderer *r, uint32_t rgb, Uint8 a = 0xFF) {
    SDL_SetRenderDrawColor(r, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, a);
}

void fillRect(SDL_Renderer *r, int x, int y, int w, int h, uint32_t rgb, Uint8 a = 0xFF) {
    setColor(r, rgb, a);
    SDL_Rect rc{x, y, w, h};
    SDL_RenderFillRect(r, &rc);
}

void frameRect(SDL_Renderer *r, int x, int y, int w, int h, uint32_t rgb) {
    setColor(r, rgb);
    SDL_Rect rc{x, y, w, h};
    SDL_RenderDrawRect(r, &rc);
}

void fillCircle(SDL_Renderer *r, int cx, int cy, int rad, uint32_t rgb, Uint8 a = 0xFF) {
    setColor(r, rgb, a);
    for (int dy = -rad; dy <= rad; dy++) {
        const int dx = (int)std::sqrt((float)(rad * rad - dy * dy));
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

void drawLine(SDL_Renderer *r, int x1, int y1, int x2, int y2, uint32_t rgb, Uint8 a = 0xFF) {
    setColor(r, rgb, a);
    SDL_RenderDrawLine(r, x1, y1, x2, y2);
}

// scale 1 => 5x7 px per char + 1 px spacing.
void drawText(SDL_Renderer *r, int x, int y, const char *s, uint32_t rgb, int scale = 1) {
    setColor(r, rgb);
    int cx = x;
    for (; *s; s++) {
        if (const Glyph *g = findGlyph(*s)) {
            for (int row = 0; row < 7; row++)
                for (int col = 0; col < 5; col++)
                    if (g->rows[row][col] == '#') {
                        SDL_Rect px{cx + col * scale, y + row * scale, scale, scale};
                        SDL_RenderFillRect(r, &px);
                    }
        }
        cx += 6 * scale; // 5 wide + 1 gap (space falls through as blank)
    }
}

int textWidth(const char *s, int scale = 1) { return (int)strlen(s) * 6 * scale - scale; }

void drawTextCentered(SDL_Renderer *r, int cx, int y, const char *s, uint32_t rgb, int scale = 1) {
    drawText(r, cx - textWidth(s, scale) / 2, y, s, rgb, scale);
}

// --- Layout (panel-local coordinates) --------------------------------------
constexpr uint32_t COL_BG = 0x1b1b20;
constexpr uint32_t COL_BODY = 0x8f9499;    // stainless
constexpr uint32_t COL_BODY_DK = 0x6d7276; // shadowed stainless
constexpr uint32_t COL_BLACK = 0x2a2a2e;
constexpr uint32_t COL_TEXT = 0xd8d8dc;
constexpr uint32_t COL_DIM = 0x77777c;
constexpr uint32_t COL_WATER = 0x64a8e6;
constexpr uint32_t COL_COFFEE = 0x6b3f1d;
constexpr uint32_t COL_STEAM = 0xcfd4d8;
constexpr uint32_t COL_LAMP_ON = 0xffb531; // amber lamps
constexpr uint32_t COL_LAMP_OFF = 0x4a3a20;
constexpr uint32_t COL_PWR_ON = 0xe8e8e0; // white power lamp
constexpr uint32_t COL_RED = 0xd05040;

// Machine visual
constexpr int MACH_X = 40, MACH_Y = 18, MACH_W = 170, MACH_H = 150; // body
constexpr int TRAY_Y = 288;                                        // drip tray top

// Rocker switches (three across)
constexpr int SW_Y = 356, SW_W = 56, SW_H = 64, SW_GAP = 26;
constexpr int SW_X0 = 20;
// Steam valve knob
constexpr int KNOB_CX = 40 + 170 + 45, KNOB_CY = 60, KNOB_R = 26;

int switchX(int i) { return SW_X0 + i * (SW_W + SW_GAP); }

} // namespace

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
bool MachinePanel::isMomentary() const { return controller.getSettings().isMomentaryButtons(); }

void MachinePanel::setSwitch(int index, bool pressed) {
    if (sw[index] == pressed)
        return;
    sw[index] = pressed;
    if (auto *c = GaggiMateClient::simInstance())
        c->simInjectButton((uint8_t)index, pressed);
}

void MachinePanel::toggleSwitch(int index) { setSwitch(index, !sw[index]); }

int MachinePanel::switchAt(int x, int y) const {
    for (int i = 0; i < 3; i++)
        if (x >= switchX(i) && x < switchX(i) + SW_W && y >= SW_Y && y < SW_Y + SW_H)
            return i;
    return -1;
}

// Runs every frame: releases scripted momentary taps and, when the setting is
// flipped mid-session, releases anything left closed (a latched-on rocker has
// no stable ON position once it becomes a momentary switch).
void MachinePanel::serviceMode(uint32_t nowMs) {
    const bool mom = isMomentary();
    if (mom != lastMomentary) {
        lastMomentary = mom;
        for (int i = 0; i < 3; i++) {
            setSwitch(i, false);
            tapRelease[i] = 0;
        }
        mouseHeld = -1;
    }
    if (mom)
        for (int i = 0; i < 3; i++)
            if (tapRelease[i] && nowMs >= tapRelease[i]) {
                tapRelease[i] = 0;
                setSwitch(i, false);
            }
}

void MachinePanel::toggleValve() {
    valveOpen = !valveOpen;
    if (auto *c = GaggiMateClient::simInstance())
        c->simMock().setWandValve(valveOpen);
}

void MachinePanel::onMouseDown(int x, int y) {
    if (const int i = switchAt(x, y); i >= 0) {
        if (isMomentary()) {
            mouseHeld = i;
            setSwitch(i, true);
        } else {
            toggleSwitch(i);
        }
        return;
    }
    const int dx = x - KNOB_CX, dy = y - KNOB_CY;
    if (dx * dx + dy * dy <= (KNOB_R + 6) * (KNOB_R + 6))
        toggleValve();
}

void MachinePanel::onMouseUp() {
    if (mouseHeld >= 0) {
        setSwitch(mouseHeld, false); // spring return
        mouseHeld = -1;
    }
}

namespace {
int keyToSwitch(SDL_Keycode key) {
    switch (key) {
    case SDLK_b:
        return 0;
    case SDLK_s:
        return 1;
    case SDLK_w:
        return 2;
    default:
        return -1;
    }
}
} // namespace

void MachinePanel::onKeyDown(SDL_Keycode key) {
    if (key == SDLK_v) {
        toggleValve();
        return;
    }
    if (const int i = keyToSwitch(key); i >= 0) {
        if (isMomentary())
            setSwitch(i, true); // held while the key is down
        else
            toggleSwitch(i);
    }
}

void MachinePanel::onKeyUp(SDL_Keycode key) {
    if (!isMomentary())
        return;
    if (const int i = keyToSwitch(key); i >= 0)
        setSwitch(i, false);
}

void MachinePanel::tap(SDL_Keycode key, uint32_t holdMs) {
    if (key == SDLK_v) {
        toggleValve();
        return;
    }
    if (const int i = keyToSwitch(key); i >= 0) {
        if (isMomentary()) {
            setSwitch(i, true);
            tapRelease[i] = SDL_GetTicks() + (holdMs ? holdMs : 200);
        } else {
            toggleSwitch(i);
        }
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
void MachinePanel::render(SDL_Renderer *r, int offsetX, uint32_t nowMs) {
    serviceMode(nowMs);
    const bool momentary = isMomentary();

    // Read the mock's physical state (may not exist for the first frames).
    MockController::SimMachineState st{};
    if (auto *c = GaggiMateClient::simInstance())
        st = c->simMock().getSimState();

    const bool brewing = st.brewValveOpen && st.flow > 0.05f;
    const bool hotWater = valveOpen && st.pumpActive && !st.brewValveOpen && st.flow > 0.05f;
    const bool steaming = valveOpen && st.temperature > 100.0f && !hotWater;
    const bool boilerOn = st.targetTemp > 0.5f;
    const bool heating = boilerOn && st.temperature < st.targetTemp - 1.0f;
    const bool ready = boilerOn && !heating;

    const int phase = (int)(nowMs / 120); // animation clock

    // Panel background + separator from the round display.
    fillRect(r, offsetX, 0, WIDTH, 480, COL_BG);
    drawLine(r, offsetX, 0, offsetX, 479, 0x000000);

#define PX(x) (offsetX + (x))

    // --- Machine front view -------------------------------------------------
    // Body
    fillRect(r, PX(MACH_X), MACH_Y, MACH_W, MACH_H, COL_BODY);
    fillRect(r, PX(MACH_X), MACH_Y, MACH_W, 12, COL_BODY_DK); // top edge
    // Boiler glow strip while heating (pulses), steady when ready.
    if (boilerOn) {
        const Uint8 a = heating ? (Uint8)(90 + 80 * std::sin(nowMs / 300.0)) : 60;
        fillRect(r, PX(MACH_X + 8), MACH_Y + 20, MACH_W - 16, 26, COL_RED, a);
    }
    drawTextCentered(r, PX(MACH_X + MACH_W / 2), MACH_Y + 28, "SILVIA", COL_BLACK);

    // Recessed status display on the machine front.
    const int dispX = PX(MACH_X + 16), dispY = MACH_Y + 66, dispW = MACH_W - 32, dispH = 54;
    fillRect(r, dispX - 2, dispY - 2, dispW + 4, dispH + 4, COL_BODY_DK); // bezel
    fillRect(r, dispX, dispY, dispW, dispH, 0x101014);

    // Group head + portafilter
    const int grpCX = PX(MACH_X + MACH_W / 2);
    const int grpY = MACH_Y + MACH_H;
    fillRect(r, grpCX - 34, grpY, 68, 16, COL_BODY_DK);     // group
    fillRect(r, grpCX - 26, grpY + 16, 52, 12, COL_BLACK);  // portafilter body
    fillRect(r, grpCX + 26, grpY + 18, 34, 8, COL_BLACK);   // handle
    fillRect(r, grpCX - 5, grpY + 28, 10, 8, COL_BLACK);    // spout

    // Cup on the drip tray
    const int cupW = 44, cupH = 34;
    const int cupX = grpCX - cupW / 2, cupY = TRAY_Y - cupH;
    fillRect(r, PX(MACH_X - 10), TRAY_Y, MACH_W + 20, 8, COL_BODY_DK); // tray
    setColor(r, COL_TEXT);
    frameRect(r, cupX, cupY, cupW, cupH, COL_TEXT);
    if (brewing) {
        // Falling coffee stream + slowly filling cup.
        for (int i = 0; i < 3; i++) {
            const int yy = grpY + 36 + ((phase * 7 + i * 22) % (cupY - grpY - 36 + 6));
            fillRect(r, grpCX - 1, yy, 3, 10, COL_COFFEE);
        }
        const int fill = 6 + (phase / 4) % (cupH - 10);
        fillRect(r, cupX + 2, cupY + cupH - 2 - fill, cupW - 4, fill, COL_COFFEE);
    }

    // Steam wand: from the body's right edge, angled down-right.
    const int wandX1 = PX(MACH_X + MACH_W - 6), wandY1 = MACH_Y + MACH_H - 30;
    const int wandX2 = wandX1 + 34, wandY2 = wandY1 + 58;
    for (int t = -1; t <= 1; t++)
        drawLine(r, wandX1 + t, wandY1, wandX2 + t, wandY2, COL_BODY_DK);
    fillRect(r, wandX2 - 3, wandY2, 7, 10, COL_BODY_DK); // tip

    if (steaming) {
        // Rising steam puffs from the tip.
        for (int i = 0; i < 4; i++) {
            const int t = (phase * 5 + i * 17) % 70;
            const int px = wandX2 + 2 + (int)(6 * std::sin((phase + i * 9) / 3.0));
            fillCircle(r, px, wandY2 + 10 - t, 4 + t / 14, COL_STEAM, (Uint8)(150 - t * 2));
        }
    }
    if (hotWater) {
        // Water stream falling from the tip.
        for (int i = 0; i < 3; i++) {
            const int yy = wandY2 + 12 + ((phase * 7 + i * 20) % 56);
            fillRect(r, wandX2 + 1, yy, 3, 9, COL_WATER);
        }
    }

    // --- Steam valve knob ---------------------------------------------------
    fillCircle(r, PX(KNOB_CX), KNOB_CY, KNOB_R, COL_BLACK);
    fillCircle(r, PX(KNOB_CX), KNOB_CY, KNOB_R - 4, valveOpen ? 0x3a3a40 : COL_BLACK);
    // Handle bar: vertical = shut, horizontal = open (animation not modeled).
    const double ang = valveOpen ? 0.0 : M_PI / 2.0;
    const int hx = (int)(std::cos(ang) * (KNOB_R - 6)), hy = (int)(std::sin(ang) * (KNOB_R - 6));
    for (int t = -1; t <= 1; t++)
        drawLine(r, PX(KNOB_CX) - hx + t, KNOB_CY - hy, PX(KNOB_CX) + hx + t, KNOB_CY + hy, COL_TEXT);
    drawTextCentered(r, PX(KNOB_CX), KNOB_CY + KNOB_R + 8, "VALVE", COL_DIM);
    drawTextCentered(r, PX(KNOB_CX), KNOB_CY + KNOB_R + 20, valveOpen ? "OPEN" : "SHUT", valveOpen ? COL_TEXT : COL_DIM);

    // --- Status on the machine's front display ------------------------------
    const char *status = "IDLE";
    uint32_t statusCol = COL_DIM;
    if (brewing) {
        status = "BREWING";
        statusCol = COL_LAMP_ON;
    } else if (hotWater) {
        status = "HOT WATER";
        statusCol = COL_WATER;
    } else if (steaming) {
        status = "STEAMING";
        statusCol = COL_STEAM;
    } else if (heating) {
        status = "HEATING";
        statusCol = COL_RED;
    } else if (ready) {
        status = "READY";
        statusCol = 0x62c46a;
    }
    drawTextCentered(r, dispX + dispW / 2, dispY + 10, status, statusCol, 2);

    char buf[32];
    snprintf(buf, sizeof(buf), "%d C  %.1f BAR", (int)(st.temperature + 0.5f), st.pressure);
    drawTextCentered(r, dispX + dispW / 2, dispY + 34, buf, COL_TEXT);

    // --- Switches + lamps ---------------------------------------------------
    static const char *names[3] = {"BREW", "STEAM", "WATER"};
    for (int i = 0; i < 3; i++) {
        const int x = PX(switchX(i)), y = SW_Y;
        // Lamp above the switch: wired to the second pole (constant 5V), so it
        // simply follows the contact state in either mode.
        fillCircle(r, x + SW_W / 2, y - 16, 7, sw[i] ? COL_LAMP_ON : COL_LAMP_OFF);
        if (sw[i])
            fillCircle(r, x + SW_W / 2, y - 16, 11, COL_LAMP_ON, 60); // glow

        fillRect(r, x, y, SW_W, SW_H, COL_BLACK);
        frameRect(r, x, y, SW_W, SW_H, COL_DIM);
        if (momentary) {
            // Spring-return push button: sunken while held, otherwise raised.
            const int inset = sw[i] ? 10 : 6;
            fillRect(r, x + inset, y + inset, SW_W - 2 * inset, SW_H - 2 * inset, sw[i] ? 0x3a3a40 : 0x505058);
            drawTextCentered(r, x + SW_W / 2, y + SW_H / 2 - 3, sw[i] ? "I" : "O", COL_TEXT);
        } else {
            // Latching rocker: top half depressed when ON.
            if (sw[i])
                fillRect(r, x + 4, y + 4, SW_W - 8, SW_H / 2 - 6, 0x505058); // pressed top
            else
                fillRect(r, x + 4, y + SW_H / 2 + 2, SW_W - 8, SW_H / 2 - 6, 0x505058); // pressed bottom
            drawTextCentered(r, x + SW_W / 2, y + SW_H / 2 - 3, sw[i] ? "I" : "O", COL_TEXT);
        }
        drawTextCentered(r, x + SW_W / 2, y + SW_H + 8, names[i], COL_DIM);
    }

    // Power lamp (always on while the sim runs).
    fillCircle(r, PX(WIDTH - 30), SW_Y - 16, 6, COL_PWR_ON);
    drawTextCentered(r, PX(WIDTH - 30), SW_Y - 4, "PWR", COL_DIM);

    // Mode + key hints
    drawTextCentered(r, PX(WIDTH / 2), 446, momentary ? "MOMENTARY" : "LATCHING", 0x55555a);
    drawTextCentered(r, PX(WIDTH / 2), 460, "B S W - SWITCHES   V - VALVE", 0x55555a);

#undef PX
}
