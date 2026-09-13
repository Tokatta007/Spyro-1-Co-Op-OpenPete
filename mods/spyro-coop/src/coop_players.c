/**
 * @file coop_players.c
 * @brief The extra dragons: their state, and the two overrides that run them.
 *
 * THE TECHNIQUE, unchanged from the PS1 mod and from Spyro2x2 before it.
 * The engine is never taught about another character. Each frame, Spyro's
 * own tick and the camera update run once for player 1, then again for each
 * extra dragon with that dragon's state exchanged into the same globals, then
 * exchanged back. The game's own movement, collision, animation and camera
 * logic do the work every time; none of it is reimplemented here.
 *
 * SLOTS (2026-09-13, up to four players). Slot 0 is the live dragon between
 * overrides, the one the camera shows. Slots 1..3 are shadows: packed copies
 * of the same globals. Shadow slot 1 lives in CoopArena, where player 2 always
 * lived, and slots 2 and 3 in CoopPartyArena. Which PLAYER is in which slot is
 * CoopPartyArena.person: normally slot n holds player n+1, and the view key
 * and handovers trade entries. Anything keyed to a person rather than a
 * position, such as colour, asks coop_physical_player.
 *
 * WHAT CHANGED FROM THE PS1 BUILD. There it was 24 patched instructions and
 * 11 KB of BIOS scratch RAM. Here it is overrides that call base() once per
 * dragon, and arena allocations. The logic is otherwise translated as directly
 * as possible, so that a bug here has a PS1 counterpart to compare against.
 *
 * PHASE A LIMITS, deliberate:
 *   - the extra dragons borrow player 1's input (no second controller yet, B1)
 *   - drawing them is in coop_draw.c; enemies and Sparx are in coop_mobys.c
 *   - individual death and respawn are in coop_respawn.c
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

/* ------------------------------------------------------------------------
 * Slots
 * ---------------------------------------------------------------------- */

CoopShadowView coop_shadow(int slot) {
    CoopShadowView v;
    if (slot <= 1) {
        CoopArena* A = coop_arena();
        CoopMobyArena* M = coop_moby_arena();
        v.spyro = A->spyro; v.camera = A->camera;
        v.camera_extra = A->camera_extra; v.pad = A->pad;
        v.sparx = &M->p2_sparx;
    } else {
        CoopPartyArena* P = coop_party_arena();
        CoopShadow* s = &P->extra[slot - 2];
        v.spyro = s->spyro; v.camera = s->camera;
        v.camera_extra = s->camera_extra; v.pad = s->pad;
        v.sparx = &P->sparx[slot - 2];
    }
    return v;
}

int coop_seeded_shadows(void) {
    if (!coop_arena()->ready)
        return 0;
    int n = coop_party_arena()->shadows;
    return (n < 1) ? 0 : (n > COOP_MAX_SHADOWS) ? COOP_MAX_SHADOWS : n;
}

/* EITHER A DRAGON'S STATE IS EXCHANGED WHOLE, OR NOT AT ALL. On PS1 a guard on
   one swap and not the other left the live camera and live Spyro belonging to
   different players, and the camera measured itself against the wrong
   dragon. Every swap below shares the one ready guard. */
static void swap_spyro(CoopArena* A, int slot) {
    if (A->ready)
        walk(k_spyro_regions, COUNT(k_spyro_regions), coop_shadow(slot).spyro, 1);
}

/* For the draw and moby hooks. */
void coop_swap_spyro(int slot) {
    swap_spyro(coop_arena(), slot);
}

static void swap_camera(CoopArena* A, int slot) {
    if (!A->ready)
        return;
    CoopShadowView v = coop_shadow(slot);
    uint8_t* cam = guest8(OP_GADDR_g_Camera);
    for (unsigned i = 0; i < CAMERA_STRUCT_BYTES; i++) {
        uint8_t t = cam[i]; cam[i] = v.camera[i]; v.camera[i] = t;
    }
    for (unsigned i = 0; i < CAMERA_EXTRA_COUNT; i++) {
        int32_t* l = guest32(k_camera_extra[i]);
        int32_t t = *l; *l = v.camera_extra[i]; v.camera_extra[i] = t;
    }
}

