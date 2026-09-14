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
static uint8_t g_tint_written[COOP_MAX_PLAYERS];  /* last strength written per slot */

/* `slot` is 0 for the live dragon, 1..3 for a shadow; the colour is the
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

/* ONE SET OF EXTRA DRAGONS PER TICK (2026-09-13). On OpenPete the scene
   composer's Spyro draw, and the portal sequences', run three times per tick
   with the primitive cursor carried on, so the extra dragons were drawn three
   times over. With four players the draw list outgrew the engine's DrawOTag
   scratch ("DrawOTag scratch overflow"), and the overflow corrupted game state
   within a tick: dragons at heights of -1,476,385,299, a false death, a hang.
   Drawing them in the first run of each tick only fixed it, and all four stay
   visible in both renderers. A tick is counted by the camera update, which
   runs once per tick in gameplay and in sequences alike. */
static unsigned g_last_extra_tick = ~0u;
static unsigned g_last_wing_tick  = ~0u;

static unsigned tick_id(void) {
    return g_stats.camera_gameplay + g_stats.camera_other;
}

static int p2_draw_wanted(CoopArena* A) {
    return coop_enabled() && coop_draw_enabled() && coop_seeded_shadows() > 0 &&
           *guest32(OP_GADDR_g_LevelId) == A->last_level;
}

/* THE PRIMITIVE BUFFER HAS A FIXED SIZE, AND SPYRO'S RENDERERS DO NOT CHECK IT
   (measured 2026-09-13). Every primitive of a frame is written into one buffer
   of 0x1C000 bytes, between the cursor D_800757B0 and the limit D_80075780.
   A dragon costs about 7.4 KB for the model and 0.64 KB for his shadow. On
   OpenPete the scene composer's Spyro draw runs three times per tick without
   the cursor going back, so every extra dragon is paid for three times. With
   four players that ran past the limit, and the overflow corrupted the dragons'
   state within a tick (positions of -1,476,385,299, a false death, a hang).
   So an extra dragon is only drawn while a generous margin remains; when it
   does not, that run leaves him out rather than writing past the end. */
#define PRIM_DRAGON_RESERVE 24000   /* one dragon with flame, plus room for the rest of the scene.
                                       Kept as a safety net: the real overflow was the repeat
                                       draws below, not this buffer, which never ran out. */

static int prim_room_for_a_dragon(void) {
    uint32_t cursor = *(uint32_t*)g_api->guest(OP_GADDR_D_800757B0);
    uint32_t limit  = *(uint32_t*)g_api->guest(OP_GADDR_D_80075780);
    return limit > cursor && limit - cursor > PRIM_DRAGON_RESERVE;
}

/* Player 2's model, shadow and flame, each as its own renderer call, with his
   state swapped in. Only when he exists, belongs to this level (so a level
   transition does not draw him where he stood in the last one), and drawing
   is wanted. */
static void draw_player2(CPUState* cpu) {
    CoopArena* A = coop_arena();
    SavedRegs regs;
    save_regs(cpu, &regs);
    int n = coop_seeded_shadows();

    for (int k = 1; k <= n; k++) {
        if (coop_flight_slot_out(k))
            continue;                  /* crashed in a flight level: out of the race */
        coop_swap_spyro(k);
        A->swapped = (uint32_t)k;  /* lets the PadVSync counter see this window too */
        g_in_extra_draw = 1;

        /* Both checks read HIS state: he is swapped in. */
        if (!prim_room_for_a_dragon()) {
            g_stats.p2_draws_skipped++;
        } else if (*guest32(OP_GADDR_g_IsSpyroHidden) == 0) {
            apply_tint(k);                               /* before the call, not inside it */
            if (!coop_respawn_blink_hidden())
                g_api->call(cpu, OP_FNADDR_func_80023AC4);   /* model */
            g_api->call(cpu, OP_FNADDR_func_80059A48);   /* drop shadow */
            g_stats.p2_draws++;
        }
        if (*guest8(OP_GADDR_g_SpyroFlame + FLAME_OFF_ACTIVE) != 0 && prim_room_for_a_dragon()) {
            g_api->call(cpu, OP_FNADDR_func_80058D64);   /* flame */
            g_stats.p2_flame_draws++;
        }

        g_in_extra_draw = 0;
        A->swapped = 0;
        coop_swap_spyro(k);
    }

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
    int n = coop_seeded_shadows();
    for (int k = 1; k <= n; k++) {
        const uint8_t* c = g_settings.color[coop_physical_player(k)];
        if (c[3] != 0 || g_tint_written[k] != 0)
            memcpy(coop_shadow(k).spyro + SPYRO_OFF_COLOR_FILTER, c, 4);
    }
}

