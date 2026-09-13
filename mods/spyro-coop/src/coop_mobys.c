/**
 * @file coop_mobys.c
 * @brief Nearest-player mobys, and player 2's own Sparx. Ported from
 *        Sp1x2UpdateMobys, Sp1x2AssignMobys and Sp1x2P2SparxKeep, with the
 *        masking replaced by a filter on the game's own update list.
 *
 * THE MECHANISM. Every moby (enemy, gem, sheep, Sparx) is updated by one
 * per-level megafunction that GamestateUpdate calls through the g_UpdateMoby
 * pointer. Its first act is always the same main-executable function,
 * func_80051FEC, which writes the list of mobys to update this frame into
 * D_8006FCF4 + 0x400, zero-terminated; the megafunction then works from that
 * list. So each frame:
 *   1. assign every moby to its nearest player (per pod group, below);
 *   2. GamestateUpdate runs the megafunction as normal, and when the builder
 *      returns we drop every list entry that belongs to player 2: player 1's
 *      mobys update, seeing player 1;
 *   3. at the start of Spyro's tick, which GamestateUpdate calls next, we
 *      swap player 2 in and run the megafunction again ourselves, dropping
 *      player 1's entries.
 * Each moby updates exactly once per frame, seeing exactly one Spyro.
 *
 * WHY A LIST FILTER, 2026-09-13. Two earlier designs failed:
 *   - MASKING (the PS1 way): zero a moby's m_WasDrawn and m_UpdateDistance so
 *     the builder skips it. Defeated by PODS. Adding any moby marks its pod
 *     (m_Pod, 0x43), and a second loop adds every member of every marked pod
 *     from g_MobyPods, ignoring both fields. A moby whose podmate belonged to
 *     the other dragon was updated twice: a ram at double speed, turning
 *     between the dragons, steering both cameras. Filtering the finished
 *     list removes an entry however it got there.
 *   - OVERRIDING THE MEGAFUNCTION. g_UpdateMoby points into level code, and
 *     every level's code loads at 0x8007AA38, so an override attached to one
 *     level's megafunction sits on some other function in the next level. In
 *     level 10 that address is re-entered repeatedly, every entry nested
 *     through the override, and the engine logged 74,705 "override frame
 *     stack overflow" errors in 110 seconds. Every hook here is now on
 *     main-executable code, which never moves.
 *
 * Pods are still owned as a group, because a pod's members coordinate and
 * splitting one between the two passes would be wrong even without double
 * updates.
 *
 * PS1 LESSONS KEPT:
 *   - Only moby state 0xFF ends the array. Other negative states are dead
 *     mobys still holding a slot, and stopping at them left the dynamic tail
 *     (the sheep) unmasked and double-updated.
 *   - Dead slots belong to nobody.
 *   - Hysteresis: a moby keeps its owner unless the other player is at least
 *     25% closer, or enemies between the players re-target every frame.
 *   - Reassign before player 2's pass: player 1's pass may spawn mobys.
 *   - Flight levels run the megafunction ONCE. Their level code does global
 *     work (timer, chests, flight state) that ran twice per frame otherwise.
 *   - If a pass starts a sequence (dragon, portal, balloonist), stop, and for
 *     player 2 leave his state live so the sequence has its player.
 */

#include "coop.h"
#include <openpete_game_structs.h>

#define SPARX_CLASS 120  /* how retail's LoadLevelScene spawns player 1's */

/* The PS1 build went through versions that spawned a new dragonfly on every
   death, and one that orphaned them. If a spawned fly keeps dying at once for
   a reason nobody has found yet, this stops it filling the level. */
#define SPARX_SPAWN_CAP_PER_LEVEL 4

/* ------------------------------------------------------------------------
 * Ownership
 * ---------------------------------------------------------------------- */

