/**
 * @file coop.h
 * @brief Shared declarations for the spyro-coop mod.
 *
 * Phase A of docs/PORT-INVENTORY.md: a second Spyro whose tick and camera
 * update run every frame alongside player 1's, borrowing player 1's input,
 * plus an experiment in drawing him (coop_draw.c). See that document for why each piece exists
 * and what it was on the PlayStation.
 *
 * Derived from the PS1 Spyro 1 Co-Op Mod, itself derived from Spyromain's
 * Spyro2x2 (MIT). See LICENSE.
 */
#ifndef SPYRO_COOP_H
#define SPYRO_COOP_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <openpete_mod_api.h>
#include <openpete_sdk_symbols.h>
#include <psx_runtime.h>

extern const openpete_mod_api_t* g_api;
extern openpete_mod_t*           g_self;

/* ------------------------------------------------------------------------
 * Call sites. OpenPete runs the retail executable, so these are the retail
 * addresses, read straight out of SCUS_942.28 (docs/PORT-INVENTORY.md §1).
 * An override catches EVERY call to a function; these tell the gameplay call
 * apart from the cutscene and menu callers, which must get stock behaviour.
 * ---------------------------------------------------------------------- */
#define RA_GAMEPLAY_SPYRO_TICK 0x80033AE0u  /* jal func_8004A200 at 0x80033AD8 */
#define RA_GAMEPLAY_CAMERA     0x80033B54u  /* jal CameraUpdate  at 0x80033B4C */
#define RA_GAMEPLAY_ENV_UPDATE 0x80033A98u  /* jal func_8002A6FC at 0x80033A90,
                                               immediately before jalr g_UpdateMoby */
/* Spyro's model renderer, func_80023AC4, called from the sequence draws: */
#define RA_COMPOSER_MODEL      0x80019700u  /* func_80019698, the scene composer */
#define RA_FLYIN_MODEL         0x8001A0E0u  /* func_8001A050: gamestates 1 and 9 */
#define RA_FLYOUT_MODEL        0x8001C96Cu  /* func_8001C694: gamestate 10 */

/* ------------------------------------------------------------------------
 * Spyro struct offsets. The SDK carries no typed Spyro struct, so these come
 * from the decompilation's spyro.h, and every one was relied on by the PS1
 * mod in play.
 * ---------------------------------------------------------------------- */
#define SPYRO_OFF_POSITION     0x000  /* Vector3D m_Position: x, y, z */
#define SPYRO_OFF_YAW          0x11C  /* heading, 0x1000 per full turn */
#define SPYRO_OFF_SCRIPT_FOCUS 0x21C  /* pointer func_8003FE40 copies into
                                         g_Camera.m_Focus unchecked */
#define SPYRO_OFF_HEALTH       0x164  /* int m_health: 3 green ... 0 no Sparx */
#define SPYRO_OFF_COLOR_FILTER 0x028  /* r, g, b, interpolation: the game's own
                                         tint, interpolated with vertex colours */
#define SPYRO_OFF_RESPAWN_BLINK 0x260 /* decomp's unk_0x260, "No XREFS": nothing in
                                         the game touches it, so the mod keeps a
                                         respawned dragon's blink ticks here, where
                                         every swap and savestate carries it */

/* Camera struct offsets, from the decompilation's camera.h (two 20-byte
   SHORTMATRIX fields come first). */
#define CAMERA_OFF_POSITION 0x28  /* Vector3D m_Position */
#define CAMERA_OFF_STATE    0x58  /* u_int m_State, the camera mode */
#define CAMERA_OFF_FOCUS    0xD0  /* Vector3D* m_Focus, a guest pointer */

/* A camera further than this from its own dragon has run away. The normal
   follow distance is about 2,560; PS1 measured a runaway at 54,150,062. */
#define CAMERA_RUNAWAY_DIST 0x4000

/* Gamestates the logic branches on. 0 is gameplay; 4 and 5 are the death
   sequence; 8, 11 and 12 are the dragon rescue, fairy prompt and balloonist,
   which reposition the live dragon without rebuilding the level. */
#define GS_PLAYING 0

