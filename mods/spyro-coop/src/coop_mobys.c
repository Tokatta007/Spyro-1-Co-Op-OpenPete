/**
 * @file coop_mobys.c
 * @brief Nearest-player mobys, and player 2's own Sparx. Ported from
 *        Sp1x2UpdateMobys, Sp1x2AssignMobys, Sp1x2MaskWalk and
 *        Sp1x2P2SparxKeep.
 *
 * THE MECHANISM. Every moby (enemy, gem, sheep, Sparx) is updated by one
 * per-level megafunction that GamestateUpdate calls through the g_UpdateMoby
 * pointer. Spyro 1 builds its list of active mobys inside that function, so
 * the list cannot be rebuilt per player the way Spyro2x2 does it. Instead we
 * use the builder's own skip rule: a moby with m_WasDrawn == 0 and
 * m_UpdateDistance == 0 is left off the list. So each frame:
 *   1. assign every moby to its nearest player;
 *   2. mask player 2's mobys and run the megafunction: player 1's mobys
 *      update, seeing player 1;
 *   3. unmask, swap player 2 in, mask player 1's, run it again.
 * Each moby updates exactly once per frame, seeing exactly one Spyro.
 *
 * WHY THE OVERRIDE IS REGISTERED LATE. g_UpdateMoby points into the loaded
 * level's code, a different function per level, and every level's code loads
 * at the same address (0x8007AA38). So the address is read at runtime and an
 * override is attached to it the first time it is seen. Because one address
 * can hold different code in different levels, the override acts only when
 * called from GamestateUpdate's own call site, which is a jalr through
 * g_UpdateMoby; called from anywhere else it runs stock.
 *
 * PS1 LESSONS KEPT:
 *   - Only moby state 0xFF ends the array. Other negative states are dead
 *     mobys still holding a slot, and stopping at them left the dynamic tail
 *     (the sheep) unmasked and double-updated.
 *   - Dead slots are never masked: a mid-pass spawn can reuse one, and
 *     unmasking stale bytes onto a fresh moby would corrupt it.
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
 * Late registration
 * ---------------------------------------------------------------------- */

static void on_moby_update(CPUState* cpu);

static uint32_t g_hooked[64];  /* addresses already overridden this process */
static unsigned g_hooked_n;

void coop_mobys_track(void) {
    uint32_t fn = *(uint32_t*)g_api->guest(OP_GADDR_g_UpdateMoby);
    if (fn == 0)
        return;
    for (unsigned i = 0; i < g_hooked_n; i++)
        if (g_hooked[i] == fn)
            return;
    if (g_hooked_n == sizeof g_hooked / sizeof g_hooked[0])
        return;
    if (g_api->override_addr(g_self, fn, on_moby_update) != 0) {
        coop_log(OP_MOD_LOG_ERROR, "could not override moby update at 0x%08X", fn);
        g_hooked[g_hooked_n++] = fn;  /* do not retry every frame */
        return;
    }
    g_hooked[g_hooked_n++] = fn;
    g_stats.moby_fns_hooked = g_hooked_n;
    coop_log(OP_MOD_LOG_INFO, "moby update hooked at 0x%08X (level %d)",
               fn, coop_level_id());
}

/* ------------------------------------------------------------------------
 * Ownership
 * ---------------------------------------------------------------------- */

static int64_t manhattan(const int32_t* a, const Vector3D* b) {
    int64_t dx = (int64_t)a[0] - b->x; if (dx < 0) dx = -dx;
    int64_t dy = (int64_t)a[1] - b->y; if (dy < 0) dy = -dy;
    int64_t dz = (int64_t)a[2] - b->z; if (dz < 0) dz = -dz;
    return dx + dy + dz;
}

static unsigned assign_mobys(CoopMobyArena* M, CoopArena* A,
                             Moby* mobys, uint32_t mobys_vaddr) {
    const int32_t* p1 = guest32(OP_GADDR_g_Spyro + SPYRO_OFF_POSITION);
    const int32_t* p2 = (const int32_t*)(A->spyro + SPYRO_OFF_POSITION);
    uint32_t sparx1 = *(uint32_t*)g_api->guest(OP_GADDR_g_Sparx);
    unsigned n;

    /* A new level gets a fresh table, so every moby starts with its truly
       nearest dragon instead of inheriting player 1 (BUGS.md A3). 3 means
       "unassigned" and falls through to plain nearest below. */
    CoopExtraArena* X = coop_extra_arena();
    if (X->owner_level != coop_level_id()) {
        memset(M->owner, 3, sizeof M->owner);
        X->owner_level = coop_level_id();
    }

    /* Switch owner only when the other dragon is this much closer. 25 is the
       PS1 value. Lower makes enemies change target more readily, and flips
       ownership more often, which PS1 found harmful mid-reaction (BUGS.md A2). */
    int64_t keep = 100 - coop_hysteresis_percent();

    for (n = 0; n < MOBY_MAX; n++) {
        int8_t state = (int8_t)mobys[n].m_State;
        if (state == -1)
            break;                           /* the real end of the array */
        if (state < 0) {
            M->owner[n] = 2;                 /* dead slot: never masked */
            continue;
        }
        uint32_t addr = mobys_vaddr + n * sizeof(Moby);
        if (addr == sparx1) {
            M->owner[n] = 0;
        } else if (M->p2_sparx != 0 && addr == M->p2_sparx) {
            M->owner[n] = 1;
        } else {
            int64_t d1 = manhattan(p1, &mobys[n].m_Position);
            int64_t d2 = manhattan(p2, &mobys[n].m_Position);
            uint8_t prev = M->owner[n];
            if (prev == 0)
                M->owner[n] = (d2 * 100 < d1 * keep) ? 1 : 0;
            else if (prev == 1)
                M->owner[n] = (d1 * 100 < d2 * keep) ? 0 : 1;
            else
                M->owner[n] = (d2 < d1) ? 1 : 0;
            if (prev <= 1 && M->owner[n] != prev)
                g_stats.owner_flips++;
        }
    }
    return n;
}

