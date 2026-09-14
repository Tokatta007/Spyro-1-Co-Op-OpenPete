/**
 * @file coop_main.c
 * @brief Entry point, the arena, settings, and the Mods panel readout.
 */

#include "coop.h"
#include <stdarg.h>
#include <stdio.h>

const openpete_mod_api_t* g_api;
openpete_mod_t*           g_self;
CoopStats                 g_stats;

static uint32_t g_arena_vaddr;  /* a guest address, valid across reloads */
static uint32_t g_moby_vaddr;   /* the moby partition block, likewise */
static uint32_t g_extra_vaddr;  /* the third block, likewise */
static uint32_t g_respawn_vaddr;  /* the fourth block, likewise */
static uint32_t g_menu_vaddr;     /* the fifth block, likewise */
static uint32_t g_party_vaddr;    /* the sixth block: players 3 and 4 */
static uint32_t g_fx_vaddr;       /* the seventh block: respawn effect scratch */

CoopArena* coop_arena(void) {
    /* Resolved on every use: the host view is not promised to survive a
       process handoff, and the guest address is. */
    return (CoopArena*)g_api->guest(g_arena_vaddr);
}

void coop_status(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    g_api->ui_status(g_self, "%s", buf);
}

void coop_log(int level, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    g_api->log(g_self, level, "%s", buf);
}

CoopPartyArena* coop_party_arena(void) {
    return (CoopPartyArena*)g_api->guest(g_party_vaddr);
}

CoopRespawnArena* coop_respawn_arena(void) {
    return (CoopRespawnArena*)g_api->guest(g_respawn_vaddr);
}
int coop_respawn_enabled(void) { return g_settings.respawn_modern; }

CoopExtraArena* coop_extra_arena(void) {
    return (CoopExtraArena*)g_api->guest(g_extra_vaddr);
}
int coop_hysteresis_percent(void)  { return g_settings.hysteresis; }

CoopMobyArena* coop_moby_arena(void) {
    return (CoopMobyArena*)g_api->guest(g_moby_vaddr);
}

int coop_enabled(void)      { return g_settings.players >= 2; }
int coop_shadow_count(void) {
    int n = g_settings.players - 1;
    return (n < 0) ? 0 : (n > COOP_MAX_SHADOWS) ? COOP_MAX_SHADOWS : n;
}
int coop_draw_enabled(void) { return g_settings.draw_p2; }

