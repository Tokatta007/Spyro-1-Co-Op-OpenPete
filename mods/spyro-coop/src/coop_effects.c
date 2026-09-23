/**
 * @file coop_effects.c
 * @brief Respawn effects (2026-09-13).
 *
 * A respawn plays every layer here together: the crystal burst, orange and
 * white sparks, the dust ring, a smoke puff and the rescue star. The user
 * chose that mix, and the height, on a test bench (a box per layer, a height
 * slider and a key to play them) that was removed once they had.
 *
 * EVERY EFFECT IS THE GAME'S OWN, found in the decompilation:
 *   - particles, through the level's particle spawner, whose address the
 *     loaders store in D_800758E4: SpawnParticle(count, class, a2, a3). The
 *     classes used exist in every homeworld and level (a class a level lacks
 *     jumps to the end of the spawner, allocating nothing), they only read
 *     their arguments and rand(), and they expire by themselves. Positions are
 *     copied at the call, so the vectors can live in scratch memory.
 *       2   smoke puff                  (the chest break: count 5)
 *       71  white spark streaks
 *       70  orange spark streaks, a3 = speed (the chest break: 16, 0x20)
 *       33  dust at Spyro's feet, a2 = velocity, read from the live g_Spyro
 *           (the heavy landing makes a ring of four)
 *       12  the gem-collect pop, a2 = a Moby*, a3 = color 0x00BBGGRR
 *   - crystal fragments, class 251, through the level's g_SpawnMoby: the
 *     pieces a rescued dragon's crystal breaks into. They fall, smoke and
 *     vanish on their own, but exist only in levels with dragons (homeworlds
 *     and normal levels), so that option falls back to smoke and sparks
 *     anywhere else. EXPERIMENTAL: their size and how the moby passes treat
 *     them are untested.
 *
 * Avoided, per the research: class 24 (never frees itself), 10 (follows the
 * live g_Spyro every frame), 77 (camera-space), and the dragon cutscene flash.
 * Everything runs only in gameplay (gamestate 0), with the dragon that the
 * effect is for as the live one.
 */

#include "coop.h"

static int32_t lift(void);

#define SPARX_FREE_SLOTS_NEEDED 0x15   /* the level's own check before a spawn */
#define FRAGMENT_CLASS  251
#define FRAGMENT_COUNT  10

static uint32_t g_fx_vaddr;

static CoopFxArena* FX(void) { return (CoopFxArena*)g_api->guest(g_fx_vaddr); }

/* ------------------------------------------------------------------------
 * Calling the game
 * ---------------------------------------------------------------------- */

static uint32_t scratch(uint32_t offset) { return g_fx_vaddr + offset; }

static void particle(CPUState* cpu, int count, int cls, uint32_t a2, uint32_t a3) {
    uint32_t spawn = *(uint32_t*)g_api->guest(OP_GADDR_D_800758E4);
    if (spawn == 0)
        return;
    cpu->a0 = (uint32_t)count;
    cpu->a1 = (uint32_t)cls;
    cpu->a2 = a2;
    cpu->a3 = a3;
    g_api->call(cpu, spawn);
}

/* A position vector in scratch: the live dragon's, raised by `lift`. */
static uint32_t here(int32_t lift) {
    CoopFxArena* F = FX();
    const int32_t* pos = guest32(OP_GADDR_g_Spyro + SPYRO_OFF_POSITION);
    int32_t v[3] = { pos[0], pos[1], pos[2] + lift };
    memcpy(F->scratch, v, sizeof v);
    return scratch(offsetof(CoopFxArena, scratch));
}

/* HEIGHT. Spyro's position sits well above his feet (a respawn stands him 356
   units over the floor), and effects started there, or above it, looked as if
   they came out of the top of his head (the user, 2026-09-13). Every effect
   but the dust ring, which is built on the ground, starts FX_HEIGHT from
   that position. */
static int32_t lift(void) { return FX_HEIGHT; }

static void smoke(CPUState* cpu)         { particle(cpu, 5, 2, here(lift()), 0); }
static void white_sparks(CPUState* cpu)  { particle(cpu, 14, 71, here(lift()), 0); }
static void orange_sparks(CPUState* cpu) { particle(cpu, 16, 70, here(lift()), 0x20); }

/* The heavy landing's ring, with eight puffs instead of four. Velocities
   (cos, sin, 0) >> 7 around the circle, as func_80041670 builds them. */
static void dust_ring(CPUState* cpu) {
    CoopFxArena* F = FX();
    const int16_t* cos8 = (const int16_t*)g_api->guest(OP_GADDR_D_8006CC78);
    for (int i = 0; i < 8; i++) {
        int b = (i * 32) & 0xFF;
        int32_t v[3] = { cos8[b] >> 7, cos8[(b - 64) & 0xFF] >> 7, 0 };
        memcpy(F->scratch, v, sizeof v);
        particle(cpu, 1, 33, scratch(offsetof(CoopFxArena, scratch)), 0);
    }
}

