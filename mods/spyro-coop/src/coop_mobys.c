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
 *     a shadow leave his state live so the sequence has its player.
 *
 * FOUR PLAYERS (2026-09-13): the owner is a slot, 0..3; every moby goes to the
 * nearest dragon with the same hysteresis, and each shadow gets his own
 * filtered pass after slot 0's.
 */

#include "coop.h"
#include <openpete_game_structs.h>

#define SPARX_CLASS 120  /* how retail's LoadLevelScene spawns player 1's */

/* The PS1 build went through versions that spawned a new dragonfly on every
   death, and one that orphaned them. If a spawned fly keeps dying at once for
   a reason nobody has found yet, this stops it filling the level. */
#define SPARX_SPAWN_CAP_PER_LEVEL 12   /* 4 per extra dragon */

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

/* One owner decision with hysteresis, over the n+1 dragons. A previous owner
   that is still a dragon is sticky: it changes only when the nearest other
   dragon is closer by the configured margin. Anything else (OWNER_NEW for a
   fresh level, OWNER_DEAD for a revived dead slot) takes plain nearest. With
   two dragons this is exactly the PS1 rule. */
static uint8_t decide_owner(uint8_t prev, const int64_t d[], int n, int64_t keep) {
    int best = 0;
    for (int s = 1; s <= n; s++)
        if (d[s] < d[best]) best = s;
    if (prev <= (uint8_t)n && best != prev)
        return (d[best] * 100 < d[prev] * keep) ? (uint8_t)best : prev;
    return (uint8_t)best;
}

/* PORTALS (2026-09-13, seen by the user and reproduced headless). Touching a
   portal (special_surfaces.c, case 6) sets g_HasLevelTransition and switches
   on the portal's path moby, which then carries IN whichever dragon is live
   when it updates. Owned by the nearest dragon, it could update in another
   dragon's pass and wait for him instead: the one who touched it walked into
   the portal as if it were a wall until the other happened to arrive. So
   while a transition is pending, the path moby belongs to the dragon whose
   tick touched the portal (CoopPartyArena.portal_pin). Returns the moby
   index, or -1 when nothing is pinned. */
static int portal_path_moby(unsigned n, int ns, uint8_t* slot) {
    CoopPartyArena* P = coop_party_arena();
    if (*guest32(OP_GADDR_g_HasLevelTransition) == 0) {
        P->portal_pin = 0;
        return -1;
    }
    int s = P->portal_pin - 1;
    uint32_t idx = (uint32_t)*guest32(OP_GADDR_D_8007576C);
    if (s < 0 || s > ns || idx >= 6)
        return -1;
    uint32_t portal = *(uint32_t*)g_api->guest(OP_GADDR_g_Portals + idx * 4);
    int32_t* pp = portal ? (int32_t*)g_api->guest(portal) : NULL;
    if (!pp)
        return -1;
    int32_t path = pp[0x18 / 4];             /* Portal.m_PathMoby */
    if (path < 0 || (unsigned)path >= n)
        return -1;
    *slot = (uint8_t)s;
    return path;
}

/* RIDES (2026-09-13, seen by the user and reproduced headless from their
   savestate). The lift up to the Dark Hollow portal in Artisans carries Spyro
   only while it updates with him live: he sets ControlFlags bit 31 and points
   m_mobyInUseBySpyro (Spyro +0x224) at it, and it drives him up each frame.
   Ownership is by distance, height included, so once the rider was high
   enough a dragon standing near the base became "nearer", the lift moved to
   his pass, and the rider was let go into a glide and fell: 1/2 or 3/4 of
   the way up, depending on where the others stood. Forcing the owner away at
   height 3333 dropped him exactly so. A moby being ridden now belongs to its
   rider; with several riders the current owner keeps it, else the first.

   WHO IS RIDING. The pointer is left stale after a ride, so it needs a second
   sign. The flag alone is not one: Spyro's tick clears it and the lift sets
   it again in its own update, so it is clear whenever ownership is decided.
   State 17 lasts the whole ride; the whirlwind code tests it for "already
   riding" too. Either one counts. */
#define SPYRO_OFF_CONTROL_FLAGS 0x1F4
#define SPYRO_OFF_STATE_M       0x078
#define SPYRO_OFF_MOBY_IN_USE   0x224
#define CONTROL_SCRIPTED        0x80000000u
#define SPYRO_STATE_RIDING      17

