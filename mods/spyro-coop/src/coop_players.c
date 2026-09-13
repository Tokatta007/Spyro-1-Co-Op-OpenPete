/**
 * @file coop_players.c
 * @brief Player 2: his state, and the two overrides that run him.
 *
 * THE TECHNIQUE, unchanged from the PS1 mod and from Spyro2x2 before it.
 * The engine is never taught about a second character. Each frame, Spyro's
 * own tick and the camera update run once for player 1, then again with
 * player 2's state exchanged into the same globals, then exchanged back. The
 * game's own movement, collision, animation and camera logic do the work both
 * times; none of it is reimplemented here.
 *
 * WHAT CHANGED FROM THE PS1 BUILD. There it was 24 patched instructions and
 * 11 KB of BIOS scratch RAM. Here it is two overrides that call base() twice,
 * and one arena allocation. Every byte-golf compromise is gone, and the logic
 * is otherwise translated as directly as possible, so that a bug here has a
 * PS1 counterpart to compare against.
 *
 * PHASE A LIMITS, deliberate:
 *   - player 2 borrows player 1's input (no second controller yet, B1)
 *   - drawing him is in coop_draw.c; enemies and Sparx are in coop_mobys.c
 *   - no individual respawn yet (phase B, last)
 */

#include "coop.h"

/* ------------------------------------------------------------------------
 * Swap tables. Retail addresses by SDK name; docs/PORT-INVENTORY.md §3.
 * ---------------------------------------------------------------------- */

typedef struct { uint32_t addr; uint32_t size; } Region;

/* Spyro's state is NOT contiguous in Spyro 1. It is three structs plus a
   dozen loose globals, and every omission on PS1 produced a distinct bug:
   walking in place, an endless jump and glide loop, the flame drawn at the
   wrong dragon. Complete the table rather than patching symptoms. */
static const Region k_spyro_regions[] = {
    { OP_GADDR_g_Spyro,                  676 },
    { OP_GADDR_g_SpyroFlame,             312 },
    { OP_GADDR_D_8007AA10,                40 },  /* drop shadow */
    { OP_GADDR_g_SurfaceBelowFlags,        4 },
    { OP_GADDR_D_80075788,                 4 },  /* idle anim timeout */
    { OP_GADDR_D_80075804,                 4 },  /* contact actor */
    { OP_GADDR_g_CollisionTriangleIndex,   4 },
    { OP_GADDR_g_IsSpyroHidden,            4 },
    { OP_GADDR_D_800758A0,                 8 },  /* turn accum, flame timer */
    { OP_GADDR_D_800758C0,                 4 },  /* fall reference Z */
    { OP_GADDR_D_80075960,                 4 },  /* pitch accum */
    { OP_GADDR_D_80075970,                 4 },  /* idle anim cursor */
    { OP_GADDR_g_CollisionPoint,           4 },
    { OP_GADDR_g_DragonCutscene + 0x8C,    4 },  /* gem pickup mirror actor */
    { OP_GADDR_g_CollisionNormal,         16 },
};

/* The four camera-module statics outside g_Camera. They are reached through
   $gp, which is why a first scan on PS1 missed them, and one of them (the
   forced-to-destination flag) is why player 2's camera never zoomed back on
   a hit until they were swapped too. */
static const uint32_t k_camera_extra[CAMERA_EXTRA_COUNT] = {
    OP_GADDR_D_800756B8,
    OP_GADDR_D_80075894,
    OP_GADDR_D_80075924,
    OP_GADDR_D_80075938,
};

static const Region k_pad_regions[] = {
    { OP_GADDR_g_Pad,        0xA4 },
    { OP_GADDR_g_PadBackup,  0xA4 },  /* per player: a lockout stashes here */
    { OP_GADDR_g_PadSwapFlag,   1 },
    { OP_GADDR_g_ActivePad,     4 },  /* how Spyro's code actually reads input */
};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* Offsets of g_Pad and g_ActivePad inside the pad shadow, in table order. */
#define PAD_SHADOW_PAD        0
#define PAD_SHADOW_ACTIVEPAD  (0xA4 + 0xA4 + 1)

/* Exchange (or with exchange == 0, copy live into) a table of regions and a
   packed shadow. One walker for both jobs so seeding and swapping cannot
   disagree about layout. */
static void walk(const Region* table, unsigned n, uint8_t* shadow, int exchange) {
    for (unsigned r = 0; r < n; r++) {
        uint8_t* live = guest8(table[r].addr);
        for (uint32_t i = 0; i < table[r].size; i++) {
            uint8_t t = live[i];
            if (exchange)
                live[i] = shadow[i];
            shadow[i] = t;
        }
        shadow += table[r].size;
    }
}

