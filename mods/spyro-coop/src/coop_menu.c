/**
 * @file coop_menu.c
 * @brief The in-game Multiplayer and Colors pages, reached from the pause list.
 *
 * Ported from the PS1 build's Sp1x2Menu.c, with one change the user chose
 * (2026-09-13): the menu is a row in the pause list itself, MULTIPLAYER, above
 * QUIT / EXIT LEVEL, rather than SQUARE on the options screen. Everything is
 * driven by the controller, so the mod is playable from a couch.
 *
 * NO INSTRUCTION PATCHES. The PS1 build flipped two branches inside the pause
 * draw so the big box was drawn for its page. A mod cannot patch instructions
 * here, so the same effect comes from call sites, all read from SCUS_942.28:
 *
 *   - the pause update, func_8002E12C, is wrapped. Navigation onto our row and
 *     every press on our pages is handled before the stock update runs, and the
 *     presses we used are hidden from it for that one call, so it still does
 *     its housekeeping (HUD, shimmer, idle counter) and nothing else;
 *   - the box's border lines, func_8001844C, are called with constant corners,
 *     and its fill quad is linked by func_800168DC. Their arguments are
 *     rewritten at those call sites, which grows the small box by a row, or
 *     widens it to the big box while one of our pages is open;
 *   - the text builder, func_800181AC, is called for each pause-list row. At
 *     the last row we draw MULTIPLAYER and move the last row down; while a page
 *     is open the stock rows are skipped and the page is drawn there instead.
 *
 * The pause substate stays 0 throughout, so the game's own options and quit
 * screens are untouched.
 *
 * THE SHIMMER. The stock draw rotates the selected row's letters from two
 * registers, s2 (first letter moby) and s5 (letter count), set when its cursor
 * matches a row. Our row index matches none, so at the last row we set them
 * ourselves: s2/s5 to our letters on the pause list, and s5 = 0 on our pages,
 * which wobble their own letters. (The PS1 build left s5 unset and relied on it
 * happening to be zero.)
 */

#include "coop.h"

/* ---- call sites (return addresses) in func_8001A40C, the pause draw ---- */
#define RA_ITEM_CONTINUE   0x8001B3F8u
#define RA_ITEM_OPTIONS    0x8001B448u
#define RA_ITEM_INVENTORY  0x8001B498u
#define RA_ITEM_QUIT_FLY   0x8001B4F8u   /* "QUIT", flight levels */
#define RA_ITEM_EXIT_LEVEL 0x8001B57Cu   /* "EXIT LEVEL", levels */
#define RA_ITEM_QUIT_GAME  0x8001B5C8u   /* "QUIT GAME", homeworlds */
#define RA_BOX_FILL_LINK   0x8001A868u   /* func_800168DC(fill quad), the box's grey fill */
#define RA_BOX_TOP         0x8001A904u   /* small box, (140,67)-(372,67) */
#define RA_BOX_RIGHT       0x8001A92Cu   /* (372,67)-(372,bottom) */
#define RA_BOX_BOTTOM      0x8001A940u   /* (372,bottom)-(140,bottom) */
#define RA_BOX_LEFT        0x8001A954u   /* (x,bottom)-(x,67), shared with the big box */

/* ---- pad bits, g_Pad.m_Down ---- */
#define PAD_L2       (1 << 0)
#define PAD_R2       (1 << 1)
#define PAD_TRIANGLE (1 << 4)
#define PAD_CROSS    (1 << 6)
#define PAD_SQUARE   (1 << 7)
#define PAD_START    (1 << 11)
#define PAD_UP       (1 << 12)
#define PAD_RIGHT    (1 << 13)
#define PAD_DOWN     (1 << 14)
#define PAD_LEFT     (1 << 15)

/* ---- layout ---- */
#define ROW_MP          4      /* the pause cursor value of our row; the stock rows are 0..3 */
#define LIST_MP_Y       164    /* our row takes the last row's place... */
#define LIST_LAST_Y     182
#define SMALL_BOTTOM    194    /* stock 176, plus one row of 18 */
#define BIG_LEFT        84     /* the options screen's big box */
#define BIG_RIGHT       428
#define BIG_BOTTOM      198
#define SHADE_NORMAL    11     /* every stock menu */
#define SHADE_DISABLED  12     /* grey, verified on PS1 */
#define LETTER_STRIDE   88     /* sizeof(Moby) */
#define LETTER_ROT_Z    70     /* Moby.m_Rotation.z, one byte */
#define SND_MOVE        45     /* sound table: menuCursor */
#define SND_PICK        46     /* sound table: menuConfirm */
#define SPU_SOUND_TABLE 0x2CC  /* g_Spu.m_SoundTable, a guest pointer */

