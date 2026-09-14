/**
 * @file coop_respawn.c
 * @brief Individual death and respawn. Ported from Sp1x2Die, Sp1x2Ground,
 *        Sp1x2CaptureSpawn, Sp1x2SparxHeal and Sp1x2FairyMute.
 *
 * RETAIL: any death calls TriggerRespawnOrGameOver (func_8002C85C), which
 * spends a life, fades out, reloads the level and puts Spyro back at the
 * checkpoint. In co-op that drags both players back. Spyro2x2 does the same.
 *
 * HERE: when one dragon dies while another is alive and a life remains,
 * the trigger is not called at all. We spend the life, tell the HUD, put him
 * at the respawn point standing on the floor, and reset him with the game's
 * own ResetSpyroState, which is what the reload would have done to him. The
 * other dragon never notices.
 *
 * WHY REPLACE RATHER THAN UNDO: two PS1 attempts let the trigger start and
 * then tried to cancel it. It sets up fade, camera and sequence state beyond
 * the gamestate, and reverting half of it crashed. Deciding before the
 * trigger runs means none of that is ever set up.
 *
 * The stock trigger still runs, unchanged, when there is nobody to carry on:
 * every other dragon is already down (a multiple death), or no lives are left
 * (a real game over). A multiple death charges a life per dragon.
 *
 * WHOSE DEATH: both call sites are inside Spyro's tick, and the tick override
 * swaps each shadow in for his, so the live dragon is the dying one. While
 * shadow k ticks, slot 0's state sits in k's shadow buffer and every other
 * shadow in its own. The tick override records which slot is ticking.
 */

#include "coop.h"
#include <openpete_game_structs.h>

/* Spyro struct offsets beyond those in coop.h (decompilation spyro.h). */
#define SPYRO_OFF_BODYROT_Z   0x00E
#define SPYRO_OFF_IDLE_TIMER  0x080
#define SPYRO_OFF_INVULN      0x160

/* CheckpointData (checkpoint.h). */
#define CHECKPOINT_OFF_STOOD  0x00  /* int: a checkpoint has been stood on */
#define CHECKPOINT_OFF_POS    0x50  /* x, y, z, then rotation at +0x5C */

/* g_Hud (hud.h). */
#define HUD_OFF_LIFE_STATE    0x02  /* HDS_Hidden 0, HDS_Opening 1 */
#define HUD_OFF_LIFE_PROGRESS 0x07
#define HUD_OFF_LIFE_COUNT    0x28

#define RESPAWN_INVULN   90       /* the engine's own i-frame count */
#define RESPAWN_BLINK    COOP_RESPAWN_BLINK_TICKS
#define BLINK_PERIOD     3        /* ticks shown, then ticks hidden */
#define RESPAWN_HEALTH   3

/* Grounding. Both constants are the game's own, and the first PS1 attempt got
   both wrong: 4096 is the MOBY probe reach and missed spawn points captured
   mid-descent, and omitting the stand height put him 356 units inside the
   floor, where collision bounced him out. */
#define PROBE_REACH      0x10000  /* pete.c:1487, Spyro's own floor probe */
#define STAND_HEIGHT     356      /* checkpoint.c:21, "accommodate Spyro's hitsphere" */

#define FAIRY_QUIET_RADIUS 0x800  /* outside her own 0x400 trigger radius */

/* The two retail call sites of TriggerRespawnOrGameOver, read from the
   executable. Both are reached only through Spyro's tick. */
#define RA_DEATH_IN_PHYSICS 0x80042F18u  /* func_80041670, called from the tick */
#define RA_DEATH_IN_TICK    0x8004A4E0u  /* func_8004A200 itself */

/* ------------------------------------------------------------------------
 * Spawn points
 * ---------------------------------------------------------------------- */

/* Called once per level entry, from player 2's seed. Two fallbacks for a
   death with no checkpoint stood on, because the game's checkpoint slot holds
   STALE coordinates after a portal exit (retail repairs that inside the
   reload we skip):
     arrival       wherever the game placed the dragon entering this level
     true start    this world's game-start spawn, captured only when the
                   arrival matches the checkpoint slot, which retail guarantees
                   exactly on a fresh load. After returning from a level this
                   still holds the homeworld's real start, so a respawn does
                   not land on the portal lip. */