static unsigned table_bytes(const Region* table, unsigned n) {
    unsigned total = 0;
    for (unsigned r = 0; r < n; r++)
        total += table[r].size;
    return total;
}

/* EITHER BOTH PLAYERS' STATE IS EXCHANGED, OR NEITHER. On PS1 a guard on one
   swap and not the other left the live camera and live Spyro belonging to
   different players, and the camera measured itself against the wrong
   dragon. Every swap below shares the one ready guard. */
static void swap_spyro(CoopArena* A) {
    if (A->ready)
        walk(k_spyro_regions, COUNT(k_spyro_regions), A->spyro, 1);
}

/* For the draw hook: drawing needs Spyro's state and nothing else. */
void coop_swap_spyro(void) {
    swap_spyro(coop_arena());
}

static void swap_camera(CoopArena* A) {
    if (!A->ready)
        return;
    uint8_t* cam = guest8(OP_GADDR_g_Camera);
    for (unsigned i = 0; i < CAMERA_STRUCT_BYTES; i++) {
        uint8_t t = cam[i]; cam[i] = A->camera[i]; A->camera[i] = t;
    }
    for (unsigned i = 0; i < CAMERA_EXTRA_COUNT; i++) {
        int32_t* l = guest32(k_camera_extra[i]);
        int32_t t = *l; *l = A->camera_extra[i]; A->camera_extra[i] = t;
    }
}

static void swap_pad(CoopArena* A) {
    if (A->ready)
        walk(k_pad_regions, COUNT(k_pad_regions), A->pad, 1);
}

/* The three sets touch no common memory (checked on PS1: the nearest
   approach is g_CollisionNormal ending at 0x80077377 and g_Pad starting at
   0x80077378), so their order is irrelevant. */
static void swap_all(CoopArena* A) {
    swap_camera(A);
    swap_spyro(A);
    swap_pad(A);
    A->swapped ^= 1u;
}

/* ------------------------------------------------------------------------
 * Small helpers
 * ---------------------------------------------------------------------- */

static int32_t gamestate(void) { return *guest32(OP_GADDR_g_Gamestate); }
static int32_t level_id(void)  { return *guest32(OP_GADDR_g_LevelId); }
int32_t coop_gamestate(void)   { return gamestate(); }
int32_t coop_level_id(void)    { return level_id(); }
void coop_swap_camera(void)    { swap_camera(coop_arena()); }
static int32_t* live_position(void) {
    return guest32(OP_GADDR_g_Spyro + SPYRO_OFF_POSITION);
}

void coop_p2_position(int32_t out[3]) {
    memcpy(out, coop_arena()->spyro + SPYRO_OFF_POSITION, 12);
}

/* THE NULL FOCUS, found on PS1 on 2026-09-02 after twelve misses.
   func_8003FE40, reached from Spyro's tick, copies the pointer at
   g_Spyro + 0x21C straight into g_Camera.m_Focus without checking it, and
   nothing in the main executable ever initialises that field. A null makes
   the camera follow address zero. The game's own loaders assign
   &g_Spyro.m_Position to m_Focus, which is correct for whichever player is
   ticking because the struct address is fixed while its contents swap. */
static void arm_script_focus(void) {
    uint32_t* focus = (uint32_t*)g_api->guest(OP_GADDR_g_Spyro + SPYRO_OFF_SCRIPT_FOCUS);
    if (*focus == 0)
        *focus = OP_GADDR_g_Spyro + SPYRO_OFF_POSITION;  /* a GUEST address */
}

/* ------------------------------------------------------------------------
 * Seeding
 * ---------------------------------------------------------------------- */

/* Out along the live dragon's wing line. On PS1 this function was shared
   with the portal fly-in draw so the two could not place him on opposite
   sides; it will be again once there is a draw. Heading is (cos, -sin),
   established by observation, so the wing line is (sin, cos). */
static void formation_offset(int32_t out[3]) {
    int32_t yaw = *guest32(OP_GADDR_g_Spyro + SPYRO_OFF_YAW);
    int     b   = (yaw >> 4) & 0xFF;               /* 0x1000 per turn -> 256 */
    int16_t* cos8 = (int16_t*)g_api->guest(OP_GADDR_D_8006CC78);  /* SIGNED */
    int32_t c = cos8[b];
    int32_t s = cos8[(b - 64) & 0xFF];              /* sin = cos(yaw - 90) */
    out[0] = (s * P2_START_OFFSET) >> 12;
    out[1] = (c * P2_START_OFFSET) >> 12;
    out[2] = 0;
}