/* How far apart the dragons start, and fly in formation, in world units: 640,
   the PS1 build's spacing. It was 1024 for a while (v0.5.1) to stop the dragons
   overlapping in the portal tunnel, but that overlap turned out to be the
   native renderer's (BUGS.md X4) and the wider gap did not help, so the user
   asked for the PS1 value back (2026-09-13). One value for seeding and every
   sequence, so the dragons never jump apart when one hands over to the next.
   Must stay above the body radius (416). */
#define P2_START_OFFSET 0x280

/* Up to four dragons: slot 0 is the live one, slots 1..3 are shadows. */
#define COOP_MAX_PLAYERS 4
#define COOP_MAX_SHADOWS (COOP_MAX_PLAYERS - 1)

/* ------------------------------------------------------------------------
 * Per-player state. Lives in the guest arena, so savestates, rewind and
 * runahead carry it exactly as they carry guest RAM. Host statics do not
 * roll back, which is why nothing here may live in one.
 * ---------------------------------------------------------------------- */
#define SPYRO_STATE_BYTES   1092  /* 15 regions, docs/PORT-INVENTORY.md §3 */
#define CAMERA_STRUCT_BYTES 0x110
#define CAMERA_EXTRA_COUNT  4
#define PAD_STATE_BYTES     333   /* g_Pad + g_PadBackup + flag + pointer */

typedef struct {
    uint32_t ready;          /* player 2 is seeded; the master switch */
    int32_t  last_level;     /* level id he was seeded in */
    uint32_t handover;       /* 0, or the shadow slot (1..3) whose tick started a
                                sequence: that slot's state is live until it ends */
    int32_t  substeps_owed;  /* physics budget captured before player 1 spends it */
    int32_t  last_seq;       /* last non-zero gamestate, for the teleport detector */
    uint32_t swapped;        /* non-zero while a shadow's state is live mid-override */
    uint32_t view_key_down;  /* last tick's view-swap key, for edge detection.
                                Here and not a host static, so rewind and
                                runahead cannot miss or repeat a press. */
    int32_t  tp_sample[3];   /* live dragon's position last frame */
    uint8_t  spyro[SPYRO_STATE_BYTES];       /* shadow slot 1 */
    uint8_t  camera[CAMERA_STRUCT_BYTES];
    int32_t  camera_extra[CAMERA_EXTRA_COUNT];
    uint8_t  pad[PAD_STATE_BYTES];
} CoopArena;

/* ------------------------------------------------------------------------
 * Moby partition state. A SEPARATE allocation from CoopArena on purpose: the
 * engine accepts a savestate only if its allocation ledger is a prefix of the
 * live one, so appending a block keeps earlier savestates loadable, and
 * growing the first block would not.
 * ---------------------------------------------------------------------- */
#define MOBY_MAX 1024  /* PS1 capped at 512 for space and a rescue overran it */

typedef struct {
    uint8_t  owner[MOBY_MAX];      /* 0 player 1, 1 player 2, 2 dead slot */
    uint8_t  unused_was_stash[MOBY_MAX * 2]; /* masking retired 2026-09-13; kept
                                               so v0.4.0 savestates stay valid */
    uint32_t p2_sparx;             /* shadow slot 1's Sparx: guest Moby*, 0 = none */
    uint32_t sparx1_seen;          /* last g_Sparx seen: level rebuild detector */
    uint32_t sparx_spawns_level;   /* spawns since the last rebuild; capped */
} CoopMobyArena;

/* ------------------------------------------------------------------------
 * A third allocation, appended for the same ledger reason as CoopMobyArena.
 * ---------------------------------------------------------------------- */
typedef struct {
    /* Player 2's health, carried across a level change (coop_players.c).
       [0] is set when a carry is pending, [1] is the health. These three
       ints were a ruled-out camera experiment's vector; reused rather than
       resized so savestates made since v0.3.0 stay valid. */
    int32_t p2_health_carry[2];
    int32_t slots_swapped;    /* 1 after an odd number of view-key swaps: slot 0
                                 (the live dragon) is physically player 2 */
    int32_t owner_level;      /* level the moby owner table belongs to */
} CoopExtraArena;

/* ------------------------------------------------------------------------
 * A fourth allocation, appended for the ledger reason above: respawn state.
 * ---------------------------------------------------------------------- */
typedef struct {
    int32_t arrival[4];     /* x, y, z, rotation where this level was entered */
    int32_t start[5];       /* level id, then x, y, z, rotation of its true start */
    int32_t fairy_mute[3];  /* respawned player + 1 (0 = off), then x, y */
    int32_t sparx_heal_pending; /* player 1 respawned on his own and needs a Sparx */
} CoopRespawnArena;

