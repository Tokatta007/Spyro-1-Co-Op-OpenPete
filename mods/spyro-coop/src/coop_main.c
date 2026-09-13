/**
 * @file coop_main.c
 * @brief Entry point, the arena, settings, and the Mods panel readout.
 */

#include "coop.h"

const openpete_mod_api_t* g_api;
openpete_mod_t*           g_self;
CoopStats                 g_stats;

static uint32_t g_arena_vaddr;  /* a guest address, valid across reloads */
static int      g_enabled;      /* from config; re-read on every reload */
static int      g_draw_enabled; /* from config; the visibility experiment */

CoopArena* coop_arena(void) {
    /* Resolved on every use: the host view is not promised to survive a
       process handoff, and the guest address is. */
    return (CoopArena*)g_api->guest(g_arena_vaddr);
}

int coop_enabled(void)      { return g_enabled; }
int coop_draw_enabled(void) { return g_draw_enabled; }

/* ------------------------------------------------------------------------
 * Readout
 * ---------------------------------------------------------------------- */

static uint32_t isqrt64(uint64_t v) {
    uint64_t r = 0, bit = 1ull << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else              { r >>= 1; }
        bit >>= 2;
    }
    return (uint32_t)r;
}

static uint32_t distance_between(const int32_t* a, const int32_t* b) {
    int64_t dx = (int64_t)a[0] - b[0];
    int64_t dy = (int64_t)a[1] - b[1];
    int64_t dz = (int64_t)a[2] - b[2];
    return isqrt64((uint64_t)(dx * dx + dy * dy + dz * dz));
}

void coop_publish_status(void) {
    CoopArena* A  = coop_arena();
    int32_t*   p1 = guest32(OP_GADDR_g_Spyro + SPYRO_OFF_POSITION);
    int32_t    p2[3];
    coop_p2_position(p2);

    if (!g_enabled)
        g_api->ui_status(g_self, "Player 2: OFF (enable it in this mod's settings)");
    else if (A->ready)
        g_api->ui_status(g_self, "Player 2: active in level %d%s",
                         A->last_level, A->handover ? ", handover pending" : "");
    else
        g_api->ui_status(g_self, "Player 2: waiting for gameplay");

    g_api->ui_status(g_self, "P1 at %d, %d, %d", p1[0], p1[1], p1[2]);
    if (A->ready) {
        g_api->ui_status(g_self, "P2 at %d, %d, %d", p2[0], p2[1], p2[2]);
        g_api->ui_status(g_self, "Distance apart: %u (started at %d)",
                         distance_between(p1, p2), P2_START_OFFSET);
    }
    g_api->ui_status(g_self, "P2 ticks %u, P2 camera updates %u",
                     g_stats.p2_ticks, g_stats.p2_cameras);
    g_api->ui_status(g_self, "Seeds %u, level reseeds %u, deaths %u, handovers %u, teleports %u",
                     g_stats.seeds, g_stats.level_reseeds, g_stats.deaths,
                     g_stats.handovers, g_stats.teleports);
    g_api->ui_status(g_self, "View swaps %u (press the swap_view key, P by default)",
                     g_stats.view_swaps);
    g_api->ui_status(g_self, "P2 drawn %u times, flame %u times%s",
                     g_stats.p2_draws, g_stats.p2_flame_draws,
                     g_draw_enabled ? "" : " (drawing OFF in settings)");
    g_api->ui_status(g_self, "Gameplay calls: tick %u, camera %u",
                     g_stats.tick_gameplay, g_stats.camera_gameplay);
    g_api->ui_status(g_self, "Other callers: tick %u (last ra 0x%08X), camera %u (last ra 0x%08X)",
                     g_stats.tick_other, g_stats.tick_other_ra,
                     g_stats.camera_other, g_stats.camera_other_ra);
    g_api->ui_status(g_self, "Collision guard refusals: probe %u, query %u",
                     g_stats.probe_refusals, g_stats.query_refusals);
    coop_pad_status();

    /* A summary in the log every ~10 seconds of gameplay, so a session can be
       read back afterwards without anyone watching the panel. */
    if (g_stats.camera_gameplay == 1u || (g_stats.camera_gameplay % 300u) == 0u) {
        g_api->log(g_self, OP_MOD_LOG_INFO,
                   "tick %u: ready=%u P1(%d,%d,%d) P2(%d,%d,%d) apart=%u | "
                   "p2ticks=%u p2cams=%u seeds=%u reseeds=%u deaths=%u handovers=%u "
                   "teleports=%u swaps=%u draws=%u flames=%u | other tick=%u ra=0x%08X other cam=%u ra=0x%08X | "
                   "guards probe=%u query=%u | padvsync=%u inswap=%u",
                   g_stats.camera_gameplay, A->ready,
                   p1[0], p1[1], p1[2], p2[0], p2[1], p2[2],
                   A->ready ? distance_between(p1, p2) : 0u,
                   g_stats.p2_ticks, g_stats.p2_cameras, g_stats.seeds,
                   g_stats.level_reseeds, g_stats.deaths, g_stats.handovers,
                   g_stats.teleports, g_stats.view_swaps,
                   g_stats.p2_draws, g_stats.p2_flame_draws,
                   g_stats.tick_other, g_stats.tick_other_ra,
                   g_stats.camera_other, g_stats.camera_other_ra,
                   g_stats.probe_refusals, g_stats.query_refusals,
                   g_stats.padvsync_calls, g_stats.padvsync_in_swap);
    }
}

/* ------------------------------------------------------------------------
 * Entry
 * ---------------------------------------------------------------------- */

static void on_toggle(CPUState* cpu, int on) {
    (void)cpu;
    if (!on)
        coop_players_disable();
}

int openpete_mod_entry(const openpete_mod_api_t* api, openpete_mod_t* self) {
    g_api  = api;
    g_self = self;

    g_enabled      = api->config_bool(self, "coop.enabled", 1);
    g_draw_enabled = api->config_bool(self, "coop.draw", 1);

    /* One allocation, at entry, every time. The engine replays the
       allocation sequence on reload and hands back the same bytes, so this
       must not depend on anything that varies between runs. */
    void* view = NULL;
    g_arena_vaddr = api->guest_alloc(self, sizeof(CoopArena), 4, 0, &view);
    if (g_arena_vaddr == 0) {
        api->log(self, OP_MOD_LOG_ERROR, "could not allocate %u bytes of arena",
                 (unsigned)sizeof(CoopArena));
        return 1;
    }

    if (coop_players_install() != 0 || coop_draw_install() != 0 ||
        coop_gates_install() != 0 || coop_pad_install() != 0)
        return 1;
    api->register_toggle_hook(self, on_toggle);

    api->log(self, OP_MOD_LOG_INFO,
             "spyro-coop phase A up: arena %u bytes at 0x%08X, player 2 %s",
             (unsigned)sizeof(CoopArena), g_arena_vaddr,
             g_enabled ? "enabled" : "disabled in settings");
    coop_publish_status();
    return 0;
}