static void swap_pad(CoopArena* A, int slot) {
    if (A->ready)
        walk(k_pad_regions, COUNT(k_pad_regions), coop_shadow(slot).pad, 1);
}

/* The three sets touch no common memory (checked on PS1: the nearest
   approach is g_CollisionNormal ending at 0x80077377 and g_Pad starting at
   0x80077378), so their order is irrelevant. */
static void swap_all(CoopArena* A, int slot) {
    swap_camera(A, slot);
    swap_spyro(A, slot);
    swap_pad(A, slot);
    A->swapped = A->swapped ? 0u : (uint32_t)slot;
}

/* Slot 0 and `slot` have traded whose dragon they hold. */
static void trade_persons(int slot) {
    CoopPartyArena* P = coop_party_arena();
    int32_t t = P->person[0];
    P->person[0] = P->person[slot];
    P->person[slot] = t;
}

static void reset_persons(void) {
    CoopPartyArena* P = coop_party_arena();
    for (int i = 0; i < COOP_MAX_PLAYERS; i++)
        P->person[i] = i;
}

/* ------------------------------------------------------------------------
 * Small helpers
 * ---------------------------------------------------------------------- */

static int32_t gamestate(void) { return *guest32(OP_GADDR_g_Gamestate); }
static int32_t level_id(void)  { return *guest32(OP_GADDR_g_LevelId); }
int32_t coop_gamestate(void)   { return gamestate(); }
int32_t coop_level_id(void)    { return level_id(); }
void coop_swap_camera(int slot) { swap_camera(coop_arena(), slot); }
static int32_t* live_position(void) {
    return guest32(OP_GADDR_g_Spyro + SPYRO_OFF_POSITION);
}

/* WHICH DRAGON IS IN WHICH SLOT (see SLOTS above). A table rather than a
   formula since there are four: the view key and a handover each trade one
   shadow slot with slot 0. (Seen 2026-09-13, before there was any mapping:
   with player 1 green and player 2 red, the camera's dragon was always green,
   and player 2 talked to the balloonist as a green dragon.) */
