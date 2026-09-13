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
 *     resizes it for one of our pages;
 *   - the big title, func_80017FE4 ("PAUSED"), reads MULTIPLAYER on that page
 *     and is left out on the Colors page, whose swatches take its row;
 *   - the text builder, func_800181AC, is called for each pause-list row. At
 *     the last row we draw MULTIPLAYER and the last row one line lower; while a
 *     page is open the stock rows are skipped and the page is drawn there.
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
#define RA_BOX_SEPARATOR   0x8001A888u   /* the short line under the title, (224,97)-(288,97) */
#define RA_BOX_TOP         0x8001A904u   /* small box, (140,67)-(372,67) */
#define RA_BOX_RIGHT       0x8001A92Cu   /* (372,67)-(372,bottom) */
#define RA_BOX_BOTTOM      0x8001A940u   /* (372,bottom)-(140,bottom) */
#define RA_BOX_LEFT        0x8001A954u   /* (x,bottom)-(x,67), shared with the big box */
#define RA_TITLE           0x8001A988u   /* func_80017FE4("PAUSED", {186,82,3072}, 28, 11) */

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
#define LIST_LAST_Y     182    /* ...and the last row moves down one */
#define SMALL_BOTTOM    194    /* stock 176, plus one row of 18 */
#define SHADE_NORMAL    11     /* every stock menu */
#define SHADE_DISABLED  12     /* grey, verified on PS1 (pale purple in the native view) */
#define LETTER_STRIDE   88     /* sizeof(Moby) */
#define LETTER_ROT_Z    70     /* Moby.m_Rotation.z, one byte */
#define SND_MOVE        45     /* sound table: menuCursor */
#define SND_PICK        46     /* sound table: menuConfirm */
#define SPU_SOUND_TABLE 0x2CC  /* g_Spu.m_SoundTable, a guest pointer */

enum { PAGE_NONE = 0, PAGE_MULTIPLAYER = 1, PAGE_COLORS = 2 };

/* Settings first, then the page that leads elsewhere, then the way out. */
enum { MP_PLAYERS, MP_RESPAWN, MP_SPLIT, MP_COLORS, MP_DONE, MP_ROWS };

/* Colors: cursor = player * 4 + channel for the cells, then DONE. */
#define COLOR_CELLS COOP_MAX_PLAYERS * 4
#define COLOR_DONE  COLOR_CELLS

typedef struct { int left, right, bottom; } Box;

static const Box k_box_multiplayer = { 84, 428, 188 };  /* the options screen's width */
static const Box k_box_colors      = { 36, 476, 196 };  /* wide enough for four columns */

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

/* How far the last letter's centre lies from the first's, by the builder's own
   rules (func_800181AC, read from the disassembly): a letter's position is its
   CENTRE; a space moves on three quarters of the spacing; and the first
   letter, a letter after a space or a digit, and ! or ? move on by the SIZE
   instead of the spacing. Centring on len * spacing, as before, put every line
   half a letter left and lines with spaces further still. */
static int text_span(const char* s, int advance, int size) {
    int x = 0, last = 0, wide = 1;
    for (; *s; s++) {
        if (*s == ' ') {
            int v = advance * 3;
            x += (v < 0 ? v + 3 : v) >> 2;
            wide = 1;
            continue;
        }
        last = x;
        x += (wide || *s == '!' || *s == '?') ? size : advance;
        wide = (*s >= '0' && *s <= '9');
    }
    return last;
}

