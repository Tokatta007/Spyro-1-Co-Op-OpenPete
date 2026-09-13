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

/* ------------------------------------------------------------------------
 * PER-PLAYER COLOUR. Written into g_Spyro.m_colorFilter, the game's own tint,
 * immediately before each dragon's model is drawn: the model renderer's
 * override does it for every call, so the colour is right in gameplay, in
 * every sequence, and for the portal wingman, without depending on when the
 * tick last ran (pete.c clears the field on state changes).
 *
 * A player at strength 0 is left alone, so the game's own uses of the filter
 * (the fairy kiss) still show. Only when his strength drops to 0 do we write
 * once more, to take our tint back off.
 * ---------------------------------------------------------------------- */
static int     g_drawing_p2;          /* set while the gameplay draw runs his model */
static uint8_t g_tint_written[2];     /* last strength written per player */

/* `slot` is 0 for the live dragon, 1 for the other; the colour is the
   person's, looked up through coop_physical_player. */
static void apply_tint(int slot) {
    const uint8_t* c = g_settings.color[coop_physical_player(slot)];
    if (c[3] == 0 && g_tint_written[slot] == 0)
        return;
    memcpy(guest8(OP_GADDR_g_Spyro + SPYRO_OFF_COLOR_FILTER), c, 4);
    g_tint_written[slot] = c[3];
}

/* WINGMAN COLOUR DIAGNOSTIC (2026-09-13). The portal wingman showed player 1's
   colour although his filter is written before his draw. The retail renderer
   reads g_Spyro + 0x28 per call and loads it into the GTE far colour (RFC,
   GFC, BFC = gte_ctrl 21..23), but OpenPete also rebuilds Spyro natively from
   game state. Logging the far colour after each of the two draws says which
   side drops the wingman's colour: different values mean the retail renderer
   used it and the engine's native path did not. */
static unsigned g_wingman_diag;

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
            g_drawing_p2 = 1;                            /* tinted as player 2 */
            g_api->call(cpu, OP_FNADDR_func_80023AC4);   /* model */
            g_drawing_p2 = 0;
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
#define TUNNEL_GAP_NUM   8      /* 640 * 8 / 5 = 1024 units in the portal tunnel */
#define TUNNEL_GAP_DEN   5
#define FLAME_MATRIX_INTS 5

static void on_spyro_model(CPUState* cpu) {
    apply_tint(g_drawing_p2 ? 1 : 0);        /* every Spyro draw, everywhere */

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
    if (coop_gamestate() == 1) {
        /* THE TUNNEL. The dragons clipped wings here (seen 2026-09-13), at the
           same 640 units the PS1 build used. Gamestate 1 is only the flight
           through the portal tunnel, which cuts to the level before play, so
           a wider gap here never has to match the spacing at landing. */
        for (int i = 0; i < 3; i++)
            right[i] = right[i] * TUNNEL_GAP_NUM / TUNNEL_GAP_DEN;
    }
    pos[0] += right[0];
    pos[1] += right[1];
    pos[2] += right[2];

    /* The wingman is player 2, drawn with player 1's pose: his colour, then
       player 1's filter bytes put back exactly. */
    uint8_t* filter = guest8(OP_GADDR_g_Spyro + SPYRO_OFF_COLOR_FILTER);
    uint8_t  saved_filter[4];
    memcpy(saved_filter, filter, 4);
    const uint8_t* wing = g_settings.color[coop_physical_player(1)];
    if (wing[3] != 0)
        memcpy(filter, wing, 4);
    else
        filter[3] = 0;                   /* untinted wingman, even if player 1 is tinted */

    uint32_t lead_far[3] = { cpu->gte_ctrl[21], cpu->gte_ctrl[22], cpu->gte_ctrl[23] };
    uint8_t  wing_filter[4];
    memcpy(wing_filter, filter, 4);

    load_regs(cpu, &regs);
    g_api->base(cpu);                                  /* the wingman */
    g_stats.flyin_draws++;

    if (g_wingman_diag < 3 && (saved_filter[3] != 0 || wing_filter[3] != 0)) {
        g_wingman_diag++;
        coop_log(OP_MOD_LOG_INFO,
                 "wingman colour check %u (gamestate %d): lead filter %02X%02X%02X/%02X far "
                 "%03X %03X %03X | wingman filter %02X%02X%02X/%02X far %03X %03X %03X",
                 g_wingman_diag, coop_gamestate(),
                 saved_filter[0], saved_filter[1], saved_filter[2], saved_filter[3],
                 lead_far[0], lead_far[1], lead_far[2],
                 wing_filter[0], wing_filter[1], wing_filter[2], wing_filter[3],
                 cpu->gte_ctrl[21], cpu->gte_ctrl[22], cpu->gte_ctrl[23]);
    }

    memcpy(filter, saved_filter, 4);
    memcpy(pos, saved_pos, sizeof saved_pos);
    memcpy(mtx, saved_mtx, sizeof saved_mtx);
}