enum { PAGE_NONE = 0, PAGE_MULTIPLAYER = 1, PAGE_COLORS = 2 };
enum { MP_PLAYERS, MP_RESPAWN, MP_COLORS, MP_SPLIT, MP_DONE, MP_ROWS };
#define COLOR_DONE 8           /* 0..3 player 1, 4..7 player 2, then DONE */
#define COLOR_ROWS 9

static uint32_t g_menu_vaddr;
static int      g_in_menu_call;    /* inside a call we made: stock behaviour */

static CoopMenuArena* M(void) { return (CoopMenuArena*)g_api->guest(g_menu_vaddr); }

/* ------------------------------------------------------------------------
 * Calling the game
 * ---------------------------------------------------------------------- */

/* Scratch in the menu block for strings and vectors the game reads by
   pointer. Reset at the start of each hook; the game copies what it needs. */
static uint32_t scratch_put(const void* src, uint32_t n) {
    CoopMenuArena* m = M();
    uint32_t at = (m->scratch_used + 3u) & ~3u;
    if (at + n > sizeof m->scratch)
        at = 0;                              /* cannot happen with this layout */
    memcpy(m->scratch + at, src, n);
    m->scratch_used = at + n;
    return g_menu_vaddr + (uint32_t)offsetof(CoopMenuArena, scratch) + at;
}

static void game_call(CPUState* cpu, uint32_t fn) {
    g_in_menu_call = 1;
    g_api->call(cpu, fn);
    g_in_menu_call = 0;
}

static int letters(const char* s) {
    int n = 0;
    for (; *s; s++)
        if (*s != ' ')
            n++;                             /* the builder makes no moby for a space */
    return n;
}

typedef struct { uint32_t mobys; int count; } Built;

/* func_800181AC(text, &pos, &spacing, size, shade). The fifth argument goes in
   the caller's outgoing-argument slot, as the pause draw itself does. */
static Built text(CPUState* cpu, const char* s, int x, int y, int z, int advance,
                  int size, int shade) {
    int32_t pos[3] = { x, y, z };
    int32_t spacing[3] = { advance, 1, 0x1600 };
    cpu->a0 = scratch_put(s, (uint32_t)strlen(s) + 1);
    cpu->a1 = scratch_put(pos, sizeof pos);
    cpu->a2 = scratch_put(spacing, sizeof spacing);
    cpu->a3 = (uint32_t)size;
    cpu->write_word(cpu->sp + 16, (uint32_t)shade);
    game_call(cpu, OP_FNADDR_func_800181AC);
    Built b = { *(uint32_t*)g_api->guest(OP_GADDR_g_HudMobys), letters(s) };
    return b;
}

static Built text_centred(CPUState* cpu, const char* s, int cx, int y, int shade) {
    return text(cpu, s, cx - (int)strlen(s) * 15 / 2, y, 0x1400, 15, 16, shade);
}

static void box_line(CPUState* cpu, int x0, int y0, int x1, int y1) {
    cpu->a0 = (uint32_t)x0; cpu->a1 = (uint32_t)y0;
    cpu->a2 = (uint32_t)x1; cpu->a3 = (uint32_t)y1;
    game_call(cpu, OP_FNADDR_func_8001844C);
}

static void chime(CPUState* cpu, int which) {
    uint32_t table = *(uint32_t*)g_api->guest(OP_GADDR_g_Spu + SPU_SOUND_TABLE);
    if (table == 0)
        return;
    SavedRegs r;
    save_regs(cpu, &r);
    cpu->a0 = *guest8(table + (uint32_t)which);
    cpu->a1 = 0;
    cpu->a2 = 16;
    cpu->a3 = 0;
    game_call(cpu, OP_FNADDR_PlaySound);
    load_regs(cpu, &r);
}

/* The stock shimmer: each letter turns on a cosine, phase-shifted along the
   row so it ripples. phase_base continues a label's ripple into its value. */