static void resample_teleport(CoopArena* A) {
    memcpy(A->tp_sample, live_position(), sizeof A->tp_sample);
}

/* Copy player 1 into player 2, then step him out to the side. */
static void seed_player2(CoopArena* A) {
    walk(k_spyro_regions, COUNT(k_spyro_regions), A->spyro, 0);
    memcpy(A->camera, guest8(OP_GADDR_g_Camera), CAMERA_STRUCT_BYTES);
    for (unsigned i = 0; i < CAMERA_EXTRA_COUNT; i++)
        A->camera_extra[i] = *guest32(k_camera_extra[i]);  /* or he starts with garbage */
    walk(k_pad_regions, COUNT(k_pad_regions), A->pad, 0);

    A->last_level = level_id();
    A->handover   = 0;
    A->swapped    = 0;
    A->ready      = 1;

    /* Swap him in and move the live position, rather than hunting for the
       right bytes in the packed shadow. */
    int32_t off[3];
    swap_spyro(A);
    formation_offset(off);
    live_position()[0] += off[0];
    live_position()[1] += off[1];
    swap_spyro(A);

    g_stats.seeds++;
    coop_log(OP_MOD_LOG_INFO,
               "player 2 seeded in level %d at offset (%d, %d)",
               A->last_level, off[0], off[1]);
}

/* ------------------------------------------------------------------------
 * Handover and teleport detection, ported from Sp1x2HandoverResume.
 * Runs before anything else in the gameplay tick.
 * ---------------------------------------------------------------------- */
static void handover_resume(CoopArena* A);
void coop_handover_resume(void) { handover_resume(coop_arena()); }

static void handover_resume(CoopArena* A) {
    int32_t gs = gamestate();
    if (gs != GS_PLAYING)
        A->last_seq = gs;

    /* A level RESTART is neither a level change nor a death, so it is caught
       the way every restart path shows itself: the live dragon moving further
       in one frame than any movement can (0x4000 is far beyond supercharge).

       WHILE A HANDOVER IS PENDING THE LIVE DRAGON IS PLAYER 2, so comparing
       him against a player 1 sample is a false teleport by construction.
       That was the PS1 balloonist bug. Skip detection in that window. */
    int jumped = 0;
    if (!A->handover) {
        int32_t* pos = live_position();
        for (int k = 0; k < 3; k++) {
            int32_t d = pos[k] - A->tp_sample[k];
            if (d < 0) d = -d;
            if (d > 0x4000) jumped = 1;
            A->tp_sample[k] = pos[k];
        }
    }
    if (jumped && A->ready) {
        /* A jump right after a dragon rescue, fairy prompt or balloonist is
           the sequence repositioning the live dragon, not a restart. Consumed
           on use so a stale value cannot mask a later real restart. */
        int32_t seq = A->last_seq;
        A->last_seq = 0;
        if (seq != 8 && seq != 11 && seq != 12) {
            A->handover = 0;
            A->ready    = 0;
            g_stats.teleports++;
            return;
        }
    }

    if (!A->handover || gs != GS_PLAYING)
        return;

    if (A->last_level != level_id()) {
        A->ready = 0;
    } else {
        swap_spyro(A);
        swap_camera(A);
        /* The swap-back IS a teleport as far as the detector is concerned.
           Move its sample in the same breath, or the next check reseeds. */
        resample_teleport(A);
    }
    A->handover = 0;
}

/* ------------------------------------------------------------------------
 * Body separation, ported from Sp1x2SeparatePlayers.
 *
 * Spyro is not a moby, so none of the game's actor collision applies between
 * the two dragons. Our own design on PS1, since Spyro2x2 let his dragons pass
 * through each other: after both have ticked, if they overlap horizontally,
 * push each half the overlap apart along the line between them.
 *   - horizontal only (z is up): pushing vertically launches or buries them
 *   - position, not velocity: a velocity nudge felt mushy and fought physics
 *   - never in flight levels, where they fly side by side constantly; the push
 *     fighting flight physics every frame was what damped vertical steering
 * ---------------------------------------------------------------------- */
#define BODY_RADIUS 0x1A0  /* 416 units centre to centre */
#define BODY_HEIGHT 0x2A0  /* ignore each other beyond this height gap */