int coop_physical_player(int slot) {
    if (slot < 0 || slot >= COOP_MAX_PLAYERS)
        return 0;
    int p = coop_party_arena()->person[slot];
    return (p >= 0 && p < COOP_MAX_PLAYERS) ? p : slot;
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

/* Out along the live dragon's wing line. SHARED with the portal fly-in draw
   (coop_draw.c), exactly as on PS1, so the dragons fly in with the spacing
   they are seeded at and do not jump sides when the sequence ends. Heading is
   (cos, -sin), established by observation, so the wing line is (sin, cos).
   Slot 1 flies on his right, slot 2 on his left, slot 3 outside slot 1. */
void coop_formation_offset(int slot, int32_t out[3]) {
    static const int k_steps[COOP_MAX_PLAYERS] = { 0, 1, -1, 2 };
    int step = (slot >= 0 && slot < COOP_MAX_PLAYERS) ? k_steps[slot] : 1;
    int32_t yaw = *guest32(OP_GADDR_g_Spyro + SPYRO_OFF_YAW);
    int     b   = (yaw >> 4) & 0xFF;               /* 0x1000 per turn -> 256 */
    int16_t* cos8 = (int16_t*)g_api->guest(OP_GADDR_D_8006CC78);  /* SIGNED */
    int32_t c = cos8[b];
    int32_t s = cos8[(b - 64) & 0xFF];              /* sin = cos(yaw - 90) */
    out[0] = (s * P2_START_OFFSET * step) >> 12;
    out[1] = (c * P2_START_OFFSET * step) >> 12;
    out[2] = 0;
}

static void resample_teleport(CoopArena* A) {
    memcpy(A->tp_sample, live_position(), sizeof A->tp_sample);
}

/* Which tick is running, for the death override. Set and cleared around each
   base() call inside one override invocation, so it never outlives a tick and
   needs no place in the arena. */
static int g_in_gameplay_tick;
static int g_ticking_player;
int coop_in_gameplay_tick(void) { return g_in_gameplay_tick; }
int coop_ticking_player(void)   { return g_ticking_player; }

/* THE TELEPORT DETECTOR TRACKS SLOT 0, the live dragon between overrides.
   A respawn moves the dying dragon, so the sample must move with slot 0's
   respawn. It must NOT be moved to a shadow's: the next frame would compare
   slot 0 against that respawn point, read a jump, and reseed everyone as a
   level restart. The PS1 build wrote it for either player, a latent bug
   whenever the respawn point was more than 0x4000 from player 1. */
void coop_resample_teleport(void) {
    if (g_ticking_player == 0)
        resample_teleport(coop_arena());
}

/* EACH EXTRA DRAGON KEEPS HIS OWN HEALTH ACROSS A LEVEL CHANGE. Seeding copies
   slot 0's state into every shadow, health included, and Sparx takes its
   colour from health. So on entering a level player 2's Sparx showed player
   1's health (seen 2026-09-13 in Dark Hollow). Retail carries health between
   levels, so a level change remembers each shadow's before it is stood down,
   and the seed puts it back. A death forgets it: the stock respawn gives
   everyone full health, and a separate respawn never reseeds.

   `live_slot` is the shadow whose state is live (a handover), or 0. */
static void carry_health(CoopArena* A, int live_slot) {
    CoopPartyArena* P = coop_party_arena();
    int n = coop_seeded_shadows();
    for (int k = 1; k <= n; k++) {
        P->health_carry[k][0] = 1;
        P->health_carry[k][1] = (k == live_slot)
            ? *guest32(OP_GADDR_g_Spyro + SPYRO_OFF_HEALTH)
            : *(int32_t*)(coop_shadow(k).spyro + SPYRO_OFF_HEALTH);
    }
    (void)A;
}

static void forget_health(void) {
    CoopPartyArena* P = coop_party_arena();
    for (int k = 0; k < COOP_MAX_PLAYERS; k++)
        P->health_carry[k][0] = 0;
}

/* Copy slot 0 into every wanted shadow, then step each out to his place. */
static void seed_shadows(CoopArena* A) {
    CoopPartyArena* P = coop_party_arena();
    int n = coop_shadow_count();

    for (int k = 1; k <= n; k++) {
        CoopShadowView v = coop_shadow(k);
        walk(k_spyro_regions, COUNT(k_spyro_regions), v.spyro, 0);
        if (P->health_carry[k][0]) {
            *(int32_t*)(v.spyro + SPYRO_OFF_HEALTH) = P->health_carry[k][1];
            P->health_carry[k][0] = 0;
        }
        memcpy(v.camera, guest8(OP_GADDR_g_Camera), CAMERA_STRUCT_BYTES);
        for (unsigned i = 0; i < CAMERA_EXTRA_COUNT; i++)
            v.camera_extra[i] = *guest32(k_camera_extra[i]);  /* or garbage */
        walk(k_pad_regions, COUNT(k_pad_regions), v.pad, 0);
    }

    /* A new level: remember where it was entered, as a fallback respawn point
       for deaths before any checkpoint (coop_respawn.c). A reseed in the same
       level, after a shared death, keeps the capture from its entry. */
    if (A->last_level != level_id())
        coop_capture_spawn();

    A->last_level = level_id();
    A->handover   = 0;
    A->swapped    = 0;
    P->shadows    = n;
    A->ready      = 1;
    reset_persons();                         /* every shadow is a fresh copy of slot 0 */

    /* Swap each in and move the live position, rather than hunting for the
       right bytes in the packed shadow. */
    int32_t off[3];
    for (int k = 1; k <= n; k++) {
        swap_spyro(A, k);
        coop_formation_offset(k, off);
        live_position()[0] += off[0];
        live_position()[1] += off[1];
        swap_spyro(A, k);
    }

    g_stats.seeds++;
    coop_log(OP_MOD_LOG_INFO, "%d extra dragon%s seeded in level %d",
             n, n == 1 ? "" : "s", A->last_level);
}

/* ------------------------------------------------------------------------
 * Handover and teleport detection, ported from Sp1x2HandoverResume.
 * Runs before anything else in the gameplay tick.
 * ---------------------------------------------------------------------- */
static void handover_resume(CoopArena* A);
void coop_handover_resume(void) { handover_resume(coop_arena()); }

/* A shadow's tick or moby pass started a sequence: his state stays live. */
static void begin_handover(CoopArena* A, int slot) {
    A->handover = (uint32_t)slot;
    trade_persons(slot);
    g_stats.handovers++;
}

static void handover_resume(CoopArena* A) {
    int32_t gs = gamestate();
    if (gs != GS_PLAYING)
        A->last_seq = gs;

    /* A level RESTART is neither a level change nor a death, so it is caught
       the way every restart path shows itself: the live dragon moving further
       in one frame than any movement can (0x4000 is far beyond supercharge).

       WHILE A HANDOVER IS PENDING THE LIVE DRAGON IS A SHADOW'S, so comparing
       him against a slot 0 sample is a false teleport by construction.
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
            if (seq == 4 || seq == 5)
                forget_health();             /* a stock respawn: full health for all */
            else
                carry_health(A, 0);
            A->handover = 0;
            A->ready    = 0;
            g_stats.teleports++;
            return;
        }
    }

    if (!A->handover || gs != GS_PLAYING)
        return;

    int slot = (int)A->handover;
    if (A->last_level != level_id()) {
        carry_health(A, slot);               /* identities crossed: he is live */
        A->ready = 0;
    } else {
        swap_spyro(A, slot);
        swap_camera(A, slot);
        trade_persons(slot);
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
 * the dragons. Our own design on PS1, since Spyro2x2 let his dragons pass
 * through each other: after all have ticked, if two overlap horizontally,
 * push each half the overlap apart along the line between them. With more
 * than two, every pair is checked once.
 *   - horizontal only (z is up): pushing vertically launches or buries them
 *   - position, not velocity: a velocity nudge felt mushy and fought physics
 *   - never in flight levels, where they fly side by side constantly; the push
 *     fighting flight physics every frame was what damped vertical steering
 * ---------------------------------------------------------------------- */
#define BODY_RADIUS 0x1A0  /* 416 units centre to centre */
#define BODY_HEIGHT 0x2A0  /* ignore each other beyond this height gap */

static void separate_pair(int32_t* p1, int32_t* p2) {
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

static void separate_players(CoopArena* A) {
    int n = coop_seeded_shadows();
    if (n == 0 || gamestate() != GS_PLAYING || *guest32(OP_GADDR_g_IsFlightLevel) != 0)
        return;

    int32_t* pos[COOP_MAX_PLAYERS];
    pos[0] = live_position();
    for (int k = 1; k <= n; k++)
        pos[k] = (int32_t*)(coop_shadow(k).spyro + SPYRO_OFF_POSITION);
    for (int i = 0; i <= n; i++)
        for (int j = i + 1; j <= n; j++)
            separate_pair(pos[i], pos[j]);
    (void)A;
}

/* ------------------------------------------------------------------------
 * The dev view swap: press the bound key to move the camera to the next
 * player's dragon. The only way to SEE the others before there is a second
 * render pass. Reading a host key in a tick hook makes a replay diverge,
 * which the SDK allows and warns about; this is a development aid.
 * ---------------------------------------------------------------------- */
static void maybe_swap_view(CoopArena* A) {
    uint32_t down = g_api->binding_down(g_self, "swap_view") ? 1u : 0u;
    int pressed = down && !A->view_key_down;
    A->view_key_down = down;
    int n = coop_seeded_shadows();
    if (!pressed || n == 0 || A->handover || gamestate() != GS_PLAYING)
        return;

    /* The slot holding the next player in turn. */
    CoopPartyArena* P = coop_party_arena();
    int want = (P->person[0] + 1) % (n + 1);
    int slot = 1;
    for (int k = 1; k <= n; k++)
        if (P->person[k] == want)
            slot = k;

    swap_all(A, slot);
    A->swapped = 0;            /* identities traded, not mid-override */
    trade_persons(slot);
    resample_teleport(A);      /* anything that moves a live dragon does this */
    coop_mobys_identities_swapped(slot);
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
        coop_tint_state();                   /* a cutscene's tick may have cleared it */
        return;
    }
    g_stats.tick_gameplay++;

    CoopArena* A = coop_arena();
    if (!coop_enabled()) {
        /* Switched to one player. A settings change reloads the mod with its
           arena intact, so stand the others down here too, or a pending
           handover would leave identities crossed for good. */
        if (A->ready)
            coop_players_disable();
        g_api->base(cpu);
        coop_effects_tick(cpu);              /* the test bench works solo too */
        return;
    }

    /* The shadows' moby passes come first, straight after slot 0's, which
       GamestateUpdate has just run. */
    if (coop_mobys_p2_pass(cpu))
        return;                              /* a pass started a sequence */

    handover_resume(A);

    /* SNAPSHOT BEFORE THE FIRST CONSUMER. Slot 0's tick spends the substep
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
    g_in_gameplay_tick = 1;
    g_ticking_player   = 0;
    g_api->base(cpu);                                  /* slot 0 */
    g_in_gameplay_tick = 0;
    coop_respawn_blink_tick();

    int32_t gs = gamestate();
    if (gs == 4 || gs == 5) {
        A->ready = 0;                                  /* death: all respawn */
        forget_health();
        g_stats.deaths++;
        return;
    }
    if (gs != GS_PLAYING)
        return;                                        /* the others hold still */

    /* A new level, or a change in how many players there should be. */
    if (A->ready && (A->last_level != level_id() ||
                     coop_party_arena()->shadows != coop_shadow_count())) {
        carry_health(A, 0);
        A->ready = 0;                                  /* reseed next frame */
        g_stats.level_reseeds++;
        return;
    }
    if (!A->ready) {
        seed_shadows(A);
        resample_teleport(A);
        return;
    }

    int n = coop_seeded_shadows();
    int32_t* anchor   = guest32(OP_GADDR_D_80077798);
    int32_t  saved_anchor[3] = { anchor[0], anchor[1], anchor[2] };
    int32_t* substeps = guest32(OP_GADDR_g_UnprocessedFrames);
    int32_t  after_p1 = *substeps;

    for (int k = 1; k <= n; k++) {
        /* PHASE A INPUT: hand each shadow exactly what slot 0 saw. g_PadBackup
           and the swap flag stay his own. When real controllers arrive, this
           is all that changes. */
        CoopShadowView v = coop_shadow(k);
        memcpy(v.pad + PAD_SHADOW_PAD, p1_pad, sizeof p1_pad);
        memcpy(v.pad + PAD_SHADOW_ACTIVEPAD, &p1_active_pad, 4);

        swap_all(A, k);
        *substeps = A->substeps_owed;                  /* same budget as slot 0 */
        arm_script_focus();
        load_regs(cpu, &regs);
        g_in_gameplay_tick = 1;
        g_ticking_player   = k;
        g_api->base(cpu);                              /* shadow k */
        g_in_gameplay_tick = 0;
        g_ticking_player   = 0;
        coop_respawn_blink_tick();
        g_stats.p2_ticks++;
        *substeps = after_p1;                          /* consumed once */

        if (gamestate() != GS_PLAYING) {
            /* This shadow's tick started a GLOBAL sequence: death, a portal, a
               dragon rescue, the balloonist. The sequence wrote its setup into
               the LIVE Spyro and camera, which are his. Swapping those back
               strands it and crashed on PS1. Leave them live; swap only the
               pad back. */
            swap_pad(A, k);
            A->swapped = 0;
            int32_t g2 = gamestate();
            if (g2 == 4 || g2 == 5) {
                A->ready = 0;
                forget_health();
                g_stats.deaths++;
            } else {
                begin_handover(A, k);
            }
            return;
        }

        /* followers (Sparx) track slot 0, not the last shadow to tick */
        memcpy(anchor, saved_anchor, sizeof saved_anchor);
        swap_all(A, k);
    }

    /* Every dragon has moved this frame: resolve any overlap. */
    separate_players(A);
    maybe_swap_view(A);
    coop_effects_tick(cpu);
}

/* ------------------------------------------------------------------------
 * Camera measurement (BUGS.md A1). Called right after each slot's camera
 * update, while that slot's camera and dragon are the live ones. Counts
 * frames spent focused on the shared vector and frames spent too far from
 * the dragon, and logs the start of each runaway.
 * ---------------------------------------------------------------------- */
static uint8_t g_cam_was_runaway[COOP_MAX_PLAYERS];  /* edge detection, display only */

static void measure_camera(int slot) {
    uint8_t* cam   = guest8(OP_GADDR_g_Camera);
    int32_t* cpos  = (int32_t*)(cam + CAMERA_OFF_POSITION);
    uint32_t focus = *(uint32_t*)(cam + CAMERA_OFF_FOCUS);
    uint32_t state = *(uint32_t*)(cam + CAMERA_OFF_STATE);
    int32_t* spos  = live_position();

    int on_shared = (focus == OP_GADDR_D_80077798);
    if (on_shared)
        g_stats.cam_on_shared[slot]++;

    int64_t dx = (int64_t)cpos[0] - spos[0];
    int64_t dy = (int64_t)cpos[1] - spos[1];
    int64_t dz = (int64_t)cpos[2] - spos[2];
    uint32_t dist = isqrt64((uint64_t)(dx * dx + dy * dy + dz * dz));
    if (dist > g_stats.cam_max_dist[slot])
        g_stats.cam_max_dist[slot] = dist;

    int runaway = dist > CAMERA_RUNAWAY_DIST;
    if (runaway) {
        g_stats.cam_runaway[slot]++;
        if (!g_cam_was_runaway[slot]) {
            g_stats.cam_runaway_events[slot]++;
            int32_t* fv = focus ? (int32_t*)g_api->guest(focus) : NULL;
            coop_log(OP_MOD_LOG_WARN,
                     "slot %d camera ran away: %u from its dragon | state 0x%08X | "
                     "focus 0x%08X -> (%d,%d,%d)%s | camera (%d,%d,%d) dragon (%d,%d,%d)",
                     slot, dist, state, focus, fv ? fv[0] : 0, fv ? fv[1] : 0, fv ? fv[2] : 0,
                     on_shared ? " (shared vector)" : "",
                     cpos[0], cpos[1], cpos[2], spos[0], spos[1], spos[2]);
        }
    }
    g_cam_was_runaway[slot] = (uint8_t)runaway;
}

/* ------------------------------------------------------------------------
 * Override: the camera update. Ported from Sp1x2UpdateCameras.
 * ---------------------------------------------------------------------- */
static void on_camera_update(CPUState* cpu) {
    if (cpu->ra != RA_GAMEPLAY_CAMERA) {
        g_stats.camera_other++;
        g_stats.camera_other_ra = cpu->ra;
        g_api->base(cpu);
        coop_tint_state();
        coop_publish_status();
        return;
    }
    g_stats.camera_gameplay++;

    CoopArena* A = coop_arena();
    SavedRegs regs;
    save_regs(cpu, &regs);

    g_api->base(cpu);                                  /* slot 0 */
    measure_camera(0);

    int n = coop_enabled() ? coop_seeded_shadows() : 0;
    for (int k = 1; k <= n; k++) {
        swap_all(A, k);
        load_regs(cpu, &regs);
        g_api->base(cpu);                              /* shadow k */
        g_stats.p2_cameras++;
        measure_camera(k);

        /* Consume his edge latches after his last reader this frame, or they
           accumulate. Harmless while his input is copied fresh each frame,
           and correct once it is not. */
        uint32_t* pad = (uint32_t*)g_api->guest(OP_GADDR_g_Pad);
        pad[0] = 0;  /* m_Down */
        pad[1] = 0;  /* m_Released */

        swap_all(A, k);
    }

    coop_pad_sample();
    coop_tint_state();
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
    /* A zeroed arena (first boot) has every slot claiming player 1. */
    CoopPartyArena* P = coop_party_arena();
    int valid = 1;
    for (int i = 0; i < COOP_MAX_PLAYERS; i++) {
        int seen = 0;
        for (int j = 0; j < COOP_MAX_PLAYERS; j++)
            if (P->person[j] == i) seen++;
        if (seen != 1) valid = 0;
    }
    if (!valid)
        reset_persons();

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

/* Called when co-op is switched off. The engine never rewinds guest state, so
   leave the game as stock would: if a handover left a shadow's identity live,
   trade back, then stand the others down. */
void coop_players_disable(void) {
    CoopArena* A = coop_arena();
    if (A->ready && A->handover) {
        int slot = (int)A->handover;
        swap_spyro(A, slot);
        swap_camera(A, slot);
        trade_persons(slot);
    }
    A->handover = 0;
    A->swapped  = 0;
    A->ready    = 0;
}