static void wobble(Built b, int phase_base) {
    if (b.mobys == 0)
        return;
    int32_t ticks = *guest32(OP_GADDR_D_800758B8);
    const int16_t* cos8 = (const int16_t*)g_api->guest(OP_GADDR_D_8006CC78);
    for (int i = 0; i < b.count; i++) {
        int phase = (ticks * 8 + (phase_base + i) * 12) & 0xFF;
        *guest8(b.mobys + (uint32_t)(i * LETTER_STRIDE + LETTER_ROT_Z)) =
            (uint8_t)((cos8[phase] * 3) >> 9);
    }
}

static int32_t* pause_cursor(void)   { return guest32(OP_GADDR_D_80075720); }
static int32_t  pause_substate(void) { return *guest32(OP_GADDR_D_800757C8); }
static int      in_pause_list(void)  { return coop_gamestate() == 2 && pause_substate() == 0; }

/* ------------------------------------------------------------------------
 * Input: the pause update
 * ---------------------------------------------------------------------- */

static void back_to_list(CPUState* cpu) {
    M()->page = PAGE_NONE;
    *pause_cursor() = ROW_MP;
    chime(cpu, SND_MOVE);
}

static void multiplayer_adjust(CPUState* cpu, int delta) {
    CoopMenuArena* m = M();
    switch (m->cursor) {
    case MP_PLAYERS:
        g_settings.players = (g_settings.players == 2) ? 1 : 2;
        break;
    case MP_RESPAWN:
        g_settings.respawn_modern = !g_settings.respawn_modern;
        break;
    case MP_COLORS:
        m->page = PAGE_COLORS;
        m->cursor = 0;
        chime(cpu, SND_PICK);
        return;
    case MP_SPLIT:
        g_settings.split_vertical = !g_settings.split_vertical;
        break;
    default:
        return;
    }
    (void)delta;
    coop_settings_changed();
    chime(cpu, SND_PICK);
}

static void colors_adjust(CPUState* cpu, int delta) {
    CoopMenuArena* m = M();
    if (m->cursor >= COLOR_DONE)
        return;
    uint8_t* v = &g_settings.color[m->cursor / 4][m->cursor % 4];
    *v = (uint8_t)((*v + delta) & 0xFF);     /* wraps, as on PS1 */
    coop_settings_changed();
    chime(cpu, SND_PICK);
}

static void page_input(CPUState* cpu, int32_t down) {
    CoopMenuArena* m = M();
    int colors = (m->page == PAGE_COLORS);
    int rows = colors ? COLOR_ROWS : MP_ROWS;

    if (down & PAD_DOWN) {
        m->cursor = (m->cursor + 1) % rows;
        chime(cpu, SND_MOVE);
    } else if (down & PAD_UP) {
        m->cursor = (m->cursor + rows - 1) % rows;
        chime(cpu, SND_MOVE);
    } else if (down & (PAD_LEFT | PAD_RIGHT)) {
        int d = (down & PAD_RIGHT) ? 1 : -1;
        if (colors) colors_adjust(cpu, d);
        else        multiplayer_adjust(cpu, d);
    } else if (colors && (down & (PAD_L2 | PAD_R2))) {
        colors_adjust(cpu, (down & PAD_R2) ? 16 : -16);   /* coarse */
    } else if (colors && (down & PAD_SQUARE)) {
        if (m->cursor < COLOR_DONE) {
            coop_settings_reset_color(m->cursor / 4);     /* the column you stand in */
            chime(cpu, SND_PICK);
        }
    } else if (down & PAD_TRIANGLE) {
        if (colors) { m->page = PAGE_MULTIPLAYER; m->cursor = MP_COLORS; chime(cpu, SND_MOVE); }
        else        back_to_list(cpu);
    } else if (down & PAD_CROSS) {
        if (m->cursor == rows - 1) {         /* DONE: one level back, never two */
            if (colors) { m->page = PAGE_MULTIPLAYER; m->cursor = MP_COLORS; chime(cpu, SND_MOVE); }
            else        back_to_list(cpu);
        } else if (colors) {
            colors_adjust(cpu, 1);
        } else {
            multiplayer_adjust(cpu, 1);
        }
    }
}