static int64_t manhattan(const int32_t* a, const Vector3D* b) {
    int64_t dx = (int64_t)a[0] - b->x; if (dx < 0) dx = -dx;
    int64_t dy = (int64_t)a[1] - b->y; if (dy < 0) dy = -dy;
    int64_t dz = (int64_t)a[2] - b->z; if (dz < 0) dz = -dz;
    return dx + dy + dz;
}

#define POD_COUNT 32                 /* func_80051FEC keeps 32 pod flags */
#define POD_NONE(pod) ((pod) >= 0x80)  /* the builder tests the byte signed */

/* One owner decision with hysteresis. prev 0 or 1 is sticky: switch only when
   the other dragon is closer by the configured margin. Anything else (3 for a
   fresh level, 2 for a revived dead slot) takes plain nearest. */
static uint8_t decide_owner(uint8_t prev, int64_t d1, int64_t d2, int64_t keep) {
    if (prev == 0) return (d2 * 100 < d1 * keep) ? 1 : 0;
    if (prev == 1) return (d1 * 100 < d2 * keep) ? 0 : 1;
    return (d2 < d1) ? 1 : 0;
}

static unsigned assign_mobys(CoopMobyArena* M, CoopArena* A,
                             Moby* mobys, uint32_t mobys_vaddr) {
    const int32_t* p1 = guest32(OP_GADDR_g_Spyro + SPYRO_OFF_POSITION);
    const int32_t* p2 = (const int32_t*)(A->spyro + SPYRO_OFF_POSITION);
    uint32_t sparx1 = *(uint32_t*)g_api->guest(OP_GADDR_g_Sparx);
    unsigned n;

    /* A new level gets a fresh table, so every moby starts with its truly
       nearest dragon instead of inheriting player 1 (BUGS.md A3). 3 means
       "unassigned" and falls through to plain nearest. */
    CoopExtraArena* X = coop_extra_arena();
    if (X->owner_level != coop_level_id()) {
        memset(M->owner, 3, sizeof M->owner);
        X->owner_level = coop_level_id();
    }

    /* Switch owner only when the other dragon is this much closer. 25 is the
       PS1 value. Lower makes enemies change target more readily, and flips
       ownership more often, which PS1 found harmful mid-reaction (BUGS.md A2). */
    int64_t keep = 100 - coop_hysteresis_percent();

    /* ---- which pod group each moby belongs to ----
       The builder MARKS a pod from a moby's m_Pod, then ADDS every index in
       that pod's g_MobyPods list (a list of shorts, low 15 bits the moby
       index, sign bit on the last entry). Membership is read from those lists
       exactly as the builder reads them, and pods linked by a moby whose m_Pod
       names a different pod are merged, so no group can be split. */
    int pod_of[MOBY_MAX];
    uint8_t pod_parent[POD_COUNT];
    uint8_t pod_marked[POD_COUNT];
    for (int p = 0; p < POD_COUNT; p++) { pod_parent[p] = (uint8_t)p; pod_marked[p] = 0; }

    for (n = 0; n < MOBY_MAX; n++) {
        int8_t state = (int8_t)mobys[n].m_State;
        if (state == -1)
            break;                           /* the real end of the array */
        pod_of[n] = -1;
        if (state >= 0 && !POD_NONE(mobys[n].m_Pod) && mobys[n].m_Pod < POD_COUNT) {
            pod_of[n] = mobys[n].m_Pod;
            pod_marked[mobys[n].m_Pod] = 1;
        }
    }

    uint32_t pods_vaddr = *(uint32_t*)g_api->guest(OP_GADDR_g_MobyPods);
    uint32_t* pod_lists = pods_vaddr ? (uint32_t*)g_api->guest(pods_vaddr) : NULL;
    for (int p = 0; pod_lists && p < POD_COUNT; p++) {
        if (!pod_marked[p] || pod_lists[p] == 0)
            continue;                        /* a pod no moby can mark is never walked */
        int16_t* e = (int16_t*)g_api->guest(pod_lists[p]);
        for (unsigned k = 0; e && k < MOBY_MAX; k++) {
            int16_t raw = e[k];
            unsigned idx = (unsigned)(raw & 0x7FFF);
            if (idx < n && (int8_t)mobys[idx].m_State >= 0) {
                if (pod_of[idx] < 0) {
                    pod_of[idx] = p;         /* in the list without naming the pod */
                } else if (pod_of[idx] != p) {
                    /* union the two pods */
                    int a = p, b = pod_of[idx];
                    while (pod_parent[a] != a) a = pod_parent[a];
                    while (pod_parent[b] != b) b = pod_parent[b];
                    if (a != b) { pod_parent[b] = (uint8_t)a; g_stats.pod_merges++; }
                }
            }
            if (raw < 0)
                break;                       /* sign bit: last entry */
        }
    }
    for (unsigned i = 0; i < n; i++) {
        if (pod_of[i] >= 0) {
            int r = pod_of[i];
            while (pod_parent[r] != r) r = pod_parent[r];
            pod_of[i] = r;
        }
    }

    /* ---- each group's nearest member to each dragon ---- */
    int64_t  pod_d1[POD_COUNT], pod_d2[POD_COUNT];
    int      pod_first[POD_COUNT];
    for (int p = 0; p < POD_COUNT; p++) {
        pod_d1[p] = pod_d2[p] = INT64_MAX;
        pod_first[p] = -1;
    }
    for (unsigned i = 0; i < n; i++) {
        if (pod_of[i] < 0)
            continue;
        int p = pod_of[i];
        int64_t d1 = manhattan(p1, &mobys[i].m_Position);
        int64_t d2 = manhattan(p2, &mobys[i].m_Position);
        if (d1 < pod_d1[p]) pod_d1[p] = d1;
        if (d2 < pod_d2[p]) pod_d2[p] = d2;
        if (pod_first[p] < 0) pod_first[p] = (int)i;
    }

    /* ---- one owner per group, sticky through its first member's last
       owner: every member shares an owner after this, so that value IS the
       group's previous owner. ---- */
    uint8_t pod_owner[POD_COUNT];
    for (int p = 0; p < POD_COUNT; p++) {
        if (pod_first[p] < 0)
            continue;
        pod_owner[p] = decide_owner(M->owner[pod_first[p]], pod_d1[p], pod_d2[p], keep);
    }

    /* ---- assign every moby ---- */
    unsigned pod_members = 0;
    for (unsigned i = 0; i < n; i++) {
        int8_t state = (int8_t)mobys[i].m_State;
        if (state < 0) {
            M->owner[i] = 2;                 /* dead slot: never masked */
            continue;
        }
        uint8_t prev = M->owner[i];
        uint32_t addr = mobys_vaddr + i * sizeof(Moby);

        if (pod_of[i] >= 0) {
            /* The group's owner wins, even for a Sparx: the builder would pull
               a split group into both passes whatever we decide per moby. */
            M->owner[i] = pod_owner[pod_of[i]];
            pod_members++;
        } else if (addr == sparx1) {
            M->owner[i] = 0;
        } else if (M->p2_sparx != 0 && addr == M->p2_sparx) {
            M->owner[i] = 1;
        } else {
            M->owner[i] = decide_owner(prev,
                                       manhattan(p1, &mobys[i].m_Position),
                                       manhattan(p2, &mobys[i].m_Position), keep);
        }
        if (prev <= 1 && M->owner[i] != prev)
            g_stats.owner_flips++;
    }
    g_stats.pod_members = pod_members;
    return n;
}