/* ------------------------------------------------------------------------
 * A fifth allocation, appended for the ledger reason above: the in-game
 * Multiplayer menu (coop_menu.c). In guest memory so a savestate taken with
 * a page open, or a rewind, finds the menu where the game's own pause is.
 * ---------------------------------------------------------------------- */
typedef struct {
    int32_t  page;          /* 0 none, 1 Multiplayer, 2 Colors */
    int32_t  cursor;        /* row on that page */
    uint32_t scratch_used;
    uint8_t  scratch[244];  /* strings and vectors the game reads by pointer */
} CoopMenuArena;

/* ------------------------------------------------------------------------
 * A sixth allocation, appended for the ledger reason above: players 3 and 4
 * (2026-09-13). Shadow slot 1 stays in CoopArena; slots 2 and 3 live here,
 * with what every slot needs beyond it.
 * ---------------------------------------------------------------------- */
typedef struct {
    uint8_t spyro[SPYRO_STATE_BYTES];
    uint8_t camera[CAMERA_STRUCT_BYTES];
    int32_t camera_extra[CAMERA_EXTRA_COUNT];
    uint8_t pad[PAD_STATE_BYTES];
} CoopShadow;

typedef struct {
    int32_t    shadows;        /* shadow slots seeded, 1..3; valid while ready */
    int32_t    person[COOP_MAX_PLAYERS]; /* whose dragon is in each slot (0-based),
                                            a permutation; the view key and
                                            handovers trade entries */
    CoopShadow extra[2];       /* shadow slots 2 and 3 */
    uint32_t   sparx[2];       /* their Sparx, guest Moby*, 0 = none */
    int32_t    health_carry[COOP_MAX_PLAYERS][2]; /* per slot: pending, health */
    int32_t    flight_out[COOP_MAX_PLAYERS];      /* per PLAYER: crashed, sitting out */
    uint32_t   flight_end_real;                   /* this flight level's Flight1 */
    int32_t    health_before_flight[COOP_MAX_PLAYERS]; /* per slot: restored on leaving one */
    int32_t    portal_pin;     /* slot + 1 of the dragon who touched a portal, 0 = none */
    int32_t    stray_ticks[COOP_MAX_PLAYERS];     /* per slot: exit glide after slot 0 landed */
    int32_t    results_pending; /* flight results shown: reseed when play resumes */
    uint32_t   controller_held; /* the extra players' controller, last tick's buttons */
} CoopPartyArena;

/* ------------------------------------------------------------------------
 * A seventh allocation, appended for the ledger reason above: scratch for the
 * respawn effects (coop_effects.c), whose particle and moby calls read vectors
 * and a stand-in Moby by pointer.
 * ---------------------------------------------------------------------- */
typedef struct {
    uint8_t  scratch[16 + 0x58];
    uint32_t unused_test_key; /* the retired O test key's; kept for the layout */
    int32_t  star_tick;       /* 0 idle, else ticks since the rescue star started */
    int32_t  star_pos[3];
    uint32_t star_drawn_tick; /* the tick it was last drawn in */
} CoopFxArena;

/* Respawn effect layers; any combination plays together. */
enum {
    FX_CRYSTAL,
    FX_ORANGE_SPARKS,
    FX_WHITE_SPARKS,
    FX_DUST_RING,
    FX_SMOKE,
    FX_STAR,
    FX_LAYER_COUNT
};
/* The user's pick, 2026-09-13, chosen on a test bench since retired: every
   layer, the rescue star included, starting 300 below Spyro's position. */
#define FX_DEFAULT_LAYERS ((1 << FX_CRYSTAL) | (1 << FX_ORANGE_SPARKS) | \
                           (1 << FX_WHITE_SPARKS) | (1 << FX_DUST_RING) | \
                           (1 << FX_SMOKE) | (1 << FX_STAR))
#define FX_HEIGHT (-300)  /* world units from Spyro's position, which sits well
                             above his feet; the dust ring ignores it */

/* One shadow slot's buffers, wherever they live. */
typedef struct {
    uint8_t*  spyro;
    uint8_t*  camera;
    int32_t*  camera_extra;
    uint8_t*  pad;
    uint32_t* sparx;           /* where this slot's Sparx pointer is kept */
} CoopShadowView;