static int ridden_moby(const uint8_t* spyro, uint32_t mobys_vaddr, unsigned n) {
    uint32_t flags = *(const uint32_t*)(spyro + SPYRO_OFF_CONTROL_FLAGS);
    uint32_t use   = *(const uint32_t*)(spyro + SPYRO_OFF_MOBY_IN_USE);
    int32_t  state = *(const int32_t*)(spyro + SPYRO_OFF_STATE_M);
    if (!((flags & CONTROL_SCRIPTED) || state == SPYRO_STATE_RIDING) || use < mobys_vaddr)
        return -1;
    uint32_t idx = (use - mobys_vaddr) / sizeof(Moby);
    if ((use - mobys_vaddr) % sizeof(Moby) != 0 || idx >= n)
        return -1;
    return (int)idx;
}

static unsigned assign_mobys(CoopMobyArena* M, Moby* mobys, uint32_t mobys_vaddr) {
    int ns = coop_seeded_shadows();
    const int32_t* pos[COOP_MAX_PLAYERS];
    uint32_t sparx_of[COOP_MAX_PLAYERS];
    pos[0] = guest32(OP_GADDR_g_Spyro + SPYRO_OFF_POSITION);
    sparx_of[0] = *(uint32_t*)g_api->guest(OP_GADDR_g_Sparx);
    for (int k = 1; k <= ns; k++) {
        CoopShadowView v = coop_shadow(k);
        pos[k] = (const int32_t*)(v.spyro + SPYRO_OFF_POSITION);
        sparx_of[k] = *v.sparx;
    }
    unsigned n;

    /* A new level gets a fresh table, so every moby starts with its truly
       nearest dragon instead of inheriting player 1 (BUGS.md A3). */
    CoopExtraArena* X = coop_extra_arena();
    if (X->owner_level != coop_level_id()) {
        memset(M->owner, OWNER_NEW, sizeof M->owner);
        X->owner_level = coop_level_id();
    }

    /* Switch owner only when another dragon is this much closer. 25 is the
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
    int64_t pod_d[POD_COUNT][COOP_MAX_PLAYERS];
    int     pod_first[POD_COUNT];
    for (int p = 0; p < POD_COUNT; p++) {
        for (int s = 0; s < COOP_MAX_PLAYERS; s++)
            pod_d[p][s] = INT64_MAX;
        pod_first[p] = -1;
    }
    for (unsigned i = 0; i < n; i++) {
        if (pod_of[i] < 0)
            continue;
        int p = pod_of[i];
        for (int s = 0; s <= ns; s++) {
            int64_t d = manhattan(pos[s], &mobys[i].m_Position);
            if (d < pod_d[p][s]) pod_d[p][s] = d;
        }
        if (pod_first[p] < 0) pod_first[p] = (int)i;
    }

    /* ---- one owner per group, sticky through its first member's last
       owner: every member shares an owner after this, so that value IS the
       group's previous owner. ---- */
    uint8_t pod_owner[POD_COUNT];
    for (int p = 0; p < POD_COUNT; p++) {
        if (pod_first[p] < 0)
            continue;
        pod_owner[p] = decide_owner(M->owner[pod_first[p]], pod_d[p], ns, keep);
    }

    uint8_t pin_slot = 0;
    int pin_moby = portal_path_moby(n, ns, &pin_slot);
    if (pin_moby >= 0 && pod_of[pin_moby] >= 0)
        pod_owner[pod_of[pin_moby]] = pin_slot;  /* its whole group goes with it */

    int ride[COOP_MAX_PLAYERS];
    ride[0] = ridden_moby(guest8(OP_GADDR_g_Spyro), mobys_vaddr, n);
    for (int k = 1; k <= ns; k++)
        ride[k] = ridden_moby(coop_shadow(k).spyro, mobys_vaddr, n);
    for (int k = ns + 1; k < COOP_MAX_PLAYERS; k++)
        ride[k] = -1;
    /* Each ridden moby's rider: its previous owner if he is one, else the
       first. Settled before the loop so a podmate earlier in the array
       follows it too. */
    int rider_of[COOP_MAX_PLAYERS];
    for (int k = 0; k <= ns; k++) {
        rider_of[k] = -1;
        if (ride[k] < 0)
            continue;
        int r = -1;
        for (int j = 0; j <= ns; j++)
            if (ride[j] == ride[k] && (r < 0 || j == M->owner[ride[k]]))
                r = j;
        rider_of[k] = r;
        if (pod_of[ride[k]] >= 0)
            pod_owner[pod_of[ride[k]]] = (uint8_t)r;
    }

    /* ---- assign every moby ---- */
    unsigned pod_members = 0;
    for (unsigned i = 0; i < n; i++) {
        int8_t state = (int8_t)mobys[i].m_State;
        if (state < 0) {
            M->owner[i] = OWNER_DEAD;        /* dead slot: never filtered */
            continue;
        }
        uint8_t prev = M->owner[i];
        uint32_t addr = mobys_vaddr + i * sizeof(Moby);

        if (pod_of[i] >= 0) {
            /* The group's owner wins, even for a Sparx: the builder would pull
               a split group into several passes whatever we decide per moby. */
            M->owner[i] = pod_owner[pod_of[i]];
            pod_members++;
        } else {
            int follows = -1;                /* a dragon's own Sparx stays his */
            for (int s = 0; s <= ns; s++)
                if (sparx_of[s] != 0 && addr == sparx_of[s])
                    follows = s;
            if (follows >= 0) {
                M->owner[i] = (uint8_t)follows;
            } else {
                int64_t d[COOP_MAX_PLAYERS];
                for (int s = 0; s <= ns; s++)
                    d[s] = manhattan(pos[s], &mobys[i].m_Position);
                M->owner[i] = decide_owner(prev, d, ns, keep);
            }
        }
        if ((int)i == pin_moby)
            M->owner[i] = pin_slot;

        for (int k = 0; k <= ns; k++)         /* see RIDES */
            if (ride[k] == (int)i)
                M->owner[i] = (uint8_t)rider_of[k];
        if (prev < COOP_MAX_PLAYERS && M->owner[i] != prev)
            g_stats.owner_flips++;
    }
    g_stats.pod_members = pod_members;
    return n;
}