/* ------------------------------------------------------------------------
 * Player 2's Sparx: Spyromain's sp2x2_rayz, ported via Sp1x2P2SparxKeep.
 *
 * v4 of the PS1 bookkeeping, after three versions that orphaned or
 * duplicated dragonflies. Only a change to a DIFFERENT LIVING g_Sparx is a
 * level rebuild; a null is a death transient, and player 2's fly is fine.
 * Called with player 2 live, so the spawn reads his Spyro if it reads one.
 * ---------------------------------------------------------------------- */
static void p2_sparx_keep(CPUState* cpu, CoopMobyArena* M) {
    uint32_t sparx1 = *(uint32_t*)g_api->guest(OP_GADDR_g_Sparx);

    if (sparx1 != 0 && sparx1 != M->sparx1_seen) {
        M->sparx1_seen = sparx1;             /* real rebuild: old world gone */
        M->p2_sparx = 0;
        M->sparx_spawns_level = 0;
    }
    if (M->p2_sparx != 0) {
        Moby* fly = (Moby*)g_api->guest(M->p2_sparx);
        if (!fly || M->p2_sparx == sparx1 || (int8_t)fly->m_State < 0)
            M->p2_sparx = 0;                 /* his fly died, or was reused */
    }
    /* g_Sparx == 0 means this level has no Sparx at all; follow suit. And a
       dragon with no health has no Sparx in retail: respawning one would only
       lose it again, and burn the spawn cap doing so. */
    if (M->p2_sparx == 0 && sparx1 != 0 &&
        *guest32(OP_GADDR_g_Spyro + SPYRO_OFF_HEALTH) > 0) {
        if (M->sparx_spawns_level >= SPARX_SPAWN_CAP_PER_LEVEL) {
            if (M->sparx_spawns_level == SPARX_SPAWN_CAP_PER_LEVEL) {
                M->sparx_spawns_level++;     /* log once */
                coop_log(OP_MOD_LOG_WARN,
                           "player 2's Sparx spawn cap reached in level %d; not respawning",
                           coop_level_id());
            }
            return;
        }
        uint32_t spawn = *(uint32_t*)g_api->guest(OP_GADDR_g_SpawnMoby);
        if (spawn == 0)
            return;
        cpu->a0 = SPARX_CLASS;
        cpu->a1 = 0;
        g_api->call(cpu, spawn);             /* the level's own moby factory */
        M->p2_sparx = cpu->v0;               /* a guest Moby*, or 0 on failure */
        if (M->p2_sparx != 0) {
            M->sparx_spawns_level++;
            g_stats.sparx_spawns++;
            coop_log(OP_MOD_LOG_INFO, "player 2's Sparx spawned at 0x%08X",
                       M->p2_sparx);
        }
    }
}

