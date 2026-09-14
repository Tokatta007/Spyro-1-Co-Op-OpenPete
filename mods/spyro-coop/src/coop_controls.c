/**
 * @file coop_controls.c
 * @brief Where players 2 to 4 get their buttons from (2026-09-13).
 *
 * WHY IMGUI. Spyro 1 reads one controller and OpenPete never fills the second
 * pad buffer (coop_pad.c), so the mod reads the extra players' controller
 * from the host itself. [[binding]] rows with gamepad names looked like the
 * way, and failed twice in the user's tests (v0.11.0, v0.11.1): the engine
 * takes at most 8 rows per mod, too few for a controller, and rows naming
 * "pad:south" through "pad4:south" never once read as held with a DualSense
 * plugged in. The engine's overlay is Dear ImGui, though, and a mod UI
 * section flagged OPENPETE_MOD_UI_ALWAYS runs on every present, where
 * ImGui_IsKeyDown(ImGuiKey_GamepadFaceDown) and friends are legal. That read
 * every button, the d-pad and the stick in the v0.11.1 test.
 *
 * ImGui backends feed gamepads only with NavEnableGamepad set, so it is set
 * here. The present pass samples into a host value; the tick reads it.
 *
 * Host input read in a tick diverges a replay, which the SDK allows and warns
 * about, as with the view key.
 */

#include "coop.h"
#include <openpete_mod_ui.h>
#include <openpete_imgui.h>

OPENPETE_MOD_IMGUI()

#define PADB_L2       0x0001u
#define PADB_R2       0x0002u
#define PADB_L1       0x0004u
#define PADB_R1       0x0008u
#define PADB_TRIANGLE 0x0010u
#define PADB_CIRCLE   0x0020u
#define PADB_CROSS    0x0040u
#define PADB_SQUARE   0x0080u
#define PADB_UP       0x1000u
#define PADB_RIGHT    0x2000u
#define PADB_DOWN     0x4000u
#define PADB_LEFT     0x8000u
#define PADB_DPAD     (PADB_UP | PADB_RIGHT | PADB_DOWN | PADB_LEFT)

/* Written by the present pass, read by the tick. Plain words: a torn read is
   one frame of a half-updated button set at worst. */
static volatile uint32_t g_imgui_held;
static volatile int      g_imgui_backend_pad;   /* io BackendFlags HasGamepad */
static volatile uint32_t g_imgui_samples;

static const struct { ImGuiKey key; uint32_t bit; } k_imgui_buttons[] = {
    { ImGuiKey_GamepadFaceDown,  PADB_CROSS },
    { ImGuiKey_GamepadFaceRight, PADB_CIRCLE },
    { ImGuiKey_GamepadFaceLeft,  PADB_SQUARE },
    { ImGuiKey_GamepadFaceUp,    PADB_TRIANGLE },
    { ImGuiKey_GamepadL1,        PADB_L1 },
    { ImGuiKey_GamepadR1,        PADB_R1 },
    { ImGuiKey_GamepadL2,        PADB_L2 },
    { ImGuiKey_GamepadR2,        PADB_R2 },
    { ImGuiKey_GamepadDpadUp,    PADB_UP },
    { ImGuiKey_GamepadDpadDown,  PADB_DOWN },
    { ImGuiKey_GamepadDpadLeft,  PADB_LEFT },
    { ImGuiKey_GamepadDpadRight, PADB_RIGHT },
};
static const struct { ImGuiKey key; uint32_t bit; } k_imgui_stick[] = {
    { ImGuiKey_GamepadLStickUp,    PADB_UP },
    { ImGuiKey_GamepadLStickDown,  PADB_DOWN },
    { ImGuiKey_GamepadLStickLeft,  PADB_LEFT },
    { ImGuiKey_GamepadLStickRight, PADB_RIGHT },
};

/* Present thread, every present (settings.c's always section calls it). */
void coop_controls_sample(void) {
    ImGuiIO* io = ImGui_GetIO();
    if (!io)
        return;
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    g_imgui_backend_pad = (io->BackendFlags & ImGuiBackendFlags_HasGamepad) != 0;

    uint32_t held = 0;
    for (unsigned i = 0; i < sizeof k_imgui_buttons / sizeof k_imgui_buttons[0]; i++)
        if (ImGui_IsKeyDown(k_imgui_buttons[i].key))
            held |= k_imgui_buttons[i].bit;
    if (!(held & PADB_DPAD))
        for (unsigned i = 0; i < sizeof k_imgui_stick / sizeof k_imgui_stick[0]; i++)
            if (ImGui_IsKeyDown(k_imgui_stick[i].key))
                held |= k_imgui_stick[i].bit;
    g_imgui_held = held;
    g_imgui_samples++;
}

static uint32_t g_seen_imgui;   /* display and log-once only */

/* The controller's buttons as sampled, for any context. */
uint32_t coop_controls_now(void) {
    return g_imgui_held;
}

/* Tick context: the extra players' buttons this tick. */
uint32_t coop_controls_held(void) {
    uint32_t held = g_imgui_held;
    if (held && !g_seen_imgui)
        coop_log(OP_MOD_LOG_INFO, "controls: controller input seen (buttons 0x%04X)", held);
    if (held)
        g_seen_imgui++;
    return held;
}

void coop_controls_status(void) {
    coop_status("Controller: %s, buttons now 0x%04X, ticks with input %u",
                g_imgui_backend_pad ? "connected" : "none seen",
                (unsigned)g_imgui_held, g_seen_imgui);
}
