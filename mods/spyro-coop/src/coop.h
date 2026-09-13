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
#define RA_GAMEPLAY_MOBY_UPDATE 0x80033AACu /* jalr g_UpdateMoby at 0x80033AA4 */
/* Spyro's model renderer, func_80023AC4, called from the sequence draws: */
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

/* How far apart the dragons start, in world units. Above the body radius, or
   the separation push would shove them apart the moment play resumes. */
#define P2_START_OFFSET 0x280

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
    uint32_t handover;       /* his tick started a sequence; identities crossed */
    int32_t  substeps_owed;  /* physics budget captured before player 1 spends it */
    int32_t  last_seq;       /* last non-zero gamestate, for the teleport detector */
    uint32_t swapped;        /* 1 while player 2's state is live mid-override */
    uint32_t view_key_down;  /* last tick's view-swap key, for edge detection.
                                Here and not a host static, so rewind and
                                runahead cannot miss or repeat a press. */
    int32_t  tp_sample[3];   /* live dragon's position last frame */
    uint8_t  spyro[SPYRO_STATE_BYTES];
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
    uint8_t  stash[MOBY_MAX * 2];  /* m_WasDrawn and m_UpdateDistance while masked */
    uint32_t p2_sparx;             /* guest Moby*, 0 = none spawned */
    uint32_t sparx1_seen;          /* last g_Sparx seen: level rebuild detector */
    uint32_t sparx_spawns_level;   /* spawns since the last rebuild; capped */
} CoopMobyArena;

/* ------------------------------------------------------------------------
 * A third allocation, appended for the same ledger reason as CoopMobyArena.
 * ---------------------------------------------------------------------- */
typedef struct {
    /* Player 2's copy of D_80077798, the vector camera mode 6 and
       func_8003FE40 point g_Camera.m_Focus at. See BUGS.md A1. */
    int32_t p2_focus_vector[3];
    int32_t owner_level;      /* level the moby owner table belongs to */
} CoopExtraArena;

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
    unsigned moby_two_pass, moby_single_pass, moby_fns_hooked;
    unsigned sparx_spawns, pushes;
    unsigned owner_flips;
    unsigned cam_on_shared[2];    /* frames each camera focused on D_80077798 */
    unsigned cam_runaway[2];      /* frames each camera was too far from its dragon */
    unsigned cam_runaway_events[2];
    uint32_t cam_max_dist[2];
    unsigned probe_refusals, query_refusals;
    unsigned padvsync_calls, padvsync_in_swap;
} CoopStats;

extern CoopStats g_stats;

/* coop_main.c */
CoopArena* coop_arena(void);
CoopMobyArena* coop_moby_arena(void);
CoopExtraArena* coop_extra_arena(void);
int        coop_focus_per_player(void);
int        coop_hysteresis_percent(void);
int        coop_enabled(void);
int        coop_draw_enabled(void);
void       coop_publish_status(void);

/* coop_players.c */
int  coop_players_install(void);
void coop_players_disable(void);
void coop_p2_position(int32_t out[3]);
void coop_swap_spyro(void);
void coop_formation_offset(int32_t out[3]);
void coop_swap_camera(void);
void coop_handover_resume(void);
int32_t coop_gamestate(void);
int32_t coop_level_id(void);

/* coop_mobys.c */
void coop_mobys_track(void);

/* coop_draw.c */
int  coop_draw_install(void);  /* gameplay draw and portal fly-in */

/* coop_gates.c */
int  coop_gates_install(void);

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
