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

/* ------------------------------------------------------------------------
 * Spyro struct offsets. The SDK carries no typed Spyro struct, so these come
 * from the decompilation's spyro.h, and every one was relied on by the PS1
 * mod in play.
 * ---------------------------------------------------------------------- */
#define SPYRO_OFF_POSITION     0x000  /* Vector3D m_Position: x, y, z */
#define SPYRO_OFF_YAW          0x11C  /* heading, 0x1000 per full turn */
#define SPYRO_OFF_SCRIPT_FOCUS 0x21C  /* pointer func_8003FE40 copies into
                                         g_Camera.m_Focus unchecked */

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
    unsigned p2_draws, p2_flame_draws;
    unsigned probe_refusals, query_refusals;
    unsigned padvsync_calls, padvsync_in_swap;
} CoopStats;

extern CoopStats g_stats;

/* coop_main.c */
CoopArena* coop_arena(void);
int        coop_enabled(void);
int        coop_draw_enabled(void);
void       coop_publish_status(void);

/* coop_players.c */
int  coop_players_install(void);
void coop_players_disable(void);
void coop_p2_position(int32_t out[3]);
void coop_swap_spyro(void);

/* coop_draw.c */
int  coop_draw_install(void);

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

/* Guest memory, by address. Host pointers are valid for this process only,
   so they are resolved on use rather than cached across reloads. */
static inline uint8_t* guest8(uint32_t va)  { return (uint8_t*)g_api->guest(va); }
static inline int32_t* guest32(uint32_t va) { return (int32_t*)g_api->guest(va); }

#endif /* SPYRO_COOP_H */