void coop_capture_spawn(void) {
    CoopRespawnArena* R = coop_respawn_arena();
    int32_t* pos  = guest32(OP_GADDR_g_Spyro + SPYRO_OFF_POSITION);
    int32_t* slot = guest32(OP_GADDR_g_Checkpoint + CHECKPOINT_OFF_POS);

    R->arrival[0] = pos[0];
    R->arrival[1] = pos[1];
    R->arrival[2] = pos[2];
    R->arrival[3] = *guest8(OP_GADDR_g_Spyro + SPYRO_OFF_BODYROT_Z);

    int near = 1;
    for (int i = 0; i < 2; i++) {
        int32_t d = pos[i] - slot[i];
        if (d < -0x100 || d > 0x100)
            near = 0;
    }
    if (near) {
        R->start[0] = coop_level_id();
        memcpy(&R->start[1], slot, 4 * sizeof(int32_t));
    }
}

/* ------------------------------------------------------------------------
 * The save-fairy mute, ported from Sp1x2FairyMute.
 *
 * Respawning on a rescued dragon's pedestal opened the save prompt at once;
 * retail hides this because its reload rarely lands you on one. The level's
 * own guard fires only when the player is near the pad AND idling
 * (g_Spyro.m_idleTimer > 0), so holding that timer at zero fails the guard at
 * its own condition, before any setup. (Patching the fairy cutscene's entry
 * instead froze player 2 on PS1: the overlay freezes the player before it
 * calls that function. Suppress a sequence at its trigger, never at the
 * function it eventually calls.)
 *
 * Distance, not a timer: quiet while he stays near where he respawned,
 * re-armed once he walks away, which mirrors retail's own positional rule.
 * Only in the respawned dragon's own moby pass, or the other dragon, far
 * away, disarms it instantly.
 * ---------------------------------------------------------------------- */
void coop_fairy_mute(int player) {
    CoopRespawnArena* R = coop_respawn_arena();
    if (R->fairy_mute[0] != player + 1)
        return;
    int32_t* pos = guest32(OP_GADDR_g_Spyro + SPYRO_OFF_POSITION);
    int32_t dx = pos[0] - R->fairy_mute[1]; if (dx < 0) dx = -dx;
    int32_t dy = pos[1] - R->fairy_mute[2]; if (dy < 0) dy = -dy;
    if (dx + dy > FAIRY_QUIET_RADIUS)
        R->fairy_mute[0] = 0;                               /* left: re-arm */
    else
        *guest32(OP_GADDR_g_Spyro + SPYRO_OFF_IDLE_TIMER) = 0;
}

/* ------------------------------------------------------------------------
 * Player 1's Sparx after an individual respawn, ported from Sp1x2SparxHeal.
 *
 * Every level's code nulls g_Sparx when the dragonfly moby dies, and retail
 * only ever puts a new one back during the reload we now skip. So: if g_Sparx
 * is null, adopt the old fly if it is still alive mid-exit, otherwise spawn a
 * fresh one. Called with player 1 live, before his moby pass.
 *
 * ONLY AFTER ONE OF OUR RESPAWNS. PS1 healed whenever g_Sparx was null, but
 * it is also null while a living dragon has lost Sparx at zero health, and
 * when he eats a butterfly the game brings Sparx back itself. Healing then
 * could leave two. Our individual respawn is the one case retail never
 * covers, so it sets a flag and this acts only on it.
 * ---------------------------------------------------------------------- */
void coop_sparx_heal(CPUState* cpu) {
    CoopRespawnArena* R = coop_respawn_arena();
    if (!R->sparx_heal_pending)
        return;
    CoopMobyArena* M = coop_moby_arena();
    uint32_t* g_sparx = (uint32_t*)g_api->guest(OP_GADDR_g_Sparx);

    if (*g_sparx != 0) {
        Moby* fly = (Moby*)g_api->guest(*g_sparx);
        if (fly && (int8_t)fly->m_State < 0)
            *g_sparx = 0;                    /* adopted earlier, finished dying */
    }
    if (*g_sparx != 0) {
        R->sparx_heal_pending = 0;           /* he has one: nothing to heal */
        return;
    }
    if (*guest32(OP_GADDR_g_Spyro + SPYRO_OFF_HEALTH) <= 0)
        return;

    /* The last Sparx we saw can be null here: the game re-nulls g_Sparx during
       the passes. PS1 once deleted this check to save four bytes and "adopted"
       address zero, losing Sparx for good. */
    Moby* seen = M->sparx1_seen ? (Moby*)g_api->guest(M->sparx1_seen) : NULL;
    if (seen && (int8_t)seen->m_State >= 0) {
        *g_sparx = M->sparx1_seen;           /* mid-exit but alive: keep him */
        R->sparx_heal_pending = 0;
        return;
    }
    if (M->sparx_spawns_level >= 8)
        return;                              /* same safety net as player 2's */
    uint32_t spawn = *(uint32_t*)g_api->guest(OP_GADDR_g_SpawnMoby);
    if (spawn == 0)
        return;
    cpu->a0 = 120;                           /* Sparx's class */
    cpu->a1 = 0;
    g_api->call(cpu, spawn);
    if (cpu->v0 != 0) {
        *g_sparx = cpu->v0;
        M->sparx1_seen = cpu->v0;            /* ours, not a level rebuild */
        M->sparx_spawns_level++;
        R->sparx_heal_pending = 0;
        g_stats.sparx_heals++;
    }
}