static void separate_players(CoopArena* A) {
    if (!A->ready || gamestate() != GS_PLAYING || *guest32(OP_GADDR_g_IsFlightLevel) != 0)
        return;

    int32_t* p1 = live_position();
    int32_t* p2 = (int32_t*)(A->spyro + SPYRO_OFF_POSITION);
    int32_t d[3] = { p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2] };

    /* Cheap rejects first, which also keep the arithmetic in range. */
    if (d[0] > BODY_RADIUS || d[0] < -BODY_RADIUS ||
        d[1] > BODY_RADIUS || d[1] < -BODY_RADIUS ||
        d[2] > BODY_HEIGHT || d[2] < -BODY_HEIGHT)
        return;

    int32_t dist = (int32_t)isqrt64((uint64_t)((int64_t)d[0] * d[0] + (int64_t)d[1] * d[1]));
    if (dist >= BODY_RADIUS)
        return;
    g_stats.pushes++;
    if (dist <= 0) {
        /* Exactly coincident: pick an axis so they cannot lock together. */
        p1[0] -= BODY_RADIUS / 2;
        p2[0] += BODY_RADIUS / 2;
        return;
    }
    int32_t overlap = BODY_RADIUS - dist;
    for (int i = 0; i < 2; i++) {
        int32_t push = (d[i] * overlap) / (dist * 2);
        p1[i] -= push;
        p2[i] += push;
    }
}

/* ------------------------------------------------------------------------
 * The dev view swap: press the bound key to trade identities, so the camera
 * jumps to the other dragon. The only way to SEE player 2 before there is a
 * second render pass. Reading a host key in a tick hook makes a replay
 * diverge, which the SDK allows and warns about; this is a development aid.
 * ---------------------------------------------------------------------- */
static void maybe_swap_view(CoopArena* A) {
    uint32_t down = g_api->binding_down(g_self, "swap_view") ? 1u : 0u;
    int pressed = down && !A->view_key_down;
    A->view_key_down = down;
    if (!pressed || !A->ready || A->handover || gamestate() != GS_PLAYING)
        return;

    swap_all(A);
    A->swapped = 0;            /* identities traded, not mid-override */
    resample_teleport(A);      /* anything that moves a live dragon does this */
    g_stats.view_swaps++;
}

/* ------------------------------------------------------------------------
 * Override: Spyro's tick. Ported from Sp1x2TickPlayer2Spyro.
 * ---------------------------------------------------------------------- */
static void on_spyro_tick(CPUState* cpu) {
    if (cpu->ra != RA_GAMEPLAY_SPYRO_TICK) {
        g_stats.tick_other++;
        g_stats.tick_other_ra = cpu->ra;
        g_api->base(cpu);
        return;
    }
    g_stats.tick_gameplay++;

    CoopArena* A = coop_arena();
    if (!coop_enabled()) {
        /* Switched off from the Mods panel. A settings change reloads the mod
           with its arena intact, so stand player 2 down here too, or a
           pending handover would leave identities crossed for good. */
        if (A->ready)
            coop_players_disable();
        g_api->base(cpu);
        return;
    }

    handover_resume(A);

    /* SNAPSHOT BEFORE THE FIRST CONSUMER. Player 1's tick spends the substep
       budget, and may reset g_Pad during an input lockout. Saving either
       after his tick was a PS1 bug twice over: it restored an empty value. */
    A->substeps_owed = *guest32(OP_GADDR_g_UnprocessedFrames);
    uint8_t  p1_pad[0xA4];
    uint32_t p1_active_pad;
    memcpy(p1_pad, guest8(OP_GADDR_g_Pad), sizeof p1_pad);
    p1_active_pad = *(uint32_t*)g_api->guest(OP_GADDR_g_ActivePad);

    SavedRegs regs;
    save_regs(cpu, &regs);

    arm_script_focus();
    g_api->base(cpu);                                  /* player 1 */

    int32_t gs = gamestate();
    if (gs == 4 || gs == 5) {
        A->ready = 0;                                  /* death: both respawn */
        g_stats.deaths++;
        return;
    }
    if (gs != GS_PLAYING)
        return;                                        /* he holds still */

    if (!A->ready) {
        seed_player2(A);
        resample_teleport(A);
        return;
    }
    if (A->last_level != level_id()) {
        A->ready = 0;                                  /* reseed next frame */
        g_stats.level_reseeds++;
        return;
    }

    /* PHASE A INPUT: hand player 2 exactly what player 1 saw. g_PadBackup and
       the swap flag stay his own. When a real second controller arrives, this
       block is all that changes. */
    memcpy(A->pad + PAD_SHADOW_PAD, p1_pad, sizeof p1_pad);
    memcpy(A->pad + PAD_SHADOW_ACTIVEPAD, &p1_active_pad, 4);

    swap_all(A);
    {
        int32_t* anchor   = guest32(OP_GADDR_D_80077798);
        int32_t  saved_anchor[3] = { anchor[0], anchor[1], anchor[2] };
        int32_t* substeps = guest32(OP_GADDR_g_UnprocessedFrames);
        int32_t  after_p1 = *substeps;

        *substeps = A->substeps_owed;                  /* same budget as P1 */
        arm_script_focus();
        load_regs(cpu, &regs);
        g_api->base(cpu);                              /* player 2 */
        g_stats.p2_ticks++;

        *substeps = after_p1;                          /* consumed once */
        if (gamestate() == GS_PLAYING) {
            /* followers (Sparx) track player 1, not the midpoint */
            anchor[0] = saved_anchor[0];
            anchor[1] = saved_anchor[1];
            anchor[2] = saved_anchor[2];
        }
    }

    if (gamestate() != GS_PLAYING) {
        /* Player 2's tick started a GLOBAL sequence: death, a portal, a dragon
           rescue, the balloonist. The sequence wrote its setup into the LIVE
           Spyro and camera, which are his. Swapping those back strands it and
           crashed on PS1. Leave them live; swap only the pad back. */
        swap_pad(A);
        A->swapped = 0;
        int32_t g2 = gamestate();
        if (g2 == 4 || g2 == 5) {
            A->ready = 0;
            g_stats.deaths++;
        } else {
            A->handover = 1;
            g_stats.handovers++;
        }
        return;
    }

    swap_all(A);

    /* Both dragons have moved this frame: resolve any overlap. */
    separate_players(A);
    maybe_swap_view(A);
}

