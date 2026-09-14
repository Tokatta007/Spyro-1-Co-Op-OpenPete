/**
 * @file coop_controls.c
 * @brief Where players 2 to 4 get their buttons from (2026-09-13).
 *
 * THE PROBLEM. Spyro 1 reads one controller and OpenPete never fills the
 * second pad buffer (coop_pad.c), so the mod reads the extra players' input
 * from the host itself. Two routes exist, and the first test of v0.11.0 found
 * the obvious one short:
 *
 *   - [[binding]] rows with gamepad names ("pad:south"). The engine accepts
 *     at most 8 rows per mod (the manifest docs), so a full controller does
 *     not fit, and with the DualSense plugged in, the 7 that were accepted
 *     never read as held. Which controller "pad:" means with several devices
 *     connected (the user's log lists a DualSense, an "XInput Controller #1"
 *     and a Razer keyboard that SDL also reports as a pad) is unknown.
 *   - ImGui's own gamepad keys. The engine's overlay is Dear ImGui, and a mod
 *     UI section flagged OPENPETE_MOD_UI_ALWAYS runs on every present, where
 *     ImGui_IsKeyDown(ImGuiKey_GamepadFaceDown) and friends are legal. No row
 *     limit and every button. Whether the engine's backend feeds the gamepad
 *     into ImGui with its overlay closed is what this build finds out; ImGui
 *     backends only do so with NavEnableGamepad set, so it is set here.
 *
 * THIS BUILD MEASURES BOTH. The present pass samples ImGui into a host
 * value; the tick reads it. Four probe bindings put cross on pad:, pad2:,
 * pad3: and pad4:, to learn which slot the DualSense is. The first input seen
 * on each route is logged, and the M panel shows them live. Players 2 to 4
 * follow ImGui's buttons, plus cross from any probe that reads.
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
static volatile int      g_imgui_nav_was_off;   /* we turned NavEnableGamepad on */
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
    if (!(io->ConfigFlags & ImGuiConfigFlags_NavEnableGamepad)) {
        io->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        g_imgui_nav_was_off = 1;
    }
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

static const char* const k_probe_names[4] = {
    "probe_pad1_cross", "probe_pad2_cross", "probe_pad3_cross", "probe_pad4_cross" };

static uint32_t g_seen_imgui, g_seen_probe[4];   /* display and log-once only */
static uint32_t g_probe_now;                     /* bit k: probe k held this tick */

/* Tick context: the extra players' buttons this tick. */
uint32_t coop_controls_held(void) {
    uint32_t held = g_imgui_held;
    g_probe_now = 0;
    for (int k = 0; k < 4; k++)
        if (g_api->binding_down(g_self, k_probe_names[k]))
            g_probe_now |= 1u << k;

    if (held && !g_seen_imgui)
        coop_log(OP_MOD_LOG_INFO, "controls: ImGui gamepad input seen (buttons 0x%04X)", held);
    if (held)
        g_seen_imgui++;
    for (int k = 0; k < 4; k++)
        if (g_probe_now & (1u << k)) {
            if (!g_seen_probe[k])
                coop_log(OP_MOD_LOG_INFO, "controls: binding %s reads held (pad slot %d)",
                         k_probe_names[k], k + 1);
            g_seen_probe[k]++;
        }
    if (g_probe_now)
        held |= PADB_CROSS;
    return held;
}

void coop_controls_status(void) {
    coop_status("Controls via ImGui: backend gamepad %s, gamepad nav %s, %u presents, "
                "buttons now 0x%04X, ticks with input %u",
                g_imgui_backend_pad ? "yes" : "no",
                g_imgui_nav_was_off ? "turned on by the mod" : "already on",
                (unsigned)g_imgui_samples, (unsigned)g_imgui_held, g_seen_imgui);
    coop_status("Controls via bindings (cross): pad1 %u  pad2 %u  pad3 %u  pad4 %u",
                g_seen_probe[0], g_seen_probe[1], g_seen_probe[2], g_seen_probe[3]);
}