/* ------------------------------------------------------------------------
 * The death override
 * ---------------------------------------------------------------------- */

/* Another dragon's health while `ticking` is the live one (see WHOSE DEATH). */
static int32_t other_health(int ticking, int slot) {
    int buffer = (slot == 0) ? ticking : slot;
    return *(int32_t*)(coop_shadow(buffer).spyro + SPYRO_OFF_HEALTH);
}

static void on_trigger_respawn(CPUState* cpu) {
    CoopArena* A = coop_arena();

    if ((cpu->ra != RA_DEATH_IN_PHYSICS && cpu->ra != RA_DEATH_IN_TICK) ||
        !coop_in_gameplay_tick() || !coop_enabled() || !coop_respawn_enabled() ||
        !A->ready) {
        g_api->base(cpu);                    /* stock: everyone respawns */
        return;
    }

    int      dying     = coop_ticking_player();
    int      shadows   = coop_seeded_shadows();
    int32_t* lives     = guest32(OP_GADDR_g_SpyroLifeCount);
    int32_t* hud_lives = guest32(OP_GADDR_g_Hud + HUD_OFF_LIFE_COUNT);

    int alive = 0, down = 0;
    for (int s = 0; s <= shadows; s++) {
        if (s == dying)
            continue;
        if (other_health(dying, s) < 0) down++;
        else                            alive++;
    }

    if (alive == 0 || *lives == 0) {
        /* Nobody to carry on: the stock sequence, as retail. A multiple death
           arrives here with the others already down, and the stock trigger
           charges one life for them all, so charge theirs now. Never below
           zero: with too few lives left, it is the game over it already was. */
        for (int i = 0; alive == 0 && i < down && *lives > 0; i++) {
            (*lives)--;
            *hud_lives = *lives;
            g_stats.double_deaths++;
        }
        g_api->base(cpu);
        return;
    }

    SavedRegs regs;
    save_regs(cpu, &regs);

    /* ---- the life ---- */
    (*lives)--;                              /* lives are shared */

    /* The HUD's own roll animation only counts UP, because in retail a lost
       life always reloads and the reload assigns the count. Assign it here,
       or the display rolls to 99 chasing a smaller number. */
    *hud_lives = *lives;

    /* Assigning the count makes the HUD think nothing changed, so ask it to
       open, the way it does itself when the counts differ, and reprint the
       digits, which only its roll animation otherwise redraws. Only from
       hidden, or an opening slide would jump. */
    uint8_t* hud = guest8(OP_GADDR_g_Hud);
    if (hud[HUD_OFF_LIFE_STATE] == 0) {
        hud[HUD_OFF_LIFE_PROGRESS] = 0;
        hud[HUD_OFF_LIFE_STATE]    = 1;      /* HDS_Opening */
        cpu->a0 = 8;                         /* the life counter's digit group */
        cpu->a1 = 2;                         /* two digits */
        cpu->a2 = (uint32_t)*lives;
        cpu->a3 = 1;                         /* plain mode, as HudReset calls it */
        g_api->call(cpu, OP_FNADDR_HudPrint);
    }

    /* ---- where ---- */
    CoopRespawnArena* R = coop_respawn_arena();
    int stood = *guest32(OP_GADDR_g_Checkpoint + CHECKPOINT_OFF_STOOD) != 0;
    const int32_t* spawn;
    const char* source;
    if (stood) {
        spawn = guest32(OP_GADDR_g_Checkpoint + CHECKPOINT_OFF_POS);
        source = "checkpoint";
    } else if (R->start[0] == coop_level_id()) {
        spawn = &R->start[1];
        source = "level start";
    } else {
        spawn = R->arrival;
        source = "arrival point";
    }
    int32_t spawn_pos[4];
    memcpy(spawn_pos, spawn, sizeof spawn_pos); /* copied before anything moves */

    int32_t* pos = guest32(OP_GADDR_g_Spyro + SPYRO_OFF_POSITION);
    pos[0] = spawn_pos[0];
    pos[1] = spawn_pos[1];
    pos[2] = spawn_pos[2];

    /* ---- on the ground ---- */
    cpu->a0 = OP_GADDR_g_Spyro + SPYRO_OFF_POSITION;
    cpu->a1 = PROBE_REACH;
    g_api->call(cpu, OP_FNADDR_func_8004D5EC);   /* FindFloorBelow */
    int32_t floor = (int32_t)cpu->v0;
    int grounded = 0;
    /* Accept only an answer inside the span searched. A failed probe or a
       sentinel keeps the stored height; a floor above him goes negative and
       fails the same unsigned test. */
    if ((uint32_t)(pos[2] - floor) <= (uint32_t)PROBE_REACH) {
        pos[2] = floor + STAND_HEIGHT;
        grounded = 1;
    }

    /* Anything that moves a live dragon moves the teleport detector's sample,
       or next frame reads as a level restart and reseeds the pair. */
    coop_resample_teleport();

    /* Slot 0's Sparx is tracked through g_Sparx, which the level nulled when
       it died, and only a reload would restore it. A shadow's is respawned by
       his own bookkeeping once his health is back. */
    if (dying == 0)
        R->sparx_heal_pending = 1;

    /* Arm the fairy mute AT the respawn point, not where he died. */
    R->fairy_mute[0] = dying + 1;
    R->fairy_mute[1] = spawn_pos[0];
    R->fairy_mute[2] = spawn_pos[1];

    /* ---- as the reload would leave him ---- */
    int32_t rot = spawn_pos[3];
    if (stood)
        rot >>= 4;                           /* loaders.c shifts for a checkpoint only */
    *guest8(OP_GADDR_g_Spyro + SPYRO_OFF_BODYROT_Z) = (uint8_t)rot;
    *guest32(OP_GADDR_g_Spyro + SPYRO_OFF_HEALTH) = RESPAWN_HEALTH;

    /* ResetSpyroState(1): clears the dying state and keeps the position,
       rotation and health just written, exactly as the reload uses it. */
    cpu->a0 = 1;
    g_api->call(cpu, OP_FNADDR_func_8004AC24);

    *guest32(OP_GADDR_g_Spyro + SPYRO_OFF_INVULN) = RESPAWN_INVULN;
    *guest32(OP_GADDR_g_Spyro + SPYRO_OFF_RESPAWN_BLINK) = RESPAWN_BLINK;
    coop_effect_play(cpu, FX_DEFAULT_LAYERS, coop_physical_player(dying));

    load_regs(cpu, &regs);
    g_stats.individual_respawns++;
    coop_log(OP_MOD_LOG_INFO,
             "player %d respawned on his own at the %s (%d,%d,%d)%s, %d lives left",
             coop_physical_player(dying) + 1, source, pos[0], pos[1], pos[2],
             grounded ? ", grounded" : ", floor not found, height kept", *lives);
    /* No base(): the stock trigger never runs on this path. */
}