/* ------------------------------------------------------------------------
 * Override: the camera update. Ported from Sp1x2UpdateCameras.
 * ---------------------------------------------------------------------- */
static void on_camera_update(CPUState* cpu) {
    if (cpu->ra != RA_GAMEPLAY_CAMERA) {
        g_stats.camera_other++;
        g_stats.camera_other_ra = cpu->ra;
        g_api->base(cpu);
        coop_mobys_track();
        coop_publish_status();
        return;
    }
    g_stats.camera_gameplay++;

    CoopArena* A = coop_arena();
    SavedRegs regs;
    save_regs(cpu, &regs);

    g_api->base(cpu);                                  /* player 1 */

    if (coop_enabled() && A->ready) {
        swap_all(A);
        load_regs(cpu, &regs);
        g_api->base(cpu);                              /* player 2 */
        g_stats.p2_cameras++;

        /* Consume his edge latches after his last reader this frame, or they
           accumulate. Harmless while his input is copied fresh each frame,
           and correct once it is not. */
        uint32_t* pad = (uint32_t*)g_api->guest(OP_GADDR_g_Pad);
        pad[0] = 0;  /* m_Down */
        pad[1] = 0;  /* m_Released */

        swap_all(A);
    }

    coop_pad_sample();
    coop_mobys_track();
    coop_publish_status();
}

/* ------------------------------------------------------------------------
 * Install and disable
 * ---------------------------------------------------------------------- */

int coop_players_install(void) {
    /* A wrong table size would swap the wrong memory silently. Fail loudly. */
    if (table_bytes(k_spyro_regions, COUNT(k_spyro_regions)) != SPYRO_STATE_BYTES ||
        table_bytes(k_pad_regions, COUNT(k_pad_regions)) != PAD_STATE_BYTES) {
        coop_log(OP_MOD_LOG_ERROR,
                   "swap table size mismatch: spyro %u, pad %u",
                   table_bytes(k_spyro_regions, COUNT(k_spyro_regions)),
                   table_bytes(k_pad_regions, COUNT(k_pad_regions)));
        return 1;
    }
    if (g_api->override_name(g_self, "func_8004A200", on_spyro_tick) != 0) {
        coop_log(OP_MOD_LOG_ERROR, "could not override Spyro's tick");
        return 1;
    }
    if (g_api->override_name(g_self, "CameraUpdate", on_camera_update) != 0) {
        coop_log(OP_MOD_LOG_ERROR, "could not override CameraUpdate");
        return 1;
    }
    return 0;
}

/* Called when the mod is disabled from the Mods panel. The engine never
   rewinds guest state, so leave the game as stock would: if a handover left
   player 2's identity live, trade back, then stand player 2 down. */
void coop_players_disable(void) {
    CoopArena* A = coop_arena();
    if (A->ready && A->handover) {
        swap_spyro(A);
        swap_camera(A);
    }
    A->handover = 0;
    A->swapped  = 0;
    A->ready    = 0;
}
