/**
 * @file coop_rumble.c
 * @brief Each player's controller buzzes for his own dragon.
 *
 * The game keeps four vibration globals and decodes them once per VBL
 * (`gamepad.c` in the decompilation): a normal timer, an electric-shock
 * timer, a custom timer with its own amount, and in that order they become
 * the actuator pair {1,120}, {1,0}, {0,amount} or {0,0}; each timer then
 * counts down by one. OpenPete 0.4's `pad_rumble(slot, small, large)` takes
 * exactly that pair for pad slots 1 to 3, with the host's own strength
 * setting applied after, and refuses slot 0 because the game already owns
 * it.
 *
 * The globals are one set for one dragon, so they are read per tick instead.
 * Every dragon's tick is the same game code with his state swapped in: when
 * it wants to buzz, it writes those globals. So around each extra player's
 * tick the mod parks the globals at zero, and whatever the tick leaves is
 * his; player 1's own values go back untouched, and the game drives his pad
 * from them as it always has. A dragon in the live slot after a view swap is
 * treated the same way, which also stops his hits buzzing player 1's pad.
 *
 * The timers then live per player in the party arena, where savestates and
 * rewind carry them, and this file ages and publishes them from the PadVSync
 * hook, the tick context `pad_rumble` requires.
 */

#include "coop.h"

/* The game's vibration globals (decomp names in the PS1 project's
   reference/spyro-1/include/gamepad.h), all ints. */
#define VIB_NORMAL   OP_GADDR_D_80075904   /* hits, landings: {1, 120} */
#define VIB_ELECTRIC OP_GADDR_D_80075764   /* the shock effect: {1, 0} */
#define VIB_CUSTOM   OP_GADDR_D_800757D0   /* a dragon's rescue, and so on */
#define VIB_AMOUNT   OP_GADDR_D_8007584C   /* how hard, for the custom timer */

enum { R_NORMAL, R_ELECTRIC, R_CUSTOM, R_AMOUNT };

static const uint32_t g_vib_addr[4] = { VIB_NORMAL, VIB_ELECTRIC, VIB_CUSTOM, VIB_AMOUNT };

static int32_t g_parked[4];      /* player 1's values, while another dragon ticks */
static int     g_parking;        /* a tick of someone else's is running */
static int     g_buzzing[COOP_MAX_PLAYERS];   /* display only */
static int     g_logged[COOP_MAX_PLAYERS];    /* said so once per player */

/* Older SDK headers have no pad_rumble field at all, so the calls have to go
   at compile time as well as at run time (the mod ships as source). */
#if OPENPETE_MOD_API_VERSION >= 12
static int rumble_available(void) {
    return g_api->api_version >= 12 && g_api->pad_rumble != NULL;
}
static void drive(int player, uint32_t small_byte, uint32_t large_byte) {
    /* His controller, wherever the settings put it (coop_settings.c). */
    g_api->pad_rumble(g_self, (uint32_t)g_settings.pad_slot[player], small_byte, large_byte);
}
#else
static int  rumble_available(void) { return 0; }
static void drive(int player, uint32_t small_byte, uint32_t large_byte) {
    (void)player; (void)small_byte; (void)large_byte;
}
#endif

/* The game's own vibration option, and whether the pad has motors at all. */
static int vibration_on(void) {
    return *guest32(OP_GADDR_g_ActEnabled) != 0 && *guest32(OP_GADDR_g_ActAvailable) != 0;
}

static void read_vib(int32_t out[4]) {
    for (int i = 0; i < 4; i++)
        out[i] = *guest32(g_vib_addr[i]);
}

static void write_vib(const int32_t in[4]) {
    for (int i = 0; i < 4; i++)
        *guest32(g_vib_addr[i]) = in[i];
}

/* Before a player's tick, whichever slot he is in. */
void coop_rumble_tick_begin(int person) {
    if (!rumble_available() || person < 0 || person >= COOP_MAX_PLAYERS)
        return;
    read_vib(g_parked);
    static const int32_t zero[4] = { 0, 0, 0, 0 };
    write_vib(zero);
    g_parking = 1;
}

/* After it: whatever the tick asked for belongs to the player who ticked, and
   the globals go back as they were. A timer already running keeps the longer
   of the two, the way a fresh hit does to the game's own.
 *
 * Player 1 is the exception, in whichever slot he is: his buzz is merged back
 * into the globals, where the game's own decode drives pad slot 0 as it
 * always has. That is also what keeps his pad quiet for other people's hits
 * after a view swap, when his dragon ticks as a shadow. */
