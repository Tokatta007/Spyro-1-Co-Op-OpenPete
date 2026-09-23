/**
 * @file coop_pad.c
 * @brief Input measurements: the controller 2 probe, and the poll timing
 *        question phase C depends on.
 *
 * THE PROBE. Spyro 1 registers both pad buffers with PadInitDirect and never
 * reads the second; original hardware fills it anyway, which is what the PS1
 * co-op mod relies on. On OpenPete v0.3.0 it measured as all zeros while the
 * engine's own input log showed slot 2 live and independent. This keeps
 * reporting, so the day a build fixes it, the Mods panel says so.
 *
 * THE POLL TIMING. On PS1, PadVSync ran from the VSync interrupt and could
 * fire while player 2's state was swapped in, polling player 1's input into
 * player 2's slot. Preventing that needed a deferred-poll mechanism, and an
 * earlier attempt with critical sections deadlocked the console. Whether
 * OpenPete can call PadVSync inside the swap window at all is unknown, so
 * this counts every call and every call that lands while swapped. A second
 * count of zero means phase C can drop that whole mechanism.
 */

#include "coop.h"

#define PAD_CONNECTED(p) ((p)[0] == 0u)                         /* status byte */
#define PAD_BUTTONS(p)   ((unsigned)((p)[2] << 8) | (unsigned)(p)[3])  /* active low */

static unsigned g_p2_seen_input;  /* display only */
static int      g_p2_ever;

void coop_pad_sample(void) {
    const uint8_t* p2 = guest8(OP_GADDR_g_PadBufferSecondController);
    if (p2 && PAD_CONNECTED(p2) && PAD_BUTTONS(p2) != 0xFFFFu) {
        g_p2_seen_input++;
        if (!g_p2_ever) {
            g_p2_ever = 1;
            coop_log(OP_MOD_LOG_INFO,
                       "controller 2 buffer is LIVE: buttons=0x%04X",
                       PAD_BUTTONS(p2));
        }
    }
}

void coop_pad_status(void) {
    const uint8_t* p2 = guest8(OP_GADDR_g_PadBufferSecondController);
    coop_status("Controller 2 buffer: %s, input seen %u times",
                     (p2 && PAD_CONNECTED(p2)) ? "connected" : "empty",
                     g_p2_seen_input);
    coop_status("PadVSync calls %u, inside swap window %u",
                     g_stats.padvsync_calls, g_stats.padvsync_in_swap);
}

/* PLAYER 1 AND THE CONTROLLER (2026-09-13, the user's v0.11.1 test). With
   players 2 to 4 on the controller and player 1 on the keyboard, the
   controller's left stick still moved player 1: OpenPete feeds its stick
   into player 1's pad buffer whatever openpete.toml says ("none", or a
   pad slot nothing is plugged into, both tried; reproduced headless with a
   movie's stick). The game only reads sticks from a DualShock, so here, just
   before the game decodes the buffer, player 1's pad reports itself as a
   plain digital pad (HW_TYPE_NON_DUALSHOCK): the stick is ignored and every
   button, keyboard included, works as before.

   That also took the stick away from the menus, the only way the controller
   could move through them. Outside gameplay and the pause menu (dialogue,
   level results) the controller's buttons are added to player 1's, so either
   can answer. The pause menu is player 1's alone, as the user asked
   (v0.11.3); on its Colors page the controller has a cursor of its own
   (coop_menu.c). In gameplay the controller belongs to the dragons. */
#define PADBUF_STATUS  0
#define PADBUF_TYPE    1
#define PADBUF_BUTTONS 2          /* two bytes, big end first, active low */
#define PAD_TYPE_DIGITAL   0x41
#define PAD_TYPE_DUALSHOCK 0x73

/* KEYED TO THE CONTROLS SETTING, NOT THE PLAYER COUNT (v0.11.5). Changing the
   pad's type is not free: when the game sees a digital pad turn back into a
   DualShock it recalibrates, and PadCaliReset clears m_Held, so a button
   still held reads as a fresh press on the next frame. Tied to "players >= 2"
   as it first was, stepping PLAYERS onto 1 in the Multiplayer menu flipped
   the type while the arrow key was down, and the step ran twice: 4 went to 2
   (seen by the user). The setting only changes from the M panel's mouse. */
static void player1_pad_rules(void) {
    if (g_settings.extra_controls != EXTRA_CONTROLS_CONTROLLER)
        return;
    uint8_t* buf = guest8(OP_GADDR_g_PadBuffer);
    if (buf[PADBUF_STATUS] != 0)
        return;                              /* nothing connected */
    if (buf[PADBUF_TYPE] == PAD_TYPE_DUALSHOCK)
        buf[PADBUF_TYPE] = PAD_TYPE_DIGITAL;
    if (coop_enabled() && coop_gamestate() != GS_PLAYING && coop_gamestate() != GS_PAUSED) {
        uint32_t extra = coop_controls_player(1) & 0xFFFFu;   /* player 2's pad */
        uint32_t held  = ~(((uint32_t)buf[PADBUF_BUTTONS] << 8) | buf[PADBUF_BUTTONS + 1]) & 0xFFFFu;
        held |= extra;
        buf[PADBUF_BUTTONS]     = (uint8_t)(~held >> 8);
        buf[PADBUF_BUTTONS + 1] = (uint8_t)~held;
    }
}

static void on_pad_vsync(CPUState* cpu) {
    g_stats.padvsync_calls++;
    player1_pad_rules();
    /* Runs every frame in every gamestate, menus included, so it is where an
       M panel edit is adopted. */
    coop_settings_tick();
    coop_tint_state();                       /* every frame, menus and sequences included */
    if (coop_arena()->swapped) {
        g_stats.padvsync_in_swap++;
        if (g_stats.padvsync_in_swap == 1)
            coop_log(OP_MOD_LOG_WARN,
                       "PadVSync ran inside the player 2 swap window; "
                       "phase C will need the deferred poll");
    }
    g_api->base(cpu);
}

int coop_pad_install(void) {
    if (g_api->override_name(g_self, "PadVSync", on_pad_vsync) != 0) {
        coop_log(OP_MOD_LOG_ERROR, "could not observe PadVSync");
        return 1;
    }
    return 0;
}