static void on_glows_and_sparkles(CPUState* cpu) {
    if (!g_p2_drawn_this_scene && p2_draw_wanted(coop_arena()))
        draw_player2(cpu);
    coop_effects_draw(cpu);                  /* the respawn star, if one is playing */
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
 * DRAW ORDER MATTERS above. (Calling base() twice inside one call was tried
 * first: both dragons took the second one's colour, and the wingman did not
 * survive interpolation either.) For the overlap in the transition, see
 * OVERLAP IN THE TRANSITION below.
 *
 * Covered call sites, read from the retail executable:
 *   0x8001A0D8 in func_8001A050: level transition (1) and entrance (9)
 *   0x8001C964 in func_8001C694: exit level (10)
 * ---------------------------------------------------------------------- */

#define FLAME_OFF_MATRIX 0xB8  /* g_SpyroFlame running orientation matrix */
#define FLAME_MATRIX_INTS 5

/* OVERLAP IN THE TRANSITION: PARKED, ENGINE SIDE (2026-09-13, BUGS.md X4).
 *
 * In the level transition the camera swings to a side view, where the wing
 * line points down the view and the wingman sits behind the lead. OpenPete's
 * native renderer (spyro_native.c) then draws parts of the farther dragon over
 * the nearer one, and it does so whichever order the two calls come in.
 * PsyCross, on the same frame, follows draw order exactly as a PS1 does, and
 * with the wingman first it is correct. So order cannot fix it from a mod.
 *
 * Tried and rejected by the user, so not to be retried as a fix: placement
 * square to the view (v0.5.3), a trailing stagger (v0.5.4), lead drawn first
 * (v0.5.5, also gave both dragons one colour), and a drop below the lead as
 * the camera swings side-on (v0.5.6, clear on screen but looked wrong when
 * the pair landed). The formation stays on the wing line, level. */

/* One wingman draw at wing_pos in his player's colour, as his own call, with
   position, flame matrix and filter put back exactly afterwards. */
static void draw_wingman(CPUState* cpu, int slot, int32_t* pos, int32_t* mtx, uint8_t* filter,
                         const int32_t wing_pos[3]) {
    int32_t saved_pos[3], saved_mtx[FLAME_MATRIX_INTS];
    uint8_t saved_filter[4];
    memcpy(saved_pos, pos, sizeof saved_pos);
    memcpy(saved_mtx, mtx, sizeof saved_mtx);
    memcpy(saved_filter, filter, 4);

    memcpy(pos, wing_pos, sizeof saved_pos);
    const uint8_t* wing = g_settings.color[coop_physical_player(slot)];
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
    if (g_in_extra_draw || coop_menu_drawing_preview()) {
        g_api->base(cpu);                    /* a call we made: colour already set */
        return;
    }
    apply_tint(0);

    /* The gameplay scene composer: player 2 first, as his own call. */
    if (cpu->ra == RA_COMPOSER_MODEL) {
        if (p2_draw_wanted(coop_arena()) && g_last_extra_tick != tick_id()) {
            g_last_extra_tick = tick_id();
            draw_player2(cpu);
            g_p2_drawn_this_scene = 1;
            apply_tint(0);                   /* the lead's colour, for his call */
        }
        if (!coop_respawn_blink_hidden())
            g_api->base(cpu);                /* player 1, last; left out on a blink's off tick */
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

    /* The wingmen first, always: see DRAW ORDER MATTERS. One per extra player,
       in the formation they are seeded in. */
    SavedRegs regs;
    save_regs(cpu, &regs);
    int n = (g_last_wing_tick != tick_id()) ? coop_shadow_count() : 0;
    g_last_wing_tick = tick_id();
    for (int k = 1; k <= n; k++) {
        coop_formation_offset(k, right);
        for (int i = 0; i < 3; i++)
            wing_pos[i] = pos[i] + right[i];
        draw_wingman(cpu, k, pos, mtx, filter, wing_pos);
        load_regs(cpu, &regs);
    }
    g_api->base(cpu);
}