/* ------------------------------------------------------------------------
 * The extra dragons' Sparx: Spyromain's sp2x2_rayz, ported via
 * Sp1x2P2SparxKeep, now kept per shadow slot.
 *
 * v4 of the PS1 bookkeeping, after three versions that orphaned or
 * duplicated dragonflies. Only a change to a DIFFERENT LIVING g_Sparx is a
 * level rebuild, and it forgets every shadow's fly; a null is a death
 * transient, and theirs are fine. Called with that shadow live, so the spawn
 * reads his Spyro if it reads one.
 * ---------------------------------------------------------------------- */
static void shadow_sparx_keep(CPUState* cpu, CoopMobyArena* M, int slot) {
    uint32_t sparx1 = *(uint32_t*)g_api->guest(OP_GADDR_g_Sparx);
    uint32_t* mine = coop_shadow(slot).sparx;

    if (sparx1 != 0 && sparx1 != M->sparx1_seen) {
        M->sparx1_seen = sparx1;             /* real rebuild: old world gone */
        for (int k = 1; k <= COOP_MAX_SHADOWS; k++)
            *coop_shadow(k).sparx = 0;
        M->sparx_spawns_level = 0;
    }
    if (*mine != 0) {
        Moby* fly = (Moby*)g_api->guest(*mine);
        if (!fly || *mine == sparx1 || (int8_t)fly->m_State < 0)
            *mine = 0;                       /* his fly died, or was reused */
    }
    /* g_Sparx == 0 means this level has no Sparx at all; follow suit. And a
       dragon with no health has no Sparx in retail: respawning one would only
       lose it again, and burn the spawn cap doing so. */
    if (*mine == 0 && sparx1 != 0 &&
        *guest32(OP_GADDR_g_Spyro + SPYRO_OFF_HEALTH) > 0) {
        if (M->sparx_spawns_level >= SPARX_SPAWN_CAP_PER_LEVEL) {
            if (M->sparx_spawns_level == SPARX_SPAWN_CAP_PER_LEVEL) {
                M->sparx_spawns_level++;     /* log once */
                coop_log(OP_MOD_LOG_WARN,
                           "extra dragons' Sparx spawn cap reached in level %d; not respawning",
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
        *mine = cpu->v0;                     /* a guest Moby*, or 0 on failure */
        if (*mine != 0) {
            M->sparx_spawns_level++;
            g_stats.sparx_spawns++;
            coop_log(OP_MOD_LOG_INFO, "slot %d's Sparx spawned at 0x%08X", slot, *mine);
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
void coop_mobys_identities_swapped(int slot) {
    CoopMobyArena* M = coop_moby_arena();
    uint8_t other = (uint8_t)slot;
    for (unsigned i = 0; i < MOBY_MAX; i++) {
        if (M->owner[i] == 0)          M->owner[i] = other;
        else if (M->owner[i] == other) M->owner[i] = 0;
    }

    uint32_t* g_sparx = (uint32_t*)g_api->guest(OP_GADDR_g_Sparx);
    uint32_t* theirs  = coop_shadow(slot).sparx;
    if (*g_sparx != 0 && *theirs != 0) {
        uint32_t t = *g_sparx;
        *g_sparx = *theirs;
        *theirs = t;
        /* The new g_Sparx is not a level rebuild; tell the detector so. */
        M->sparx1_seen = *g_sparx;
    }

    /* The save-fairy mute names a slot too. Seen 2026-09-13: pressing the key
       while standing on the pedestal let the fairy talk at once. */
    CoopRespawnArena* R = coop_respawn_arena();
    if (R->fairy_mute[0] == 1)             R->fairy_mute[0] = slot + 1;
    else if (R->fairy_mute[0] == slot + 1) R->fairy_mute[0] = 1;
}

/* ------------------------------------------------------------------------
 * The hooks
 * ---------------------------------------------------------------------- */

#define LIST_BUFFER   (OP_GADDR_D_8006FCF4 + 0x400)  /* func_80051FEC's output */
#define LIST_MAX      1024                            /* stop somewhere if unterminated */

/* Which slot's entries survive the next list build: -1 none, else 0..3. Armed
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
                drop = (owner < COOP_MAX_PLAYERS && owner != (uint8_t)keep_owner);
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
    return coop_enabled() && A->ready && coop_seeded_shadows() > 0 &&
           coop_gamestate() == GS_PLAYING &&
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
    g_assigned_n = assign_mobys(M, mobys, mobys_vaddr);
    coop_fairy_mute(0);

    g_filter_owner = 0;                      /* the megafunction GamestateUpdate */
    g_p2_pass_pending = 1;                   /* calls next is player 1's pass */
}

/* Called first thing in the gameplay tick override. Runs each shadow's moby
   pass, in slot order. Returns 1 if a pass started a sequence retail would
   skip Spyro's tick for (fairy, balloonist, flight results, level transition),
   so the caller returns without ticking. */
int coop_mobys_p2_pass(CPUState* cpu) {
    g_filter_owner = -1;                     /* slot 0's pass is over */
    if (!g_p2_pass_pending)
        return 0;
    g_p2_pass_pending = 0;

    CoopArena* A = coop_arena();
    uint32_t update = *(uint32_t*)g_api->guest(OP_GADDR_g_UpdateMoby);
    if (update == 0)
        return 0;

    SavedRegs regs;
    save_regs(cpu, &regs);
    CoopMobyArena* M = coop_moby_arena();
    int n = coop_seeded_shadows();

    for (int k = 1; k <= n; k++) {
        if (!two_pass_allowed(A))
            break;

        uint32_t mobys_vaddr = *(uint32_t*)g_api->guest(OP_GADDR_g_LevelMobys);
        Moby* mobys = (Moby*)g_api->guest(mobys_vaddr);
        g_assigned_n = assign_mobys(M, mobys, mobys_vaddr);  /* a pass may have spawned some */

        /* Camera too: sound attenuates from the camera, so an enemy dying next
           to this dragon has to be heard from his camera, not slot 0's. */
        coop_swap_camera(k);
        coop_swap_spyro(k);
        A->swapped = (uint32_t)k;

        coop_fairy_mute(k);
        shadow_sparx_keep(cpu, M, k);
        load_regs(cpu, &regs);

        /* For this pass, his dragonfly IS "the" Sparx: the megafunction finds
           the followed Sparx through g_Sparx and homes it on the anchor, which
           his tick left as his position. Both go back on every path; g_Sparx
           is our bookkeeping and must never leak. */
        uint32_t* g_sparx = (uint32_t*)g_api->guest(OP_GADDR_g_Sparx);
        uint32_t  mine    = *coop_shadow(k).sparx;
        int32_t*  anchor  = guest32(OP_GADDR_D_80077798);
        uint32_t  sparx1  = *g_sparx;
        int32_t   saved_anchor[3] = { anchor[0], anchor[1], anchor[2] };
        if (mine != 0) {
            *g_sparx = mine;
            memcpy(anchor, guest32(OP_GADDR_g_Spyro + SPYRO_OFF_POSITION), 12);
        }

        g_filter_owner = k;
        g_api->call(cpu, update);            /* the level's megafunction, his pass */
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
               global sequence with his Spyro and camera live. Leave them live
               so the sequence has its player, exactly as the tick does. */
            if (gs == 4 || gs == 5) {
                A->ready = 0;
                g_stats.deaths++;
            } else {
                CoopPartyArena* P = coop_party_arena();
                int32_t t = P->person[0];
                P->person[0] = P->person[k];
                P->person[k] = t;
                A->handover = (uint32_t)k;
                g_stats.handovers++;
            }
            /* GamestateUpdate skips Spyro's tick for these after a moby pass. */
            return gs == 11 || gs == 12 || gs == 7 || gs == 1;
        }

        coop_swap_spyro(k);
        coop_swap_camera(k);
    }
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