/* Moby owner codes beyond the slots 0..3. */
#define OWNER_DEAD 0xFE        /* a dead slot in the moby array: belongs to nobody */
#define OWNER_NEW  0xFF        /* not assigned yet: take the plain nearest */

/* ------------------------------------------------------------------------
 * Host counters. Display only: they reset after a savestate load, which is
 * acceptable for numbers nobody plays against.
 * ---------------------------------------------------------------------- */
typedef struct {
    unsigned tick_gameplay, tick_other;
    unsigned camera_gameplay, camera_other;
    uint32_t tick_other_ra, camera_other_ra;
    unsigned p2_ticks, p2_cameras;
    unsigned seeds, level_reseeds, deaths, handovers, teleports;
    unsigned view_swaps;
    unsigned p2_draws, p2_flame_draws, flyin_draws;
    unsigned p2_draws_skipped;    /* extra dragons left out: primitive buffer nearly full */
    unsigned moby_two_pass, moby_single_pass, list_dropped;
    unsigned sparx_spawns, pushes;
    unsigned owner_flips;
    unsigned individual_respawns, double_deaths, sparx_heals;
    unsigned sounds_nearer_p2;    /* voices measured from player 2's camera */
    unsigned pod_members;         /* mobys in a pod, last assignment */
    unsigned pod_merges;          /* pods joined because a list crossed them */
    unsigned cam_on_shared[COOP_MAX_PLAYERS];  /* frames each camera focused on D_80077798 */
    unsigned cam_runaway[COOP_MAX_PLAYERS];    /* frames each camera was too far from its dragon */
    unsigned cam_runaway_events[COOP_MAX_PLAYERS];
    uint32_t cam_max_dist[COOP_MAX_PLAYERS];
    unsigned probe_refusals, query_refusals;
    unsigned padvsync_calls, padvsync_in_swap;
    unsigned menu_opens;          /* Multiplayer page opened from the pause list */
    unsigned effects_played;      /* respawn effects, real and tested */
    unsigned flight_sit_outs;     /* crashes in a flight level that did not end it */
    unsigned strays_landed;       /* dragons set down after missing a portal exit */
} CoopStats;

extern CoopStats g_stats;

/* ------------------------------------------------------------------------
 * Settings (coop_settings.c). Host state, tick context only.
 * ---------------------------------------------------------------------- */
typedef struct {
    int     players;         /* 1 to 4 */
    int     respawn_modern;  /* 1 modern, 0 original */
    int     split_vertical;  /* 1 vertical, 0 horizontal; no effect until split-screen exists */
    uint8_t color[COOP_MAX_PLAYERS][4]; /* per player: red, green, blue, strength */
    int     extra_controls;  /* EXTRA_CONTROLS_* */
    int     draw_p2;         /* development */
    int     hysteresis;      /* development: enemy switch margin, percent */
} CoopSettings;

/* Where players 2 to 4 get their input (coop_players.c, CONTROLS). */
enum { EXTRA_CONTROLS_COPY, EXTRA_CONTROLS_CONTROLLER, EXTRA_CONTROLS_COUNT };

extern CoopSettings g_settings;
void coop_settings_load(void);
void coop_settings_save(void);
void coop_settings_tick(void);     /* adopt an M panel edit */
void coop_settings_changed(void);  /* after the in-game menu edits g_settings */
void coop_settings_reset_color(int player);  /* back to Spyro's own, and save */

/* coop_main.c */
CoopArena* coop_arena(void);
CoopMobyArena* coop_moby_arena(void);
CoopExtraArena* coop_extra_arena(void);
CoopRespawnArena* coop_respawn_arena(void);
CoopPartyArena* coop_party_arena(void);
int        coop_respawn_enabled(void);
int        coop_hysteresis_percent(void);
int        coop_enabled(void);        /* two players or more */
int        coop_shadow_count(void);   /* dragons besides the live one the settings ask for */
int        coop_draw_enabled(void);
void       coop_publish_status(void);