static void on_pause_update(CPUState* cpu) {
    CoopMenuArena* m = M();
    int32_t* down_p = guest32(OP_GADDR_g_Pad);          /* g_Pad.m_Down */
    int32_t  down   = *down_p;

    /* A fresh pause (the timer restarts at 0) or the options/quit screens:
       our pages are closed and the stock update runs untouched. */
    if (*guest32(OP_GADDR_D_800758B8) < 5 || !in_pause_list()) {
        m->page = PAGE_NONE;
        g_api->base(cpu);
        return;
    }

    int32_t consumed = 0;
    int32_t* cur = pause_cursor();

    if (m->page != PAGE_NONE) {
        page_input(cpu, down);
        consumed = down;                     /* the stock list sees nothing */
    } else {
        int c = *cur;
        if (down & PAD_DOWN) {
            if (c == 2)           { *cur = ROW_MP; consumed |= PAD_DOWN; }
            else if (c == ROW_MP) { *cur = 3;      consumed |= PAD_DOWN; }
        } else if (down & PAD_UP) {
            if (c == 3)           { *cur = ROW_MP; consumed |= PAD_UP; }
            else if (c == ROW_MP) { *cur = 2;      consumed |= PAD_UP; }
        }
        if (consumed) {
            *guest32(OP_GADDR_D_8007568C) = 0;   /* the idle counter, as stock */
            chime(cpu, SND_MOVE);
        } else if (c == ROW_MP && (down & (PAD_CROSS | PAD_START))) {
            m->page = PAGE_MULTIPLAYER;
            m->cursor = 0;
            consumed = PAD_CROSS | PAD_START;
            chime(cpu, SND_PICK);
            g_stats.menu_opens++;
        }
    }

    *down_p = down & ~consumed;
    g_api->base(cpu);
    if (*down_p == (down & ~consumed))
        *down_p = down;                      /* put the frame's presses back */
}

/* ------------------------------------------------------------------------
 * Drawing: the pages
 * ---------------------------------------------------------------------- */

static const uint8_t k_swatch_base[3] = { 0x78, 0x58, 0xA8 };   /* his skin, roughly */

/* A flat quad showing what the tint does to him: Spyro's colour blended
   toward the chosen one by the strength, the way the filter blends. The PS1
   build's answer to the preview dragons that never drew. */