/* ------------------------------------------------------------------------
 * The view-swap key traded the two dragons' identities. Ownership is stored
 * per slot (0 = whoever is player 1), so without this every moby would be
 * reassigned to the other physical dragon at once: a ram charging one dragon
 * would suddenly be driven against the other. Measured 2026-09-12: 1,374
 * owner changes in a session with 16 presses. Swap the owner values so each
 * moby keeps its dragon, trade the two Sparx so each keeps following his, and
 * move the save-fairy mute with the dragon it belongs to.
 * ---------------------------------------------------------------------- */
void coop_mobys_identities_swapped(void) {
    CoopMobyArena* M = coop_moby_arena();
    for (unsigned i = 0; i < MOBY_MAX; i++) {
        if (M->owner[i] == 0)      M->owner[i] = 1;
        else if (M->owner[i] == 1) M->owner[i] = 0;
    }

    uint32_t* g_sparx = (uint32_t*)g_api->guest(OP_GADDR_g_Sparx);
    if (*g_sparx != 0 && M->p2_sparx != 0) {
        uint32_t t = *g_sparx;
        *g_sparx = M->p2_sparx;
        M->p2_sparx = t;
        /* The new g_Sparx is not a level rebuild; tell the detector so. */
        M->sparx1_seen = *g_sparx;
    }

    /* The save-fairy mute names a slot too. Seen 2026-09-13: pressing the key
       while standing on the pedestal let the fairy talk at once. */
    CoopRespawnArena* R = coop_respawn_arena();
    if (R->fairy_mute[0] == 1)      R->fairy_mute[0] = 2;
    else if (R->fairy_mute[0] == 2) R->fairy_mute[0] = 1;
}