/* Levels whose code knows class 251: every homeworld and normal level. */
static int level_has_fragments(int32_t level) {
    int world = level / 10, n = level % 10;
    if (world < 1 || world > 6)
        return 0;
    return (world == 6) ? (n <= 2) : (n <= 4);
}

static int crystal_burst(CPUState* cpu) {
    uint32_t spawn = *(uint32_t*)g_api->guest(OP_GADDR_g_SpawnMoby);
    if (spawn == 0 || !level_has_fragments(coop_level_id()))
        return 0;

    /* A parent the fragment case reads: position at +0xC and a rotation
       matrix at +0x20, identity. */
    CoopFxArena* F = FX();
    uint8_t* m = F->scratch + 16;
    memset(m, 0, 0x58);
    const int32_t* pos = guest32(OP_GADDR_g_Spyro + SPYRO_OFF_POSITION);
    int32_t v[3] = { pos[0], pos[1], pos[2] + lift() };
    memcpy(m + 0x0C, v, sizeof v);
    int16_t one = 0x1000;
    memcpy(m + 0x20, &one, 2);               /* m[0][0] */
    memcpy(m + 0x28, &one, 2);               /* m[1][1] */
    memcpy(m + 0x30, &one, 2);               /* m[2][2] */

    int spawned = 0;
    for (int i = 0; i < FRAGMENT_COUNT; i++) {
        int32_t used = *guest32(OP_GADDR_g_DynMobyCount);
        int32_t max  = *guest32(OP_GADDR_g_DynMobyMax);
        if (max - used < SPARX_FREE_SLOTS_NEEDED)
            break;                           /* the level would refuse; so do we */
        cpu->a0 = FRAGMENT_CLASS;
        cpu->a1 = scratch(offsetof(CoopFxArena, scratch) + 16);
        g_api->call(cpu, spawn);
        if (cpu->v0 != 0)
            spawned++;
    }
    return spawned;
}

/* ------------------------------------------------------------------------
 * THE RESCUE STAR (2026-09-13, at the user's request): the flat star that
 * grows out of a frozen dragon when Spyro touches it.
 *
 * It is ONE global, D_80076248 (dragon.h): +0x0 on, +0x4 position, +0x10 a
 * matrix, +0x24 size, +0x28 a rotation (+0x2A the spin), +0x2B brightness.
 * func_80058864 draws it, semi-transparent triangles into the HUD ordering
 * table, after RotVec8ToMatrix combines the rotation with the camera's
 * projection. Retail calls those only from the rescue's draw (gamestate 8),
 * and animates it in the rescue's update, func_8002F3E4 at 0x8002F494: over
 * 32 ticks the size is t * 16, the brightness t * 4 + 0x40, and the spin
 * grows. Here the same values are set each tick, then played backwards to
 * close it, and it is drawn from the gameplay scene; the struct is switched
 * off again after each draw so the rescue never finds it on.
 * ---------------------------------------------------------------------- */
#define STAR_GROW_TICKS 32

static void star_start(void) {
    CoopFxArena* F = FX();
    const int32_t* pos = guest32(OP_GADDR_g_Spyro + SPYRO_OFF_POSITION);
    F->star_pos[0] = pos[0];
    F->star_pos[1] = pos[1];
    F->star_pos[2] = pos[2] + lift();
    F->star_tick = 1;
}

static void star_advance(void) {
    CoopFxArena* F = FX();
    if (F->star_tick <= 0)
        return;
    if (++F->star_tick > STAR_GROW_TICKS * 2)
        F->star_tick = 0;
}

void coop_effects_draw(CPUState* cpu) {
    CoopFxArena* F = FX();
    if (F->star_tick <= 0 || coop_gamestate() != GS_PLAYING)
        return;
    uint32_t tick = g_stats.camera_gameplay;
    if (F->star_drawn_tick == tick)
        return;                              /* once per tick, as the extra dragons */
    F->star_drawn_tick = tick;

    int t = F->star_tick;
    int grow = (t <= STAR_GROW_TICKS) ? t : STAR_GROW_TICKS * 2 - t;   /* up, then back down */

    uint8_t* star = guest8(OP_GADDR_D_80076248);
    int32_t on = 1, size = grow * 16;
    int bright = grow * 4 + 0x40;
    memcpy(star + 0x00, &on, 4);
    memcpy(star + 0x04, F->star_pos, 12);
    memcpy(star + 0x24, &size, 4);
    star[0x28] = 0;
    star[0x29] = 0;
    star[0x2A] = (uint8_t)(t * 4);
    star[0x2B] = (uint8_t)(bright > 255 ? 255 : bright);

    SavedRegs r;
    save_regs(cpu, &r);
    cpu->a0 = OP_GADDR_D_80076248 + 0x28;
    cpu->a1 = OP_GADDR_D_80076248 + 0x10;
    cpu->a2 = OP_GADDR_g_Camera;             /* m_ProjectionMatrix, at +0 */
    g_api->call(cpu, OP_FNADDR_RotVec8ToMatrix);
    g_api->call(cpu, OP_FNADDR_func_80058864);
    load_regs(cpu, &r);

    on = 0;
    memcpy(star + 0x00, &on, 4);
}