/* Hide (or restore) one player's mobys from the megafunction's list builder. */
static void mask_walk(CoopMobyArena* M, Moby* mobys, unsigned n, uint8_t owner, int unmask) {
    for (unsigned i = 0; i < n; i++) {
        if (M->owner[i] != owner)
            continue;
        if (unmask) {
            mobys[i].m_WasDrawn       = M->stash[i * 2];
            mobys[i].m_UpdateDistance = M->stash[i * 2 + 1];
        } else {
            M->stash[i * 2]     = mobys[i].m_WasDrawn;
            M->stash[i * 2 + 1] = mobys[i].m_UpdateDistance;
            mobys[i].m_WasDrawn       = 0;
            mobys[i].m_UpdateDistance = 0;
        }
    }
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
    /* g_Sparx == 0 means this level has no Sparx at all; follow suit. */
    if (M->p2_sparx == 0 && sparx1 != 0) {
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
 * moby keeps its dragon, and trade the two Sparx so each keeps following his.
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
}

/* ------------------------------------------------------------------------
 * The override
 * ---------------------------------------------------------------------- */

static void on_moby_update(CPUState* cpu) {
    /* GamestateUpdate's call site is a `jalr` through g_UpdateMoby, so being
       called from there already means this is the current level's update.
       Called from anywhere else, this address holds some other code. */
    if (cpu->ra != RA_GAMEPLAY_MOBY_UPDATE) {
        g_api->base(cpu);
        return;
    }

    CoopArena*     A = coop_arena();
    CoopMobyArena* M = coop_moby_arena();
    uint32_t mobys_vaddr = *(uint32_t*)g_api->guest(OP_GADDR_g_LevelMobys);
    Moby*    mobys = mobys_vaddr ? (Moby*)g_api->guest(mobys_vaddr) : NULL;

    if (coop_enabled())
        coop_handover_resume();              /* first co-op code each frame */

    if (!coop_enabled() || !A->ready || coop_gamestate() != GS_PLAYING ||
        !mobys || *guest32(OP_GADDR_g_IsFlightLevel) != 0 ||
        A->last_level != coop_level_id()) {
        g_stats.moby_single_pass++;
        g_api->base(cpu);                    /* retail behaviour, one pass */
        return;
    }

    SavedRegs regs;
    save_regs(cpu, &regs);

    /* ---- player 1's pass ---- */
    unsigned n = assign_mobys(M, A, mobys, mobys_vaddr);
    mask_walk(M, mobys, n, 1, 0);
    g_api->base(cpu);
    mask_walk(M, mobys, n, 1, 1);

    if (coop_gamestate() != GS_PLAYING)
        return;                              /* one of his mobys started a sequence */

    /* ---- player 2's pass ---- */
    n = assign_mobys(M, A, mobys, mobys_vaddr);  /* his pass may have spawned some */

    /* Camera too: sound attenuates from the camera, so an enemy dying next to
       player 2 has to be heard from his camera, not player 1's. */
    coop_swap_camera();
    coop_swap_spyro();
    A->swapped = 1;

    load_regs(cpu, &regs);
    p2_sparx_keep(cpu, M);

    {
        /* For this pass, player 2's dragonfly IS "the" Sparx: the megafunction
           finds the followed Sparx through g_Sparx and homes it on the anchor,
           which the tick restored to player 1's position. Both go back on
           every path; g_Sparx is our bookkeeping and must never leak. */
        uint32_t* g_sparx = (uint32_t*)g_api->guest(OP_GADDR_g_Sparx);
        int32_t*  anchor  = guest32(OP_GADDR_D_80077798);
        uint32_t  sparx1  = *g_sparx;
        int32_t   saved_anchor[3] = { anchor[0], anchor[1], anchor[2] };

        if (M->p2_sparx != 0) {
            *g_sparx = M->p2_sparx;
            /* With the focus vector per player, his own copy is already live
               here, swapped in with his camera. */
            if (!coop_focus_per_player())
                memcpy(anchor, guest32(OP_GADDR_g_Spyro + SPYRO_OFF_POSITION), 12);
        }

        mask_walk(M, mobys, n, 0, 0);
        load_regs(cpu, &regs);
        g_api->base(cpu);
        mask_walk(M, mobys, n, 0, 1);

        *g_sparx = sparx1;
        if (!coop_focus_per_player() && coop_gamestate() == GS_PLAYING)
            memcpy(anchor, saved_anchor, 12);
    }
    g_stats.moby_two_pass++;
    A->swapped = 0;

    int32_t gs = coop_gamestate();
    if (gs != GS_PLAYING) {
        /* One of HIS mobys (dragon statue, balloonist, portal) started a
           global sequence with his Spyro and camera live. Leave them live so
           the sequence has its player, exactly as the tick does. */
        if (gs == 4 || gs == 5) {
            A->ready = 0;
            g_stats.deaths++;
        } else {
            A->handover = 1;
            g_stats.handovers++;
        }
        return;
    }

    coop_swap_spyro();
    coop_swap_camera();
}
