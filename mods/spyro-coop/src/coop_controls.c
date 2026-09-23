/**
 * @file coop_controls.c
 * @brief Where players 2 to 4 get their buttons from.
 *
 * OpenPete 0.4 (mod api 12) reads four controllers and hands a mod every
 * slot: `pad_read(slot)`, slot 0 being player 1's, the one the game itself
 * reads, and slots 1 to 3 host-only, "nothing writes them into guest RAM, so
 * a mod that wants extra players decodes libpad into a pad buffer it
 * allocates itself". That is exactly this mod's shape, so player n reads pad
 * slot n - 1 and coop_players.c builds his pad record from it.
 *
 * The bytes come from the tick's input-log record, so a read reproduces under
 * replay, rewind and runahead, unlike the two routes this replaced:
 *   - [[binding]] rows with gamepad names: at most 8 rows per mod, too few
 *     for a controller, and with a DualSense plugged in they never read as
 *     held (the user's tests, v0.11.0 and v0.11.1);
 *   - ImGui's gamepad keys, sampled in an always-on UI section: worked, but
 *     merged every controller into one set, so players 2 to 4 shared a
 *     controller and had no analog sticks.
 *
 * Before api 12 there is no fourth route, so the extra players fall back to
 * copying player 1 and the log says why.
 */

#include "coop.h"
#include <stdio.h>

/* A PSX pad reports 1 for RELEASED; the game's own decode inverts it. */
#define PAD_BUTTONS_MASK 0xFFFFu

static int g_have_pads = -1;      /* -1 not asked yet, 0 too old, 1 usable */
static int g_slot_seen[COOP_MAX_PLAYERS];   /* display only */

static int pads_available(void) {
    if (g_have_pads < 0) {
        g_have_pads = (g_api->api_version >= 12) ? 1 : 0;
        if (!g_have_pads)
            coop_log(OP_MOD_LOG_WARN,
                     "this OpenPete has mod api %u; players 2-4 need api 12 for their own "
                     "controllers and will copy player 1", g_api->api_version);
    }
    return g_have_pads;
}

/* One player's pad slot, in tick context. Player is 0-based: player 2 is 1,
   and reads slot 1. Returns 0 and an empty port when unavailable. */
int coop_controls_pad(int player, CoopPad* out) {
    memset(out, 0, sizeof *out);
    out->held    = 0;
    out->stick_x = 0x80;
    out->stick_y = 0x80;
    if (player <= 0 || player >= COOP_MAX_PLAYERS || !pads_available())
        return 0;

    openpete_mod_pad_t pad;
    pad.struct_size = sizeof pad;
    if (g_api->pad_read(g_self, (uint32_t)player, &pad) != 0)
        return 0;

    out->present = pad.present != 0;
    out->held    = (~pad.buttons) & PAD_BUTTONS_MASK;
    out->stick_x = pad.axes[0];
    out->stick_y = pad.axes[1];

    if (out->present && !g_slot_seen[player]) {
        g_slot_seen[player] = 1;
        coop_log(OP_MOD_LOG_INFO, "controls: player %d has a controller on pad slot %d",
                 player + 1, player);
    }
    return out->present;
}

/* The buttons alone, for the menu. */
uint32_t coop_controls_player(int player) {
    CoopPad pad;
    coop_controls_pad(player, &pad);
    return pad.held;
}

void coop_controls_status(void) {
    if (!pads_available()) {
        coop_status("Controllers: this OpenPete is too old (mod api %u, needs 12)",
                    g_api->api_version);
        return;
    }
    char line[128];
    int o = 0;
    for (int p = 1; p < COOP_MAX_PLAYERS; p++) {
        CoopPad pad;
        coop_controls_pad(p, &pad);
        o += snprintf(line + o, sizeof line - o, "  P%d slot %d: %s", p + 1, p,
                      pad.present ? "connected" : "none");
    }
    coop_status("Controllers (player 1 plays on the game's own controls)%s", line);
}
