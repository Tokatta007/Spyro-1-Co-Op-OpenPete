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

static void on_pad_vsync(CPUState* cpu) {
    g_stats.padvsync_calls++;
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