static void swatch(CPUState* cpu, int player, int cx) {
    const uint8_t* c = g_settings.color[player];
    uint32_t f4 = *(uint32_t*)g_api->guest(OP_GADDR_D_800757B0);   /* primitive cursor */
    uint8_t* p = guest8(f4);
    int x0 = cx - 22, x1 = cx + 22, y0 = 38, y1 = 60;

    memset(p, 0, 24);
    uint32_t tag = 0x05000000u;
    memcpy(p, &tag, 4);
    for (int i = 0; i < 3; i++)
        p[4 + i] = (uint8_t)(k_swatch_base[i] + ((c[i] - k_swatch_base[i]) * c[3]) / 255);
    p[7] = 0x28;                             /* POLY_F4, opaque */
    int16_t xy[8] = { (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y0,
                      (int16_t)x0, (int16_t)y1, (int16_t)x1, (int16_t)y1 };
    memcpy(p + 8, xy, sizeof xy);

    cpu->a0 = f4;
    game_call(cpu, OP_FNADDR_func_800168DC);            /* link it */
    *(uint32_t*)g_api->guest(OP_GADDR_D_800757B0) = f4 + 24;

    /* Framed with the box's own line routine, for the same shimmering gold. */
    box_line(cpu, x0, y0, x1, y0);
    box_line(cpu, x1, y0, x1, y1);
    box_line(cpu, x1, y1, x0, y1);
    box_line(cpu, x0, y1, x0, y0);
}

static void hint(CPUState* cpu, const char* s) {
    /* Narrower spacing so a long line fits under the box, as on PS1. */
    text(cpu, s, 256 - (int)strlen(s) * 12 / 2, 206, 0x1100, 12, 13, SHADE_NORMAL);
}

static void draw_multiplayer(CPUState* cpu) {
    CoopMenuArena* m = M();
    static const int y[MP_ROWS] = { 120, 134, 148, 162, 176 };
    const char* values[MP_ROWS] = {
        g_settings.players == 2 ? "2" : "1",
        g_settings.respawn_modern ? "MODERN" : "ORIGINAL",
        NULL,
        g_settings.split_vertical ? "VERTICAL" : "HORIZONTAL",
        NULL,
    };
    static const char* const labels[MP_ROWS] = { "PLAYERS", "RESPAWN", "COLORS", "SPLIT", "DONE" };

    text_centred(cpu, "MULTIPLAYER", 256, 104, SHADE_NORMAL);
    Built sel_label = { 0, 0 }, sel_value = { 0, 0 };
    for (int r = 0; r < MP_ROWS; r++) {
        int shade = (r == MP_SPLIT) ? SHADE_DISABLED : SHADE_NORMAL;
        Built l = text(cpu, labels[r], 114, y[r], 0x1400, 15, 16, shade);
        Built v = { 0, 0 };
        if (values[r])
            v = text(cpu, values[r], 272, y[r], 0x1400, 15, 16, shade);
        if (r == m->cursor) { sel_label = l; sel_value = v; }
    }
    if (m->cursor == MP_SPLIT)
        hint(cpu, "SPLIT SCREEN IS NOT AVAILABLE YET");
    else if (m->cursor == MP_RESPAWN)
        hint(cpu, g_settings.respawn_modern ? "ONLY THE DRAGON WHO FELL RESPAWNS"
                                            : "EVERY DEATH RESTARTS BOTH DRAGONS");
    wobble(sel_label, 0);
    wobble(sel_value, sel_label.count);
}

#define COL1_X 282
#define COL2_X 372

static Built number(CPUState* cpu, int v, int cx, int y) {
    char s[4];
    int i = 0;
    if (v >= 100) s[i++] = (char)('0' + v / 100);
    if (v >= 10)  s[i++] = (char)('0' + (v / 10) % 10);
    s[i++] = (char)('0' + v % 10);
    s[i] = 0;
    return text_centred(cpu, s, cx, y, SHADE_NORMAL);
}

static void draw_colors(CPUState* cpu) {
    CoopMenuArena* m = M();
    static const char* const labels[4] = { "RED", "GREEN", "BLUE", "STRENGTH" };
    static const int y[4] = { 134, 147, 160, 173 };

    text_centred(cpu, "COLORS", 256, 104, SHADE_NORMAL);
    text_centred(cpu, "P1", COL1_X, 120, SHADE_NORMAL);
    text_centred(cpu, "P2", COL2_X, 120, SHADE_NORMAL);

    Built sel = { 0, 0 };
    for (int k = 0; k < 4; k++) {
        text(cpu, labels[k], 114, y[k], 0x1400, 15, 16, SHADE_NORMAL);
        Built a = number(cpu, g_settings.color[0][k], COL1_X, y[k]);
        Built b = number(cpu, g_settings.color[1][k], COL2_X, y[k]);
        if (m->cursor == k)     sel = a;
        if (m->cursor == k + 4) sel = b;
    }
    Built done = text(cpu, "DONE", 114, 186, 0x1400, 15, 16, SHADE_NORMAL);
    if (m->cursor == COLOR_DONE)
        sel = done;

    hint(cpu, "L2 R2 FAST  SQUARE RESET");
    swatch(cpu, 0, COL1_X);
    swatch(cpu, 1, COL2_X);
    wobble(sel, 0);
}

/* ------------------------------------------------------------------------
 * Drawing: hooks
 * ---------------------------------------------------------------------- */

static int is_last_item(uint32_t ra) {
    return ra == RA_ITEM_QUIT_FLY || ra == RA_ITEM_EXIT_LEVEL || ra == RA_ITEM_QUIT_GAME;
}
static int is_list_item(uint32_t ra) {
    return ra == RA_ITEM_CONTINUE || ra == RA_ITEM_OPTIONS || ra == RA_ITEM_INVENTORY ||
           is_last_item(ra);
}

static void on_text_sprites(CPUState* cpu) {
    if (g_in_menu_call || !is_list_item(cpu->ra) || !in_pause_list()) {
        g_api->base(cpu);
        return;
    }
    CoopMenuArena* m = M();
    m->scratch_used = 0;

    if (m->page != PAGE_NONE) {
        /* Our page owns the box: the stock rows are not built at all. */
        if (is_last_item(cpu->ra)) {
            if (m->page == PAGE_COLORS) draw_colors(cpu);
            else                        draw_multiplayer(cpu);
            cpu->s5 = 0;                     /* no stock shimmer on our letters */
        }
        return;
    }
    if (!is_last_item(cpu->ra)) {
        g_api->base(cpu);                    /* CONTINUE, OPTIONS, INVENTORY: stock */
        return;
    }

    /* The last pause row: MULTIPLAYER first, in its place, then the row itself
       one line lower. Ours first, so g_HudMobys is the stock row's again when
       the stock code reads it for its own shimmer.

       THE ROW IS REBUILT, NOT MOVED. Its position vector is the pause draw's
       one stack Vector3D, shared by every row, and writing the new y into it
       put ALL FOUR rows on that line (seen in both renderers, 2026-09-13): the
       letters take their position from it after this call, not during it. So
       the row gets its own copy of the vector, with only y changed. */
    SavedRegs r;
    save_regs(cpu, &r);
    uint32_t arg5 = cpu->read_word(cpu->sp + 16);
    int32_t  row_pos[3];
    memcpy(row_pos, guest32(cpu->a1), sizeof row_pos);

    const char* label = "MULTIPLAYER";
    Built mp = text(cpu, label, 263 - 8 * (int)strlen(label), LIST_MP_Y, 0x1100, 16, 18,
                    SHADE_NORMAL);

    row_pos[1] = LIST_LAST_Y;
    cpu->a0 = r.a0;                          /* the row's own string, spacing and size */
    cpu->a1 = scratch_put(row_pos, sizeof row_pos);
    cpu->a2 = r.a2;
    cpu->a3 = r.a3;
    cpu->write_word(cpu->sp + 16, arg5);
    game_call(cpu, OP_FNADDR_func_800181AC);
    load_regs(cpu, &r);

    if (*pause_cursor() == ROW_MP) {
        cpu->s2 = mp.mobys;                  /* the stock loop shimmers these... */
        cpu->s5 = (uint32_t)mp.count;        /* ...this many */
    }
}

static void on_box_line(CPUState* cpu) {
    uint32_t ra = cpu->ra;
    if (g_in_menu_call || !in_pause_list() ||
        (ra != RA_BOX_TOP && ra != RA_BOX_RIGHT &&
         ra != RA_BOX_BOTTOM && ra != RA_BOX_LEFT)) {
        g_api->base(cpu);
        return;
    }
    int big = (M()->page != PAGE_NONE);
    int bottom = big ? BIG_BOTTOM : SMALL_BOTTOM;

    switch (ra) {
    case RA_BOX_TOP:
        if (big) { cpu->a0 = BIG_LEFT; cpu->a2 = BIG_RIGHT; }
        break;
    case RA_BOX_RIGHT:
        if (big) { cpu->a0 = BIG_RIGHT; cpu->a2 = BIG_RIGHT; }
        cpu->a3 = (uint32_t)bottom;
        break;
    case RA_BOX_BOTTOM:
        if (big) { cpu->a0 = BIG_RIGHT; cpu->a2 = BIG_LEFT; }
        cpu->a1 = (uint32_t)bottom;
        cpu->a3 = (uint32_t)bottom;
        break;
    case RA_BOX_LEFT:
        if (big) { cpu->a0 = BIG_LEFT; cpu->a2 = BIG_LEFT; }
        cpu->a1 = (uint32_t)bottom;
        break;
    }
    g_api->base(cpu);
}

/* func_800168DC links a primitive into the frame. Only the pause box's fill is
   touched: its corners are set, and the native renderer takes a copy AT THIS
   CALL, so they must be changed before it (editing the quad afterwards moved
   it in PsyCross and not in the native view). */
static void on_link_prim(CPUState* cpu) {
    if (cpu->ra != RA_BOX_FILL_LINK || g_in_menu_call || !in_pause_list()) {
        g_api->base(cpu);
        return;
    }
    int big = (M()->page != PAGE_NONE);
    int16_t* xy = (int16_t*)g_api->guest(cpu->a0 + 8);   /* POLY_F4 x/y pairs */
    if (big) {
        xy[0] = BIG_LEFT;  xy[2] = BIG_RIGHT;
        xy[4] = BIG_LEFT;  xy[6] = BIG_RIGHT;
    }
    xy[5] = (int16_t)(big ? BIG_BOTTOM : SMALL_BOTTOM);  /* y2 */
    xy[7] = xy[5];                                         /* y3 */
    g_api->base(cpu);
}

int coop_menu_install(uint32_t menu_vaddr) {
    g_menu_vaddr = menu_vaddr;
    if (g_api->override_name(g_self, "func_8002E12C", on_pause_update) != 0 ||
        g_api->override_name(g_self, "func_800181AC", on_text_sprites) != 0 ||
        g_api->override_name(g_self, "func_8001844C", on_box_line) != 0 ||
        g_api->override_name(g_self, "func_800168DC", on_link_prim) != 0) {
        coop_log(OP_MOD_LOG_ERROR, "could not install the Multiplayer menu");
        return 1;
    }
    return 0;
}
