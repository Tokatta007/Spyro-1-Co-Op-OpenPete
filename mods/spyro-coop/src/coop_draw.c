/**
 * @file coop_draw.c
 * @brief Drawing player 2, the portal wingman, and both dragons' colours.
 *
 * THE PS1 WAY, ported unchanged in idea. Spyro is not a moby, so there is
 * nothing to spawn. Swap player 2's Spyro state into the same globals, call
 * the same three renderers (model, drop shadow, and flame when he breathes
 * fire), and swap back. The renderers never know there are two dragons.
 *
 * WHERE. The gameplay scene composer, func_80019698, draws mobys, then Spyro's
 * model, shadow and flame, then glows and sparkles (func_80058BA8). Player 2
 * is drawn from the model renderer's override at the composer's call, AHEAD of
 * player 1, for the reason in DRAW ORDER MATTERS below; the glows hook covers
 * a scene where player 1 is hidden and the composer skips his model. The
 * flame's orientation matrix lives in the flame state we already swap, so
 * each dragon keeps his own; the PS1 build needed per-viewport flame chains
 * only because it drew every dragon twice.
 *
 * WHAT OPENPETE DOES WITH IT (measured). The second dragon's geometry reaches
 * the native renderer, which draws it on whole frames and drops it from the
 * in-between frames it builds for high frame rates (PORT-INVENTORY.md §7), so
 * player 2 is visible with interpolation off. The "Draw player 2" setting
 * switches just this off, for comparison.
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


/* ------------------------------------------------------------------------
 * DRAW ORDER MATTERS ON OPENPETE (measured 2026-09-13). OpenPete rebuilds
 * Spyro natively, and it takes ONE colour per call of the model renderer: the
 * colour of the last dragon drawn inside that call. Its in-between frames
 * show one dragon, coloured like the last Spyro draw of the frame. The portal
 * diagnostic showed the retail renderer giving the lead red and the wingman
 * green in one call, and the user saw two green dragons; with player 2 drawn
 * after player 1, the camera's dragon showed player 2's colour until
 * interpolation was switched off.
 *
 * So: every extra dragon is its own call of the renderer, and it is drawn
 * BEFORE the dragon the camera follows, which is then always last.
 * ---------------------------------------------------------------------- */

static int g_p2_drawn_this_scene;   /* the composer's hook drew him already */
static int g_in_extra_draw;         /* inside a call we made ourselves */

static int p2_draw_wanted(CoopArena* A) {
    return coop_enabled() && coop_draw_enabled() && A->ready &&
           *guest32(OP_GADDR_g_LevelId) == A->last_level;
}

/* Player 2's model, shadow and flame, each as its own renderer call, with his
   state swapped in. Only when he exists, belongs to this level (so a level
   transition does not draw him where he stood in the last one), and drawing
   is wanted. */
static void draw_player2(CPUState* cpu) {
    CoopArena* A = coop_arena();
    SavedRegs regs;
    save_regs(cpu, &regs);

    coop_swap_spyro();
    A->swapped = 1;  /* lets the PadVSync counter see this window too */
    g_in_extra_draw = 1;

    /* Both checks read HIS state: he is swapped in. */
    if (*guest32(OP_GADDR_g_IsSpyroHidden) == 0) {
        g_drawing_p2 = 1;                            /* tinted as player 2 */
        apply_tint(1);                               /* before the call, not inside it */
        g_api->call(cpu, OP_FNADDR_func_80023AC4);   /* model */
        g_drawing_p2 = 0;
        g_api->call(cpu, OP_FNADDR_func_80059A48);   /* drop shadow */
        g_stats.p2_draws++;
    }
    if (*guest8(OP_GADDR_g_SpyroFlame + FLAME_OFF_ACTIVE) != 0) {
        g_api->call(cpu, OP_FNADDR_func_80058D64);   /* flame */
        g_stats.p2_flame_draws++;
    }

    g_in_extra_draw = 0;
    A->swapped = 0;
    coop_swap_spyro();

    /* The original expects its own arguments, and api->call clobbered them.
       The CPUState reference requires restoring them. */
    load_regs(cpu, &regs);
}