/* coop_players.c */
int  coop_players_install(void);
void coop_players_disable(void);
void coop_p2_position(int32_t out[3]);
CoopShadowView coop_shadow(int slot);      /* slot 1..3 */
int  coop_seeded_shadows(void);            /* 0 unless ready */
void coop_swap_spyro(int slot);
void coop_swap_camera(int slot);
int  coop_physical_player(int slot);  /* which player's dragon is in slot 0..3 */
void coop_formation_offset(int slot, int32_t out[3]);
void coop_handover_resume(void);
void coop_resample_teleport(void);
int  coop_in_gameplay_tick(void);   /* inside Spyro's gameplay tick right now */
int  coop_ticking_player(void);     /* 0 or 1, valid while the above is true */
int32_t coop_gamestate(void);
int32_t coop_level_id(void);

/* coop_mobys.c */
int  coop_mobys_install(void);
int  coop_mobys_p2_pass(CPUState* cpu);
void coop_mobys_identities_swapped(int slot);  /* slot traded identities with slot 0 */

/* coop_respawn.c */
int  coop_respawn_install(void);
#define COOP_RESPAWN_BLINK_TICKS 45        /* about 1.5 seconds */
void coop_respawn_blink_tick(void);   /* count down the live dragon's blink */
int  coop_respawn_blink_hidden(void); /* the live dragon is in an "off" blink tick */
void coop_capture_spawn(void);
void coop_fairy_mute(int player);
void coop_sparx_heal(CPUState* cpu);

/* coop_draw.c */
int  coop_draw_install(void);  /* gameplay draw and portal fly-in */
void coop_tint_state(void);    /* keep both dragons' colour in game state */

/* coop_menu.c */
int  coop_menu_install(uint32_t menu_vaddr);
int  coop_menu_drawing_preview(void);  /* the Colors page is drawing a preview dragon */

/* coop_effects.c */
void coop_effects_init(uint32_t fx_vaddr);
void coop_effect_play(CPUState* cpu, int layers, int person);
void coop_effects_tick(CPUState* cpu);   /* the rescue star's clock */
void coop_effects_draw(CPUState* cpu);   /* the star, from the scene composer's end */

/* coop_flight.c */
int  coop_flight_install(void);
void coop_flight_tick(void);
int  coop_flight_slot_out(int slot);   /* this slot's dragon crashed in a flight level */
int  coop_flight_pick_camera(void);    /* slot to trade with slot 0, or 0 */

/* coop_gates.c */
int  coop_gates_install(void);

/* coop_sound.c */
int  coop_sound_install(void);

/* coop_pad.c */
int  coop_pad_install(void);
void coop_pad_sample(void);
void coop_pad_status(void);

/* Registers an override relies on across base() or api->call(). The CPUState
   reference: expect a0..a3, v0, v1 and ra to have changed across either. */
typedef struct { uint32_t a0, a1, a2, a3, v0, v1, ra; } SavedRegs;
static inline void save_regs(const CPUState* c, SavedRegs* s) {
    s->a0 = c->a0; s->a1 = c->a1; s->a2 = c->a2; s->a3 = c->a3;
    s->v0 = c->v0; s->v1 = c->v1; s->ra = c->ra;
}
static inline void load_regs(CPUState* c, const SavedRegs* s) {
    c->a0 = s->a0; c->a1 = s->a1; c->a2 = s->a2; c->a3 = s->a3;
    c->v0 = s->v0; c->v1 = s->v1; c->ra = s->ra;
}

/* Printing. ALWAYS use these, never g_api->ui_status or g_api->log directly.
   The API's print functions are reached through function pointers, which the
   compiler cannot check against their format strings. A mismatched argument
   list crashed OpenPete at startup on 2026-09-12: a counter was passed where
   the format said %s, and the engine read it as a string address. These carry
   the printf format attribute, so -Wformat now catches that at compile time. */
__attribute__((format(printf, 1, 2)))
void coop_status(const char* fmt, ...);
__attribute__((format(printf, 2, 3)))
void coop_log(int level, const char* fmt, ...);

/* Integer square root, for distances without floating point. */
static inline uint32_t isqrt64(uint64_t v) {
    uint64_t r = 0, bit = 1ull << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else              { r >>= 1; }
        bit >>= 2;
    }
    return (uint32_t)r;
}

/* Guest memory, by address. Host pointers are valid for this process only,
   so they are resolved on use rather than cached across reloads. */
static inline uint8_t* guest8(uint32_t va)  { return (uint8_t*)g_api->guest(va); }
static inline int32_t* guest32(uint32_t va) { return (int32_t*)g_api->guest(va); }

#endif /* SPYRO_COOP_H */