/* ------------------------------------------------------------------------
 * The effects
 * ---------------------------------------------------------------------- */

/* THE RESPAWN SOUND (v0.12.6, the user's request: "a slow fwosh ... it might
   be the slowed down version of one of the main menu sounds"). The game keeps
   its sounds in a table of ids indexed by g_Spu.m_SoundTable, named in the
   decompilation's sound_table.h; these are the ones that could pass for an
   arrival. The M panel picks, so the choice can be made by ear in play. */
const CoopSound k_respawn_sounds[] = {
    { "None",                 -1 },
    { "Inventory swoosh",     56 },
    { "Menu sound",           44 },
    { "Titlescreen move",     55 },
    { "Exit vortex",          26 },
    { "Whirlwind",            54 },
    { "Spyro stars",          27 },
    { "Dragon flash",         51 },
    { "Moby poof",            38 },
    { "Spyro unsquish",       32 },
    { "Rescue sound",         48 },
    { "Windy loop (0x39)",    57 },
};
const int k_respawn_sound_count = (int)(sizeof k_respawn_sounds / sizeof k_respawn_sounds[0]);

#define SPU_SOUND_TABLE 0x2CC          /* g_Spu.m_SoundTable, a guest pointer */

/* One sound from the table, 2D and unconditional, the way the pause menu's
   own chime plays (PlaySound(id, NULL, 16, NULL)). */
void coop_sound_play_table(CPUState* cpu, int table_index) {
    if (table_index < 0)
        return;
    uint32_t table = *(uint32_t*)g_api->guest(OP_GADDR_g_Spu + SPU_SOUND_TABLE);
    if (table == 0)
        return;
    SavedRegs r;
    save_regs(cpu, &r);
    uint32_t id = *guest8(table + (uint32_t)table_index);
    static int said;
    if (!said) {
        said = 1;
        coop_log(OP_MOD_LOG_INFO, "respawn sound: table entry %d is sound id %u",
                 table_index, id);
    }
    cpu->a0 = id;
    cpu->a1 = 0;
    cpu->a2 = 16;
    cpu->a3 = 0;
    g_api->call(cpu, OP_FNADDR_PlaySound);
    load_regs(cpu, &r);
}

static int g_sound_test;      /* the panel asked to hear it; host state, display only */

void coop_sound_test_request(void) { g_sound_test = 1; }

void coop_sound_test_tick(CPUState* cpu) {
    if (!g_sound_test)
        return;
    g_sound_test = 0;
    int pick = g_settings.respawn_sound;
    if (pick > 0 && pick < k_respawn_sound_count)
        coop_sound_play_table(cpu, k_respawn_sounds[pick].index);
}

void coop_effect_play(CPUState* cpu, int layers, int person) {
    if (coop_gamestate() != GS_PLAYING || layers == 0)
        return;
    {
        int pick = g_settings.respawn_sound;
        if (pick > 0 && pick < k_respawn_sound_count)
            coop_sound_play_table(cpu, k_respawn_sounds[pick].index);
    }
    SavedRegs r;
    save_regs(cpu, &r);
    /* The crystal first, so its pieces are under the rest. Where the level has
       no crystal pieces, that layer simply adds nothing. */
    if (layers & (1 << FX_CRYSTAL))       crystal_burst(cpu);
    if (layers & (1 << FX_ORANGE_SPARKS)) orange_sparks(cpu);
    if (layers & (1 << FX_WHITE_SPARKS))  white_sparks(cpu);
    if (layers & (1 << FX_DUST_RING))     dust_ring(cpu);
    if (layers & (1 << FX_SMOKE))         smoke(cpu);
    if (layers & (1 << FX_STAR))          star_start();
    (void)person;
    load_regs(cpu, &r);
    g_stats.effects_played++;
}

/* Once per gameplay tick: the rescue star's clock. */
void coop_effects_tick(CPUState* cpu) {
    (void)cpu;
    star_advance();
}

void coop_effects_init(uint32_t fx_vaddr) {
    g_fx_vaddr = fx_vaddr;
}