/* func_80058BA8 (glows and sparkles), the composer's last call: the fallback
   for a scene where player 1 was hidden, so the composer never called the
   model renderer and player 2 was not drawn ahead of him. */
/* Keep both dragons' colour in game state, every frame, in every gamestate.
   Writing it only immediately before each draw was not enough: during a
   dragon's dialogue the game keeps clearing the filter (ChangeSpyroState
   zeroes its strength byte), and the native rebuild evidently reads it from
   state rather than only at the draw, so Spyro showed purple for the
   conversation (seen 2026-09-13). */
void coop_tint_state(void) {
    CoopArena* A = coop_arena();
    if (A->swapped)
        return;                              /* never mid-swap */
    apply_tint(0);
    if (A->ready) {
        const uint8_t* c = g_settings.color[coop_physical_player(1)];
        if (c[3] != 0 || g_tint_written[1] != 0)
            memcpy(A->spyro + SPYRO_OFF_COLOR_FILTER, c, 4);
    }
}

static void on_glows_and_sparkles(CPUState* cpu) {
    if (!g_p2_drawn_this_scene && p2_draw_wanted(coop_arena()))
        draw_player2(cpu);
    g_p2_drawn_this_scene = 0;
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
 * The wingman is his own call of the renderer, drawn before the lead: see
 * DRAW ORDER MATTERS above, and THE PORTAL PAIR SEEN FROM THE SIDE below.
 * (Calling base() twice inside one call was tried
 * first: both dragons took the second one's colour, and the wingman did not
 * survive interpolation either.)
 *
 * Covered call sites, read from the retail executable:
 *   0x8001A0D8 in func_8001A050: level transition (1) and entrance (9)
 *   0x8001C964 in func_8001C694: exit level (10)
 * ---------------------------------------------------------------------- */

#define FLAME_OFF_MATRIX 0xB8  /* g_SpyroFlame running orientation matrix */
#define FLAME_MATRIX_INTS 5

/* THE PORTAL PAIR SEEN FROM THE SIDE (2026-09-13, measured headless).
 *
 * The wingman flies along the lead's wing line, as on PS1. In the level
 * transition (gamestate 1) the camera starts facing the pair and then swings
 * round to a side view, where it stays for most of the screen. Logged there:
 * camera to lead (+2695, +58, +818), lead to wingman (+1024, 0, 0). The wing
 * line points straight down the view, so the wingman sits directly behind the
 * lead, smaller and hidden by him. That was the "clipping".
 *
 * Tried and rejected: square to the camera's view (v0.5.3, seen side-on that
 * is nose to tail), a trailing stagger (v0.5.4), and drawing the farther dragon
 * first (v0.5.5: no change to the overlap, and the native renderer gave both
 * dragons the colour of whichever was drawn last).
 *
 * So the formation stays, and the wingman drops as the wing line turns toward
 * the camera: by TUNNEL_DROP times the cosine of the angle between them. Faced
 * head-on nothing moves; side-on he flies below the lead. Below, not above,
 * because the camera sits under the pair and perspective already lowers the
 * farther dragon; above needed more than 700 units and still touched. Chosen
 * from headless screenshots: 350 touched in places, and with cos squared a
 * horn still grazed a wingtip mid-swing; 500 times cos clears every frame.
 * Tunnel only; the landing must match where play begins. */
#define TUNNEL_DROP 500

static int32_t tunnel_drop(const int32_t lead[3], const int32_t off[3]) {
    const int32_t* cam = guest32(OP_GADDR_g_Camera + CAMERA_OFF_POSITION);
    int64_t v[3] = { (int64_t)lead[0] - cam[0], (int64_t)lead[1] - cam[1],
                     (int64_t)lead[2] - cam[2] };
    int64_t  dot = v[0] * off[0] + v[1] * off[1] + v[2] * off[2];
    uint64_t vl  = isqrt64((uint64_t)(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
    uint64_t ol  = isqrt64((uint64_t)((int64_t)off[0] * off[0] + (int64_t)off[1] * off[1]));
    if (vl == 0 || ol == 0)
        return 0;
    int64_t c = (dot < 0 ? -dot : dot) * 4096 / (int64_t)(vl * ol);   /* |cos|, 4096 = 1 */
    return (int32_t)(TUNNEL_DROP * c / 4096);
}

/* One wingman draw at wing_pos in his player's colour, as his own call, with
   position, flame matrix and filter put back exactly afterwards. */
static void draw_wingman(CPUState* cpu, int32_t* pos, int32_t* mtx, uint8_t* filter,
                         const int32_t wing_pos[3]) {
    int32_t saved_pos[3], saved_mtx[FLAME_MATRIX_INTS];
    uint8_t saved_filter[4];
    memcpy(saved_pos, pos, sizeof saved_pos);
    memcpy(saved_mtx, mtx, sizeof saved_mtx);
    memcpy(saved_filter, filter, 4);

    memcpy(pos, wing_pos, sizeof saved_pos);
    const uint8_t* wing = g_settings.color[coop_physical_player(1)];
    if (wing[3] != 0)
        memcpy(filter, wing, 4);
    else
        filter[3] = 0;                       /* untinted, even if the lead is tinted */

    g_in_extra_draw = 1;
    g_api->call(cpu, OP_FNADDR_func_80023AC4);
    g_in_extra_draw = 0;
    g_stats.flyin_draws++;

    memcpy(filter, saved_filter, 4);
    memcpy(pos, saved_pos, sizeof saved_pos);
    memcpy(mtx, saved_mtx, sizeof saved_mtx);
}

static void on_spyro_model(CPUState* cpu) {
    if (g_in_extra_draw) {
        g_api->base(cpu);                    /* a call we made: colour already set */
        return;
    }
    apply_tint(g_drawing_p2 ? 1 : 0);

    /* The gameplay scene composer: player 2 first, as his own call. */
    if (cpu->ra == RA_COMPOSER_MODEL) {
        if (p2_draw_wanted(coop_arena())) {
            draw_player2(cpu);
            g_p2_drawn_this_scene = 1;
            apply_tint(0);                   /* the lead's colour, for his call */
        }
        g_api->base(cpu);                    /* player 1, last */
        return;
    }

    if ((cpu->ra != RA_FLYIN_MODEL && cpu->ra != RA_FLYOUT_MODEL) ||
        !coop_enabled() || !coop_draw_enabled()) {
        g_api->base(cpu);
        return;
    }

    /* The wingman must leave no trace: not his position, not the flame matrix
       every model draw nudges (retail nudges it once here), and not the filter. */
    int32_t* pos    = guest32(OP_GADDR_g_Spyro + SPYRO_OFF_POSITION);
    int32_t* mtx    = guest32(OP_GADDR_g_SpyroFlame + FLAME_OFF_MATRIX);
    uint8_t* filter = guest8(OP_GADDR_g_Spyro + SPYRO_OFF_COLOR_FILTER);
    int32_t  right[3], wing_pos[3];

    coop_formation_offset(right);
    if (coop_gamestate() == 1)
        right[2] -= tunnel_drop(pos, right);
    for (int i = 0; i < 3; i++)
        wing_pos[i] = pos[i] + right[i];

    /* The wingman first, always: see DRAW ORDER MATTERS. (Lead first was tried
       in v0.5.5 and gave both dragons the wingman's colour.) */
    SavedRegs regs;
    save_regs(cpu, &regs);
    draw_wingman(cpu, pos, mtx, filter, wing_pos);
    load_regs(cpu, &regs);
    g_api->base(cpu);
}