/* ------------------------------------------------------------------------
 * Readout
 * ---------------------------------------------------------------------- */

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

    if (g_settings.players < 2)
        coop_status("Extra players: OFF (players set to 1)");
    else if (A->ready)
        coop_status("Players: %d, active in level %d%s", coop_seeded_shadows() + 1,
                         A->last_level, A->handover ? ", handover pending" : "");
    else
        coop_status("Players: %d, waiting for gameplay", g_settings.players);

    coop_status("P1 at %d, %d, %d", p1[0], p1[1], p1[2]);
    if (A->ready) {
        coop_status("P2 at %d, %d, %d", p2[0], p2[1], p2[2]);
        coop_status("Distance apart: %u (started at %d)",
                         distance_between(p1, p2), P2_START_OFFSET);
    }
    coop_status("P2 ticks %u, P2 camera updates %u",
                     g_stats.p2_ticks, g_stats.p2_cameras);
    coop_status("Seeds %u, level reseeds %u, deaths %u, handovers %u, teleports %u",
                     g_stats.seeds, g_stats.level_reseeds, g_stats.deaths,
                     g_stats.handovers, g_stats.teleports);
    coop_status("View swaps %u (press the swap_view key, P by default)",
                     g_stats.view_swaps);
    coop_status("P2 drawn %u times, flame %u times, portal wingman %u times%s",
                     g_stats.p2_draws, g_stats.p2_flame_draws, g_stats.flyin_draws,
                     g_settings.draw_p2 ? "" : " (drawing OFF in settings)");
    coop_status("Moby passes: two-player %u, single %u; list entries dropped %u",
                     g_stats.moby_two_pass, g_stats.moby_single_pass, g_stats.list_dropped);
    coop_status("P2 Sparx spawns %u, body pushes %u, moby owner flips %u (switch at %d%% closer)",
                     g_stats.sparx_spawns, g_stats.pushes, g_stats.owner_flips, g_settings.hysteresis);
    coop_status("Mobys in pods (owned as a group): %u, pod merges %u", g_stats.pod_members, g_stats.pod_merges);
    coop_status("Sounds measured from player 2's camera: %u", g_stats.sounds_nearer_p2);
    coop_status("Respawn style: %s; separate respawns %u, double deaths %u, Sparx heals %u",
                g_settings.respawn_modern ? "modern" : "original", g_stats.individual_respawns,
                g_stats.double_deaths, g_stats.sparx_heals);
    for (int i = 0; i < 2; i++)
        coop_status("  P%d camera: on shared vector %u frames, runaway %u frames in %u events, max %u away",
                    i + 1, g_stats.cam_on_shared[i], g_stats.cam_runaway[i],
                    g_stats.cam_runaway_events[i], g_stats.cam_max_dist[i]);
    coop_status("Gameplay calls: tick %u, camera %u",
                     g_stats.tick_gameplay, g_stats.camera_gameplay);
    coop_status("Other callers: tick %u (last ra 0x%08X), camera %u (last ra 0x%08X)",
                     g_stats.tick_other, g_stats.tick_other_ra,
                     g_stats.camera_other, g_stats.camera_other_ra);
    coop_status("Collision guard refusals: probe %u, query %u",
                     g_stats.probe_refusals, g_stats.query_refusals);
    coop_status("Multiplayer page opened %u times (pause, then MULTIPLAYER)", g_stats.menu_opens);
    coop_pad_status();

    /* A summary in the log every ~10 seconds of gameplay, so a session can be
       read back afterwards without anyone watching the panel. */
    if (g_stats.camera_gameplay == 1u || (g_stats.camera_gameplay % 300u) == 0u) {
        coop_log(OP_MOD_LOG_INFO,
                   "tick %u: ready=%u P1(%d,%d,%d) P2(%d,%d,%d) apart=%u | "
                   "p2ticks=%u p2cams=%u seeds=%u reseeds=%u deaths=%u handovers=%u "
                   "teleports=%u swaps=%u draws=%u flames=%u flyin=%u | mobys 2p=%u 1p=%u dropped=%u sparx=%u pushes=%u flips=%u pods=%u | respawn on=%d solo=%u double=%u heals=%u | cam shared=%u/%u runaway=%u/%u events=%u/%u max=%u/%u | other tick=%u ra=0x%08X other cam=%u ra=0x%08X | "
                   "guards probe=%u query=%u | padvsync=%u inswap=%u",
                   g_stats.camera_gameplay, A->ready,
                   p1[0], p1[1], p1[2], p2[0], p2[1], p2[2],
                   A->ready ? distance_between(p1, p2) : 0u,
                   g_stats.p2_ticks, g_stats.p2_cameras, g_stats.seeds,
                   g_stats.level_reseeds, g_stats.deaths, g_stats.handovers,
                   g_stats.teleports, g_stats.view_swaps,
                   g_stats.p2_draws, g_stats.p2_flame_draws, g_stats.flyin_draws,
                   g_stats.moby_two_pass, g_stats.moby_single_pass,
                   g_stats.list_dropped, g_stats.sparx_spawns, g_stats.pushes,
                   g_stats.owner_flips, g_stats.pod_members,
                   g_settings.respawn_modern, g_stats.individual_respawns,
                   g_stats.double_deaths, g_stats.sparx_heals,
                   g_stats.cam_on_shared[0], g_stats.cam_on_shared[1],
                   g_stats.cam_runaway[0], g_stats.cam_runaway[1],
                   g_stats.cam_runaway_events[0], g_stats.cam_runaway_events[1],
                   g_stats.cam_max_dist[0], g_stats.cam_max_dist[1],
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

    /* Settings from mods/spyro-coop/data/settings.txt, shared by the in-game
       Multiplayer menu and the M overlay panel (coop_settings.c). */
    coop_settings_load();

    /* One allocation, at entry, every time. The engine replays the
       allocation sequence on reload and hands back the same bytes, so this
       must not depend on anything that varies between runs. */
    void* view = NULL;
    g_arena_vaddr = api->guest_alloc(self, sizeof(CoopArena), 4, 0, &view);
    if (g_arena_vaddr == 0) {
        coop_log(OP_MOD_LOG_ERROR, "could not allocate %u bytes of arena",
                 (unsigned)sizeof(CoopArena));
        return 1;
    }
    /* Appended, never merged into the block above: see CoopMobyArena. */
    g_moby_vaddr = api->guest_alloc(self, sizeof(CoopMobyArena), 4, 0, &view);
    if (g_moby_vaddr == 0) {
        coop_log(OP_MOD_LOG_ERROR, "could not allocate the moby table");
        return 1;
    }
    g_extra_vaddr = api->guest_alloc(self, sizeof(CoopExtraArena), 4, 0, &view);
    if (g_extra_vaddr == 0) {
        coop_log(OP_MOD_LOG_ERROR, "could not allocate the extra block");
        return 1;
    }
    g_respawn_vaddr = api->guest_alloc(self, sizeof(CoopRespawnArena), 4, 0, &view);
    if (g_respawn_vaddr == 0) {
        coop_log(OP_MOD_LOG_ERROR, "could not allocate the respawn block");
        return 1;
    }
    g_menu_vaddr = api->guest_alloc(self, sizeof(CoopMenuArena), 4, 0, &view);
    if (g_menu_vaddr == 0) {
        coop_log(OP_MOD_LOG_ERROR, "could not allocate the menu block");
        return 1;
    }
    g_party_vaddr = api->guest_alloc(self, sizeof(CoopPartyArena), 4, 0, &view);
    if (g_party_vaddr == 0) {
        coop_log(OP_MOD_LOG_ERROR, "could not allocate the players 3 and 4 block");
        return 1;
    }
    g_fx_vaddr = api->guest_alloc(self, sizeof(CoopFxArena), 4, 0, &view);
    if (g_fx_vaddr == 0) {
        coop_log(OP_MOD_LOG_ERROR, "could not allocate the effects block");
        return 1;
    }
    coop_effects_init(g_fx_vaddr);


    if (coop_players_install() != 0 || coop_draw_install() != 0 ||
        coop_gates_install() != 0 || coop_pad_install() != 0 ||
        coop_respawn_install() != 0 || coop_mobys_install() != 0 ||
        coop_sound_install() != 0 || coop_menu_install(g_menu_vaddr) != 0 ||
        coop_flight_install() != 0)
        return 1;
    api->register_toggle_hook(self, on_toggle);

    coop_log(OP_MOD_LOG_INFO,
             "spyro-coop phase A up: arena %u bytes at 0x%08X, player 2 %s",
             (unsigned)sizeof(CoopArena), g_arena_vaddr,
             g_settings.players >= 2 ? "enabled" : "off (players = 1)");
    coop_publish_status();
    return 0;
}