void coop_rumble_tick_end(int person) {
    if (!g_parking)
        return;
    g_parking = 0;

    int32_t got[4];
    read_vib(got);
    write_vib(g_parked);

    if (person == 0) {
        for (int i = R_NORMAL; i <= R_CUSTOM; i++)
            if (got[i] > g_parked[i])
                *guest32(g_vib_addr[i]) = got[i];
        if (got[R_CUSTOM] > 0)
            *guest32(VIB_AMOUNT) = got[R_AMOUNT];
        return;
    }

    CoopPartyArena* P = coop_party_arena();
    int32_t* mine = P->rumble[person];
    for (int i = R_NORMAL; i <= R_CUSTOM; i++)
        if (got[i] > mine[i])
            mine[i] = got[i];
    if (got[R_CUSTOM] > 0)
        mine[R_AMOUNT] = got[R_AMOUNT];

    if (!g_logged[person] && (got[R_NORMAL] || got[R_ELECTRIC] || got[R_CUSTOM])) {
        g_logged[person] = 1;
        coop_log(OP_MOD_LOG_INFO,
                 "rumble: player %d's own buzz (normal %d, shock %d, custom %d) goes to "
                 "pad slot %d", person + 1, got[R_NORMAL], got[R_ELECTRIC], got[R_CUSTOM],
                 g_settings.pad_slot[person]);
    }
}

/* Once per VBL, from the PadVSync hook: decode as the game does, publish,
   then age the timers by one. */
void coop_rumble_vbl(void) {
    if (!rumble_available())
        return;

    CoopPartyArena* P = coop_party_arena();
    int on = coop_enabled() && vibration_on();

    for (int p = 1; p < COOP_MAX_PLAYERS; p++) {
        int32_t* t = P->rumble[p];
        if (!on) {
            t[R_NORMAL] = t[R_ELECTRIC] = t[R_CUSTOM] = t[R_AMOUNT] = 0;
        }

        uint32_t small_byte = 0, large_byte = 0;
        if (t[R_NORMAL] > 0)        { small_byte = 1; large_byte = 120; }
        else if (t[R_ELECTRIC] > 0) { small_byte = 1; large_byte = 0; }
        else if (t[R_CUSTOM] > 0)   { small_byte = 0; large_byte = (uint32_t)(t[R_AMOUNT] & 0xFF); }

        drive(p, small_byte, large_byte);
        g_buzzing[p] = (small_byte || large_byte) ? 1 : 0;

        for (int i = R_NORMAL; i <= R_CUSTOM; i++)
            if (t[i] > 0)
                t[i]--;
    }
}

/* Stop every extra pad: one player, or the mod switched off. */
void coop_rumble_silence(void) {
    if (!rumble_available())
        return;
    CoopPartyArena* P = coop_party_arena();
    for (int p = 1; p < COOP_MAX_PLAYERS; p++) {
        P->rumble[p][R_NORMAL] = P->rumble[p][R_ELECTRIC] = 0;
        P->rumble[p][R_CUSTOM] = P->rumble[p][R_AMOUNT]   = 0;
        drive(p, 0, 0);
        g_buzzing[p] = 0;
    }
}

void coop_rumble_status(void) {
    if (!rumble_available()) {
        coop_status("Rumble: needs OpenPete 0.4 (mod api 12); this one is %u",
                    g_api->api_version);
        return;
    }
    if (!vibration_on()) {
        coop_status("Rumble: off in the game's options, or the pad has no motors");
        return;
    }
    CoopPartyArena* P = coop_party_arena();
    coop_status("Rumble: P2 %s, P3 %s, P4 %s",
                g_buzzing[1] ? "buzzing" : "quiet",
                g_buzzing[2] ? "buzzing" : "quiet",
                g_buzzing[3] ? "buzzing" : "quiet");
    coop_status("  timers P2 %d/%d/%d  P3 %d/%d/%d  P4 %d/%d/%d (normal/shock/custom)",
                P->rumble[1][R_NORMAL], P->rumble[1][R_ELECTRIC], P->rumble[1][R_CUSTOM],
                P->rumble[2][R_NORMAL], P->rumble[2][R_ELECTRIC], P->rumble[2][R_CUSTOM],
                P->rumble[3][R_NORMAL], P->rumble[3][R_ELECTRIC], P->rumble[3][R_CUSTOM]);
}
