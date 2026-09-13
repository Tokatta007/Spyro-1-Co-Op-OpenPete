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

int coop_draw_install(void) {
    if (g_api->override_name(g_self, "func_80058BA8", on_glows_and_sparkles) != 0) {
        coop_log(OP_MOD_LOG_ERROR, "could not install the player 2 draw");
        return 1;
    }
    return 0;
}
