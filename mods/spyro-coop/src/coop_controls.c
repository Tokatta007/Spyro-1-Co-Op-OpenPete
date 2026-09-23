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

/* A PSX pad reports 1 for RELEASED; the game's own decode inverts it.
 *
 * AND THE TWO HALVES SWAP. pad_read hands back the wire's own word, whose
 * low byte is libpad's low button byte (select, start, d-pad) and whose high
 * byte is the face and shoulder buttons: up is 0x0010, square 0x8000. The
 * game composes its own held word the other way round - the same two bytes,
 * high first - so up is 0x1000 and square 0x0080 (PADB_* in coop_players.c,
 * PAD_* in the decompilation's gamepad.h). Everything in this mod speaks the
 * game's layout, so the swap happens here, once, at the source. Without it
 * every extra player's d-pad arrived as face buttons: pressing up flamed. */
#define PAD_BUTTONS_MASK 0xFFFFu

static uint32_t to_game_layout(uint32_t wire) {
    return ((wire << 8) | (wire >> 8)) & 0xFFFFu;
}

static int g_have_pads = -1;      /* -1 not asked yet, 0 too old, 1 usable */
static int g_slot_seen[COOP_MAX_PLAYERS];   /* logged once per slot */

/* Last tick's state of every slot, for the settings panel: it draws on the
   present thread, where pad_read is refused, so it reads this instead. */
static int      g_slot_present[COOP_MAX_PLAYERS];
static uint32_t g_slot_held[COOP_MAX_PLAYERS];

static int pads_available(void) {
    if (g_have_pads < 0) {
#if OPENPETE_MOD_API_VERSION >= 12
        g_have_pads = (g_api->api_version >= 12) ? 1 : 0;
#else
        g_have_pads = 0;    /* this SDK has no pad_read to call */
#endif
        if (!g_have_pads)
            coop_log(OP_MOD_LOG_WARN,
                     "this OpenPete has mod api %u; players 2-4 need api 12 for their own "
                     "controllers and will copy player 1", g_api->api_version);
    }
    return g_have_pads;
}

/* One slot's bytes, in tick context. */
static int read_slot(int slot, CoopPad* out) {
    memset(out, 0, sizeof *out);
    out->stick_x = 0x80;
    out->stick_y = 0x80;
    if (slot < 0 || slot >= COOP_MAX_PLAYERS || !pads_available())
        return 0;

#if OPENPETE_MOD_API_VERSION >= 12
    openpete_mod_pad_t pad;
    pad.struct_size = sizeof pad;
    if (g_api->pad_read(g_self, (uint32_t)slot, &pad) != 0)
        return 0;

    out->present = pad.present != 0;
    out->held    = to_game_layout((~pad.buttons) & PAD_BUTTONS_MASK);
    out->stick_x = pad.axes[0];
    out->stick_y = pad.axes[1];
#endif
    return out->present;
}

/* Every slot, once per tick, so the panel has something to show. Says once
   which slots have a device: the engine says which are empty, and between
   the two the log accounts for every slot. */
void coop_controls_scan(void) {
    static int said[COOP_MAX_PLAYERS];
    for (int slot = 0; slot < COOP_MAX_PLAYERS; slot++) {
        CoopPad pad;
        read_slot(slot, &pad);
        g_slot_present[slot] = pad.present;
        g_slot_held[slot]    = pad.held;
        if (pad.present && !said[slot]) {
            said[slot] = 1;
            coop_log(OP_MOD_LOG_INFO, "controls: pad slot %d has a device%s", slot,
                     slot == 0 ? " (player 1's own slot)" : "");
        }
    }
}

int      coop_controls_slot_present(int slot) {
    return (slot >= 0 && slot < COOP_MAX_PLAYERS) ? g_slot_present[slot] : 0;
}
uint32_t coop_controls_slot_held(int slot) {
    return (slot >= 0 && slot < COOP_MAX_PLAYERS) ? g_slot_held[slot] : 0u;
}

/* One player's controller, in tick context. Player is 0-based: player 2 is 1,
   and reads whichever slot the settings put him on, slot 1 by default.
   Returns 0 and an empty port when unavailable. */
int coop_controls_pad(int player, CoopPad* out) {
    memset(out, 0, sizeof *out);
    out->stick_x = 0x80;
    out->stick_y = 0x80;
    if (player <= 0 || player >= COOP_MAX_PLAYERS)
        return 0;

    int slot = g_settings.pad_slot[player];
    read_slot(slot, out);

    if (out->present && !g_slot_seen[player]) {
        g_slot_seen[player] = 1;
        coop_log(OP_MOD_LOG_INFO, "controls: player %d has a controller on pad slot %d",
                 player + 1, slot);
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
    char line[160];
    int o = 0;
    for (int p = 1; p < COOP_MAX_PLAYERS; p++)
        o += snprintf(line + o, sizeof line - o, "  P%d slot %d: %s", p + 1,
                      g_settings.pad_slot[p],
                      coop_controls_slot_present(g_settings.pad_slot[p]) ? "connected" : "none");
    coop_status("Controllers (player 1 plays on the game's own controls)%s", line);
}
