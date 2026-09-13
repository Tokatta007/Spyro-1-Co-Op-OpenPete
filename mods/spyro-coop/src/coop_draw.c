/**
 * @file coop_draw.c
 * @brief The visibility experiment: draw player 2 with the game's own Spyro
 *        renderers.
 *
 * THE PS1 WAY, ported unchanged in idea. Spyro is not a moby, so there is
 * nothing to spawn. Instead, after the game has drawn player 1, swap player
 * 2's Spyro state into the same globals and call the same three renderers
 * again: the model, the drop shadow, and (if he is breathing fire) the flame.
 * Swap back. The renderers never know there are two dragons.
 *
 * WHERE. The gameplay scene composer, func_80019698, draws mobys, then Spyro's
 * model, shadow and flame, then calls func_80058BA8 (glows and sparkles) last.
 * func_80058BA8 has that one caller, so a pre-hook on it runs exactly once per
 * composed scene, straight after player 1's dragon, whether or not player 1
 * himself was hidden. The flame's orientation matrix lives inside the flame
 * state we already swap, so each dragon keeps his own; the PS1 build needed
 * per-viewport flame chains only because it drew every dragon twice.
 *
 * WHY IT IS AN EXPERIMENT. On PS1 these renderers emitted primitives into an
 * ordering table, and calling them twice drew twice. OpenPete's native
 * renderer builds its scene its own way, so whether a second call produces a
 * second dragon is exactly what this finds out. Three outcomes are possible:
 *   - a second dragon appears: visible single-screen co-op, today;
 *   - nothing appears: the renderer does not take its dragon from these calls;
 *   - one dragon flickers or smears between two places: it does, but treats
 *     both calls as the same object, which is worth knowing for the pane API.
 * The "Draw player 2" setting switches just this off, for comparison.
 */

#include "coop.h"

#define FLAME_OFF_ACTIVE 0x98  /* g_SpyroFlame.m_IsFlameActive, one byte */

static void on_glows_and_sparkles(CPUState* cpu) {
    CoopArena* A = coop_arena();

    /* Only when he exists, belongs to this level (so a level transition does
       not draw him where he stood in the last one), and drawing is wanted. */
    if (coop_enabled() && coop_draw_enabled() && A->ready &&
        *guest32(OP_GADDR_g_LevelId) == A->last_level) {
        SavedRegs regs;
        save_regs(cpu, &regs);

        coop_swap_spyro();
        A->swapped = 1;  /* lets the PadVSync counter see this window too */

        /* Both checks read HIS state: he is swapped in. */
        if (*guest32(OP_GADDR_g_IsSpyroHidden) == 0) {
            g_api->call(cpu, OP_FNADDR_func_80023AC4);   /* model */
            g_api->call(cpu, OP_FNADDR_func_80059A48);   /* drop shadow */
            g_stats.p2_draws++;
        }
        if (*guest8(OP_GADDR_g_SpyroFlame + FLAME_OFF_ACTIVE) != 0) {
            g_api->call(cpu, OP_FNADDR_func_80058D64);   /* flame */
            g_stats.p2_flame_draws++;
        }

        A->swapped = 0;
        coop_swap_spyro();

        /* The original expects its own arguments, and api->call clobbered
           them. The CPUState reference requires restoring them. */
        load_regs(cpu, &regs);
    }

    g_api->base(cpu);
}

static void on_spyro_model(CPUState* cpu);

int coop_draw_install(void) {
    if (g_api->override_name(g_self, "func_80058BA8", on_glows_and_sparkles) != 0 ||
        g_api->override_name(g_self, "func_80023AC4", on_spyro_model) != 0) {
        coop_log(OP_MOD_LOG_ERROR, "could not install the player 2 draw");
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * The portal fly-in and fly-out. Ported from Sp1x2DrawPortalSpyro, and
 * extended to the level exit, which the PS1 build never covered.
 *
 * THE FLIGHT IS STAGED. During these sequences the dragon does not really
 * travel: the game parks him and orbits the camera around him while the
 * background scrolls. So there is no stable "beside him" in world space. PS1
 * tried a world axis (the gap collapsed into depth when the camera looked
 * down it), a camera matrix row (came out vertical), and a direction square
 * to the view (right on screen, but the wingman slid round as the camera
 * orbited). What works is anchoring to the DRAGON: offset along his own wing
 * line, so the pair is a rigid formation the camera views from moving angles.
 *
 * WHAT IS DRAWN is player 1's dragon a second time, pose and all, moved
 * sideways. Player 2's own state is not used: in these sequences he has no
 * meaningful pose of his own, and the two dragons look identical anyway.
 *
 * Unlike the gameplay draw, this one calls base() twice INSIDE the model
 * renderer's own override. If the engine brackets its render paths per call
 * of this function, the wingman may survive interpolation where the gameplay
 * draw does not, which would be worth knowing.
 *
 * Covered call sites, read from the retail executable:
 *   0x8001A0D8 in func_8001A050: level transition (1) and entrance (9)
 *   0x8001C964 in func_8001C694: exit level (10)
 * ---------------------------------------------------------------------- */

#define FLAME_OFF_MATRIX 0xB8  /* g_SpyroFlame running orientation matrix */
#define FLAME_MATRIX_INTS 5

static void on_spyro_model(CPUState* cpu) {
    if ((cpu->ra != RA_FLYIN_MODEL && cpu->ra != RA_FLYOUT_MODEL) ||
        !coop_enabled() || !coop_draw_enabled()) {
        g_api->base(cpu);
        return;
    }

    SavedRegs regs;
    save_regs(cpu, &regs);

    g_api->base(cpu);                                  /* player 1, stock */

    /* The wingman must leave no trace: not his position, and not the flame
       matrix, which every model draw nudges. Retail nudges it once here. */
    int32_t* pos = guest32(OP_GADDR_g_Spyro + SPYRO_OFF_POSITION);
    int32_t* mtx = guest32(OP_GADDR_g_SpyroFlame + FLAME_OFF_MATRIX);
    int32_t  saved_pos[3], saved_mtx[FLAME_MATRIX_INTS], right[3];

    memcpy(saved_pos, pos, sizeof saved_pos);
    memcpy(saved_mtx, mtx, sizeof saved_mtx);
    coop_formation_offset(right);
    pos[0] += right[0];
    pos[1] += right[1];
    pos[2] += right[2];

    load_regs(cpu, &regs);
    g_api->base(cpu);                                  /* the wingman */
    g_stats.flyin_draws++;

    memcpy(pos, saved_pos, sizeof saved_pos);
    memcpy(mtx, saved_mtx, sizeof saved_mtx);
}