/* ------------------------------------------------------------------------
 * The hooks
 * ---------------------------------------------------------------------- */

#define LIST_BUFFER   (OP_GADDR_D_8006FCF4 + 0x400)  /* func_80051FEC's output */
#define LIST_MAX      1024                            /* stop somewhere if unterminated */

/* Which player's entries survive the next list build: -1 none, 0 or 1. Armed
   immediately before a megafunction runs and consumed by the one builder call
   it makes first, so it never outlives the pass it was armed for. */
static int      g_filter_owner = -1;
static unsigned g_assigned_n;      /* mobys covered by the last assignment */
static int      g_p2_pass_pending; /* player 1's pass ran filtered this frame */

/* func_80051FEC, post-hook: drop entries owned by the other player. */
static void on_list_builder(CPUState* cpu) {
    g_api->base(cpu);

    int keep_owner = g_filter_owner;
    g_filter_owner = -1;                     /* one build per pass */
    if (keep_owner < 0)
        return;

    CoopMobyArena* M = coop_moby_arena();
    uint32_t mobys_vaddr = *(uint32_t*)g_api->guest(OP_GADDR_g_LevelMobys);
    uint32_t* list = (uint32_t*)g_api->guest(LIST_BUFFER);
    if (!list || mobys_vaddr == 0)
        return;

    unsigned r = 0, w = 0;
    for (; r < LIST_MAX && list[r] != 0; r++) {
        uint32_t moby = list[r];
        int drop = 0;
        if (moby >= mobys_vaddr) {
            uint32_t idx = (moby - mobys_vaddr) / sizeof(Moby);
            /* Only mobys this frame's assignment covered: anything spawned
               since, or outside the array, is left alone. */
            if (idx < g_assigned_n && (moby - mobys_vaddr) % sizeof(Moby) == 0) {
                uint8_t owner = M->owner[idx];
                drop = (owner <= 1 && owner != (uint8_t)keep_owner);
            }
        }
        if (drop)
            g_stats.list_dropped++;
        else
            list[w++] = moby;
    }
    list[w] = 0;
}

static int two_pass_allowed(CoopArena* A) {
    return coop_enabled() && A->ready && coop_gamestate() == GS_PLAYING &&
           *(uint32_t*)g_api->guest(OP_GADDR_g_LevelMobys) != 0 &&
           *guest32(OP_GADDR_g_IsFlightLevel) == 0 &&
           A->last_level == coop_level_id();
}

/* func_8002A6FC (environment animation), post-hook at GamestateUpdate's
   gameplay call, which comes immediately before the megafunction. */
static void on_env_update(CPUState* cpu) {
    g_api->base(cpu);
    g_filter_owner = -1;
    g_p2_pass_pending = 0;
    if (cpu->ra != RA_GAMEPLAY_ENV_UPDATE)
        return;

    SavedRegs regs;
    save_regs(cpu, &regs);

    CoopArena* A = coop_arena();
    if (coop_enabled())
        coop_handover_resume();              /* first co-op code each frame */

    if (!two_pass_allowed(A)) {
        g_stats.moby_single_pass++;
        return;                              /* retail: one unfiltered pass */
    }

    CoopMobyArena* M = coop_moby_arena();
    uint32_t mobys_vaddr = *(uint32_t*)g_api->guest(OP_GADDR_g_LevelMobys);
    Moby* mobys = (Moby*)g_api->guest(mobys_vaddr);

    coop_sparx_heal(cpu);                    /* after his own respawn */
    load_regs(cpu, &regs);
    g_assigned_n = assign_mobys(M, A, mobys, mobys_vaddr);
    coop_fairy_mute(0);

    g_filter_owner = 0;                      /* the megafunction GamestateUpdate */
    g_p2_pass_pending = 1;                   /* calls next is player 1's pass */
}