/* ------------------------------------------------------------------------
 * BLINKING IN (2026-09-13, at the user's request). A dragon who respawns on his
 * own flickers for RESPAWN_BLINK ticks, so he reads as arriving rather than
 * popping into place. The countdown lives in his own Spyro struct, so each
 * dragon blinks on his own, and the draw hook leaves his model out on the
 * "off" ticks (his shadow stays, which grounds him while he flickers).
 * ---------------------------------------------------------------------- */
void coop_respawn_blink_tick(void) {
    int32_t* blink = guest32(OP_GADDR_g_Spyro + SPYRO_OFF_RESPAWN_BLINK);
    if (*blink < 0 || *blink > RESPAWN_BLINK)
        *blink = 0;                          /* never trust a stray value */
    else if (*blink > 0)
        (*blink)--;
}

int coop_respawn_blink_hidden(void) {
    int32_t blink = *guest32(OP_GADDR_g_Spyro + SPYRO_OFF_RESPAWN_BLINK);
    return blink > 0 && blink <= RESPAWN_BLINK && ((blink / BLINK_PERIOD) & 1);
}

int coop_respawn_install(void) {
    if (g_api->override_name(g_self, "func_8002C85C", on_trigger_respawn) != 0) {
        coop_log(OP_MOD_LOG_ERROR, "could not install individual respawn");
        return 1;
    }
    return 0;
}
