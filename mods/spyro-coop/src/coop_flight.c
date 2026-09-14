/**
 * @file coop_flight.c
 * @brief Flight levels: a dragon who crashes sits out while the others finish.
 *
 * RETAIL (read from the executable, 2026-09-13). A flight level has no
 * respawns. When a crash kills Spyro, his own code does not call the death
 * trigger but, because g_IsFlightLevel is set, calls the flight level's
 * "end" function through the pointer D_80075694, which the loaders set to
 * that level's Flight1 (include/overlays/flight.inc.h): it sets gamestate 7,
 * the flight results. Three places in Spyro's code do this, and the level's
 * own update does too when time runs out or the course is finished.
 *
 * With co-op that meant one crash ended the run for everyone (seen by the
 * user). Here the pointer is pointed at an empty function the mod overrides,
 * and the override decides:
 *   - a crash (one of the three call sites in Spyro's code) while any other
 *     dragon is still flying: the crashed dragon sits out. He is no longer
 *     ticked, his camera no longer runs, and he is not drawn. If he was the
 *     camera's dragon, the camera moves to one still flying;
 *   - the last dragon's crash, the timer, or finishing the course: the real
 *     function runs, exactly as retail.
 *
 * THE BORROWED FUNCTION is func_8002C91C, empty, called only from the unused
 * prototype gamestate 6 (func_8002F3C4). That caller still gets its empty
 * function: calls from inside func_8002F3C4 go straight to base().
 *
 * Only a mod-owned value moves: the pointer is re-pointed every tick while a
 * flight level runs (a level load writes it afresh) and its real value kept
 * in the arena, so a savestate or a rewind carries both.
 */

#include "coop.h"

#define STUB_ADDR        OP_FNADDR_func_8002C91C
#define STUB_CALLER_LO   0x8002F3C4u          /* func_8002F3C4, the prototype stub */
#define STUB_CALLER_HI   0x8002F3E4u

/* Return addresses of the three `jalr D_80075694` in Spyro's code. */
#define RA_CRASH_PHYSICS 0x80042EDCu          /* func_80041670 */
#define RA_CRASH_COLLIDE 0x80048314u          /* func_80047B60 */
#define RA_CRASH_TICK    0x8004A494u          /* func_8004A200 */

static int is_flight(void) { return *guest32(OP_GADDR_g_IsFlightLevel) != 0; }

int coop_flight_slot_out(int slot) {
    if (slot < 0 || slot >= COOP_MAX_PLAYERS || !is_flight())
        return 0;
    return coop_party_arena()->flight_out[coop_physical_player(slot)] != 0;
}

/* A slot's health, whoever is live (see coop_respawn.c, WHOSE DEATH). */
static int32_t slot_health(int ticking, int slot) {
    if (slot == ticking)
        return *guest32(OP_GADDR_g_Spyro + SPYRO_OFF_HEALTH);
    int buffer = (slot == 0) ? ticking : slot;
    return *(int32_t*)(coop_shadow(buffer).spyro + SPYRO_OFF_HEALTH);
}

static void on_flight_end(CPUState* cpu) {
    uint32_t ra = cpu->ra;
    if (ra >= STUB_CALLER_LO && ra < STUB_CALLER_HI) {
        g_api->base(cpu);                    /* the prototype's own empty call */
        return;
    }

    CoopPartyArena* P = coop_party_arena();
    int crash = (ra == RA_CRASH_PHYSICS || ra == RA_CRASH_COLLIDE || ra == RA_CRASH_TICK);
    int shadows = coop_seeded_shadows();

    if (crash && coop_enabled() && shadows > 0 && is_flight() && coop_in_gameplay_tick()) {
        int dying = coop_ticking_player();
        int flying = 0;
        for (int s = 0; s <= shadows; s++) {
            if (s == dying || coop_flight_slot_out(s))
                continue;
            if (slot_health(dying, s) >= 0)
                flying++;
        }
        if (flying > 0) {
            int person = coop_physical_player(dying);
            if (!P->flight_out[person]) {
                P->flight_out[person] = 1;
                g_stats.flight_sit_outs++;
                coop_log(OP_MOD_LOG_INFO, "flight: player %d crashed and sits out, %d still flying",
                         person + 1, flying);
            }
            return;                          /* no results screen: the others fly on */
        }
    }

    /* The real end: the last crash, the timer, or the finish. */
    uint32_t real = P->flight_end_real;
    if (real == 0 || real == STUB_ADDR) {
        g_api->base(cpu);
        return;
    }
    g_api->call(cpu, real);
}

/* Once per tick, from the tick override: keep the pointer on the stub while a
   flight level runs, and forget who sat out once it is over. */
void coop_flight_tick(void) {
    CoopPartyArena* P = coop_party_arena();
    uint32_t* ptr = (uint32_t*)g_api->guest(OP_GADDR_D_80075694);
    if (!is_flight()) {
        memset(P->flight_out, 0, sizeof P->flight_out);
        return;
    }
    if (*ptr != STUB_ADDR && *ptr != 0) {
        P->flight_end_real = *ptr;           /* this level's Flight1 */
        *ptr = STUB_ADDR;
    }
}

/* The camera's dragon sat out in his own tick: give the camera to one still
   flying. Returns the slot traded with slot 0, or 0 if none was needed. */
int coop_flight_pick_camera(void) {
    if (!coop_flight_slot_out(0))
        return 0;
    int shadows = coop_seeded_shadows();
    for (int k = 1; k <= shadows; k++)
        if (!coop_flight_slot_out(k) &&
            *(int32_t*)(coop_shadow(k).spyro + SPYRO_OFF_HEALTH) >= 0)
            return k;
    return 0;
}

int coop_flight_install(void) {
    if (g_api->override_name(g_self, "func_8002C91C", on_flight_end) != 0) {
        coop_log(OP_MOD_LOG_ERROR, "could not install the flight sit-out");
        return 1;
    }
    return 0;
}