/* Called first thing in the gameplay tick override. Runs player 2's moby pass.
   Returns 1 if that pass started a sequence retail would skip Spyro's tick
   for (fairy, balloonist, flight results, level transition), so the caller
   returns without ticking. */
int coop_mobys_p2_pass(CPUState* cpu) {
    g_filter_owner = -1;                     /* player 1's pass is over */
    if (!g_p2_pass_pending)
        return 0;
    g_p2_pass_pending = 0;

    CoopArena* A = coop_arena();
    if (!two_pass_allowed(A))
        return 0;                            /* one of his mobys started a sequence */

    uint32_t update = *(uint32_t*)g_api->guest(OP_GADDR_g_UpdateMoby);
    if (update == 0)
        return 0;

    SavedRegs regs;
    save_regs(cpu, &regs);

    CoopMobyArena* M = coop_moby_arena();
    uint32_t mobys_vaddr = *(uint32_t*)g_api->guest(OP_GADDR_g_LevelMobys);
    Moby* mobys = (Moby*)g_api->guest(mobys_vaddr);
    g_assigned_n = assign_mobys(M, A, mobys, mobys_vaddr);  /* his pass may have spawned some */

    /* Camera too: sound attenuates from the camera, so an enemy dying next to
       player 2 has to be heard from his camera, not player 1's. */
    coop_swap_camera();
    coop_swap_spyro();
    A->swapped = 1;

    coop_fairy_mute(1);
    p2_sparx_keep(cpu, M);
    load_regs(cpu, &regs);

    /* For this pass, player 2's dragonfly IS "the" Sparx: the megafunction
       finds the followed Sparx through g_Sparx and homes it on the anchor,
       which his tick left as his position. Both go back on every path;
       g_Sparx is our bookkeeping and must never leak. */
    uint32_t* g_sparx = (uint32_t*)g_api->guest(OP_GADDR_g_Sparx);
    int32_t*  anchor  = guest32(OP_GADDR_D_80077798);
    uint32_t  sparx1  = *g_sparx;
    int32_t   saved_anchor[3] = { anchor[0], anchor[1], anchor[2] };
    if (M->p2_sparx != 0) {
        *g_sparx = M->p2_sparx;
        memcpy(anchor, guest32(OP_GADDR_g_Spyro + SPYRO_OFF_POSITION), 12);
    }

    g_filter_owner = 1;
    g_api->call(cpu, update);                /* the level's megafunction, his pass */
    g_filter_owner = -1;

    *g_sparx = sparx1;
    if (coop_gamestate() == GS_PLAYING)
        memcpy(anchor, saved_anchor, 12);

    g_stats.moby_two_pass++;
    A->swapped = 0;
    load_regs(cpu, &regs);

    int32_t gs = coop_gamestate();
    if (gs != GS_PLAYING) {
        /* One of HIS mobys (dragon statue, balloonist, portal) started a
           global sequence with his Spyro and camera live. Leave them live so
           the sequence has its player, exactly as the tick does. */
        if (gs == 4 || gs == 5) {
            A->ready = 0;
            coop_extra_arena()->p2_health_carry[0] = 0;
            g_stats.deaths++;
        } else {
            A->handover = 1;
            g_stats.handovers++;
        }
        /* GamestateUpdate skips Spyro's tick for these after a moby pass. */
        return gs == 11 || gs == 12 || gs == 7 || gs == 1;
    }

    coop_swap_spyro();
    coop_swap_camera();
    return 0;
}

int coop_mobys_install(void) {
    if (g_api->override_name(g_self, "func_8002A6FC", on_env_update) != 0 ||
        g_api->override_name(g_self, "func_80051FEC", on_list_builder) != 0) {
        coop_log(OP_MOD_LOG_ERROR, "could not install the moby passes");
        return 1;
    }
    return 0;
}