static Built text_centred(CPUState* cpu, const char* s, int cx, int y, int shade) {
    return text(cpu, s, cx - text_span(s, 15, 16) / 2, y, 0x1400, 15, 16, shade);
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

static const Box* page_box(void) {
    switch (M()->page) {
    case PAGE_MULTIPLAYER: return &k_box_multiplayer;
    case PAGE_COLORS:      return &k_box_colors;
    default:               return NULL;
    }
}

/* ------------------------------------------------------------------------
 * Input: the pause update
 * ---------------------------------------------------------------------- */

static void back_to_list(CPUState* cpu) {
    M()->page = PAGE_NONE;
    *pause_cursor() = ROW_MP;
    chime(cpu, SND_MOVE);
}

static void back_to_multiplayer(CPUState* cpu) {
    M()->page = PAGE_MULTIPLAYER;
    M()->cursor = MP_COLORS;
    chime(cpu, SND_MOVE);
}

static void multiplayer_adjust(CPUState* cpu) {
    CoopMenuArena* m = M();
    switch (m->cursor) {
    case MP_PLAYERS:
        g_settings.players = (g_settings.players == 2) ? 1 : 2;
        break;
    case MP_RESPAWN:
        g_settings.respawn_modern = !g_settings.respawn_modern;
        break;
    case MP_SPLIT:
        g_settings.split_vertical = !g_settings.split_vertical;
        break;
    case MP_COLORS:
        m->page = PAGE_COLORS;
        m->cursor = 0;
        chime(cpu, SND_PICK);
        return;
    default:
        return;
    }
    coop_settings_changed();
    chime(cpu, SND_PICK);
}

/* A column belongs to a player who is in the game. The others are drawn grey
   and the cursor steps over them. */
static int color_cell_active(int cell) {
    return cell >= COLOR_DONE || cell / 4 < g_settings.players;
}

static int color_step(int cell, int dir) {
    for (int i = 0; i <= COLOR_DONE; i++) {
        cell = (cell + dir + COLOR_DONE + 1) % (COLOR_DONE + 1);
        if (color_cell_active(cell))
            return cell;
    }
    return COLOR_DONE;
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

    if (down & (PAD_DOWN | PAD_UP)) {
        int dir = (down & PAD_DOWN) ? 1 : -1;
        m->cursor = colors ? color_step(m->cursor, dir)
                           : (m->cursor + dir + MP_ROWS) % MP_ROWS;
        chime(cpu, SND_MOVE);
    } else if (down & (PAD_LEFT | PAD_RIGHT)) {
        if (colors) colors_adjust(cpu, (down & PAD_RIGHT) ? 1 : -1);
        else        multiplayer_adjust(cpu);
    } else if (colors && (down & (PAD_L2 | PAD_R2))) {
        colors_adjust(cpu, (down & PAD_R2) ? 16 : -16);   /* coarse */
    } else if (colors && (down & PAD_SQUARE)) {
        if (m->cursor < COLOR_DONE) {
            coop_settings_reset_color(m->cursor / 4);     /* the column you stand in */
            chime(cpu, SND_PICK);
        }
    } else if (down & PAD_TRIANGLE) {
        if (colors) back_to_multiplayer(cpu);
        else        back_to_list(cpu);
    } else if (down & PAD_CROSS) {
        if (colors) {
            if (m->cursor == COLOR_DONE) back_to_multiplayer(cpu);   /* one level, never two */
            else                         colors_adjust(cpu, 1);
        } else {
            if (m->cursor == MP_DONE) back_to_list(cpu);
            else                      multiplayer_adjust(cpu);
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

static void hint(CPUState* cpu, const char* s) {
    /* Narrower spacing so a long line fits under the box, as on PS1. */
    text(cpu, s, 256 - text_span(s, 12, 13) / 2, 206, 0x1100, 12, 13, SHADE_NORMAL);
}

static void draw_multiplayer(CPUState* cpu) {
    CoopMenuArena* m = M();
    static const int y[MP_ROWS] = { 114, 128, 142, 156, 170 };
    static const char* const labels[MP_ROWS] = { "PLAYERS", "RESPAWN", "SPLIT", "COLORS", "DONE" };
    const char* values[MP_ROWS] = {
        g_settings.players == 2 ? "2" : "1",
        g_settings.respawn_modern ? "MODERN" : "ORIGINAL",
        g_settings.split_vertical ? "VERTICAL" : "HORIZONTAL",
        NULL,
        NULL,
    };

    /* The page's title is the big one, drawn in place of PAUSED. */
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
        hint(cpu, g_settings.respawn_modern ? "DEATH CAUSES INDIVIDUAL RESPAWN"
                                            : "DEATH RESTARTS BOTH DRAGONS");
    wobble(sel_label, 0);
    wobble(sel_value, sel_label.count);
}

/* Colors page columns, one per player, and the rows. */
static const int k_col_x[COOP_MAX_PLAYERS] = { 216, 280, 344, 408 };
#define COLOR_LABEL_X  48
#define SWATCH_TOP     72     /* in the title row, where PAUSED would be */
#define SWATCH_BOTTOM  92
#define SWATCH_HALF_W  22

static const uint8_t k_swatch_base[3] = { 0x78, 0x58, 0xA8 };   /* his skin, roughly */

/* The window's width over its height, from the present hook. Host state and
   display-only: it moves where a swatch quad is drawn and nothing else. */
static volatile float g_window_aspect;

static void on_present(const openpete_present_ctx_t* ctx) {
    g_window_aspect = ctx->aspect;
}

/* The native view stretches a flat quad by (window aspect) / (the 4:3 area's
   aspect). That area is 4:3 over 224 of the 240 lines, so its own aspect is
   4/3 * 240/224; the measured stretch at 21:9 (window 1120x448) was 1.75,
   which this gives. A window narrower than that area shows no stretch. */
static int squeeze_x(int x) {
    float k = g_window_aspect / (4.0f / 3.0f * 240.0f / 224.0f);
    if (!(k > 1.0f))
        return x;
    return 256 + (int)((float)(x - 256) / k);
}

/* A flat quad showing what the tint does to him: Spyro's colour blended
   toward the chosen one by the strength, the way the filter blends. The PS1
   build's answer to the preview dragons that never drew. A player who is not
   in the game gets a darkened swatch, like his greyed column. */
static void swatch(CPUState* cpu, int player, int cx) {
    const uint8_t* c = g_settings.color[player];
    uint32_t f4 = *(uint32_t*)g_api->guest(OP_GADDR_D_800757B0);   /* primitive cursor */
    uint8_t* p = guest8(f4);
    int x0 = cx - SWATCH_HALF_W, x1 = cx + SWATCH_HALF_W;
    int active = player < g_settings.players;

    uint8_t rgb[3];
    for (int i = 0; i < 3; i++) {
        int v = k_swatch_base[i] + ((c[i] - k_swatch_base[i]) * c[3]) / 255;
        rgb[i] = (uint8_t)(active ? v : v / 3);
    }
    /* THE QUAD IS SQUEEZED, THE FRAME IS NOT. In a wide window the native
       renderer places this quad as if the 512-wide screen were stretched over
       the whole window, while the frame's lines, the box and the text stay in
       the centred 4:3 area (measured at 21:9, 2026-09-13: the quad's corners
       landed at x * width / 512). So its corners are pulled toward the centre
       by that stretch, and it lands inside its frame at any window shape. */
    int qx0 = squeeze_x(x0), qx1 = squeeze_x(x1);
    memset(p, 0, 24);
    uint32_t tag = 0x05000000u;
    memcpy(p, &tag, 4);
    memcpy(p + 4, rgb, 3);
    p[7] = 0x28;                             /* POLY_F4, opaque */
    int16_t xy[8] = { (int16_t)qx0, SWATCH_TOP,    (int16_t)qx1, SWATCH_TOP,
                      (int16_t)qx0, SWATCH_BOTTOM, (int16_t)qx1, SWATCH_BOTTOM };
    memcpy(p + 8, xy, sizeof xy);

    cpu->a0 = f4;
    game_call(cpu, OP_FNADDR_func_800168DC);            /* link it */
    *(uint32_t*)g_api->guest(OP_GADDR_D_800757B0) = f4 + 24;

    /* Framed with the box's own line routine, for the same shimmering gold. */
    box_line(cpu, x0, SWATCH_TOP, x1, SWATCH_TOP);
    box_line(cpu, x1, SWATCH_TOP, x1, SWATCH_BOTTOM);
    box_line(cpu, x1, SWATCH_BOTTOM, x0, SWATCH_BOTTOM);
    box_line(cpu, x0, SWATCH_BOTTOM, x0, SWATCH_TOP);
}

static Built number(CPUState* cpu, int v, int cx, int y, int shade) {
    char s[4];
    int i = 0;
    if (v >= 100) s[i++] = (char)('0' + v / 100);
    if (v >= 10)  s[i++] = (char)('0' + (v / 10) % 10);
    s[i++] = (char)('0' + v % 10);
    s[i] = 0;
    return text_centred(cpu, s, cx, y, shade);
}

static void draw_colors(CPUState* cpu) {
    CoopMenuArena* m = M();
    static const char* const labels[4] = { "RED", "GREEN", "BLUE", "STRENGTH" };
    static const char* const heads[COOP_MAX_PLAYERS] = { "P1", "P2", "P3", "P4" };
    static const int y[4] = { 122, 136, 150, 164 };

    Built sel = { 0, 0 };
    for (int p = 0; p < COOP_MAX_PLAYERS; p++) {
        int shade = (p < g_settings.players) ? SHADE_NORMAL : SHADE_DISABLED;
        text_centred(cpu, heads[p], k_col_x[p], 108, shade);
        for (int k = 0; k < 4; k++) {
            Built b = number(cpu, g_settings.color[p][k], k_col_x[p], y[k], shade);
            if (m->cursor == p * 4 + k)
                sel = b;
        }
    }
    for (int k = 0; k < 4; k++)
        text(cpu, labels[k], COLOR_LABEL_X, y[k], 0x1400, 15, 16, SHADE_NORMAL);
    Built done = text(cpu, "DONE", COLOR_LABEL_X, 178, 0x1400, 15, 16, SHADE_NORMAL);
    if (m->cursor == COLOR_DONE)
        sel = done;

    hint(cpu, "L2 R2 FAST  SQUARE RESET");
    for (int p = 0; p < COOP_MAX_PLAYERS; p++)
        swatch(cpu, p, k_col_x[p]);
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

    if (m->page != PAGE_NONE) {
        /* Our page owns the box: the stock rows are not built at all. */
        if (is_last_item(cpu->ra)) {
            m->scratch_used = 0;
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
       one line lower, built from a copy of its position vector so the pause
       draw's own stack vector is left as the game wrote it. Ours first, so
       g_HudMobys is the stock row's again when the stock code reads it for
       its own shimmer. */
    m->scratch_used = 0;
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

/* func_80017FE4, the big letters: PAUSED becomes the page's own title. */
#define TITLE_X 117           /* MULTIPLAYER centred on the box: measured on screen, the
                                 big letters run wider than PAUSED's x 186 suggests */

static void on_title(CPUState* cpu) {
    const Box* box = (cpu->ra == RA_TITLE && !g_in_menu_call && in_pause_list())
                     ? page_box() : NULL;
    if (box == NULL) {
        g_api->base(cpu);
        return;
    }
    if (M()->page == PAGE_COLORS)
        return;                              /* the swatches take this row */

    CoopMenuArena* m = M();
    m->scratch_used = 0;
    static const char title[] = "MULTIPLAYER";
    int32_t pos[3];
    memcpy(pos, guest32(cpu->a1), sizeof pos);
    pos[0] = TITLE_X;
    cpu->a0 = scratch_put(title, sizeof title);
    cpu->a1 = scratch_put(pos, sizeof pos);
    g_api->base(cpu);
}

static void on_box_line(CPUState* cpu) {
    uint32_t ra = cpu->ra;
    if (g_in_menu_call || !in_pause_list() ||
        (ra != RA_BOX_SEPARATOR && ra != RA_BOX_TOP && ra != RA_BOX_RIGHT &&
         ra != RA_BOX_BOTTOM && ra != RA_BOX_LEFT)) {
        g_api->base(cpu);
        return;
    }
    const Box* box = page_box();
    int left   = box ? box->left : 140;
    int right  = box ? box->right : 372;
    int bottom = box ? box->bottom : SMALL_BOTTOM;

    switch (ra) {
    case RA_BOX_SEPARATOR:
        if (box) {                           /* under the whole title, or the swatches */
            cpu->a0 = (uint32_t)(M()->page == PAGE_COLORS ? left + 12 : 128);
            cpu->a2 = (uint32_t)(M()->page == PAGE_COLORS ? right - 12 : 384);
        }
        break;
    case RA_BOX_TOP:
        cpu->a0 = (uint32_t)left;
        cpu->a2 = (uint32_t)right;
        break;
    case RA_BOX_RIGHT:
        cpu->a0 = (uint32_t)right;
        cpu->a2 = (uint32_t)right;
        cpu->a3 = (uint32_t)bottom;
        break;
    case RA_BOX_BOTTOM:
        cpu->a0 = (uint32_t)right;
        cpu->a1 = (uint32_t)bottom;
        cpu->a2 = (uint32_t)left;
        cpu->a3 = (uint32_t)bottom;
        break;
    case RA_BOX_LEFT:
        cpu->a0 = (uint32_t)left;
        cpu->a1 = (uint32_t)bottom;
        cpu->a2 = (uint32_t)left;
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
    const Box* box = page_box();
    int16_t* xy = (int16_t*)g_api->guest(cpu->a0 + 8);   /* POLY_F4 x/y pairs */
    if (box) {
        xy[0] = (int16_t)box->left;  xy[2] = (int16_t)box->right;
        xy[4] = (int16_t)box->left;  xy[6] = (int16_t)box->right;
    }
    xy[5] = (int16_t)(box ? box->bottom : SMALL_BOTTOM);  /* y2 */
    xy[7] = xy[5];                                          /* y3 */
    g_api->base(cpu);
}

int coop_menu_install(uint32_t menu_vaddr) {
    g_menu_vaddr = menu_vaddr;
    if (g_api->override_name(g_self, "func_8002E12C", on_pause_update) != 0 ||
        g_api->override_name(g_self, "func_800181AC", on_text_sprites) != 0 ||
        g_api->override_name(g_self, "func_80017FE4", on_title) != 0 ||
        g_api->override_name(g_self, "func_8001844C", on_box_line) != 0 ||
        g_api->override_name(g_self, "func_800168DC", on_link_prim) != 0 ||
        g_api->register_present_hook(g_self, on_present) != 0) {
        coop_log(OP_MOD_LOG_ERROR, "could not install the Multiplayer menu");
        return 1;
    }
    return 0;
}
