/**
 * @file coop_settings.c
 * @brief The player's settings: one set, two editors, saved to a file.
 *
 * WHY NOT [[config]] ROWS. The engine renders those in the M overlay and the
 * mod reads them, but a mod cannot WRITE one. The in-game Multiplayer menu
 * has to change the same settings the M panel shows, from a controller, and
 * both must agree. So the settings live here, in the mod:
 *
 *   - g_settings is the truth, read and written only from tick context;
 *   - the M overlay panel below edits a copy and raises a flag, and the next
 *     tick adopts it (a panel body runs under the render-purity guard and on
 *     the present thread, where the game must not be touched);
 *   - the in-game menu writes g_settings directly, in tick context;
 *   - any change is saved to mods/spyro-coop/data/settings.txt, so both
 *     editors, and the next launch, see it.
 *
 * Settings are host state, not arena state, on purpose: a savestate load or a
 * rewind should not undo a colour the player just picked.
 */

#include "coop.h"
#include <openpete_mod_ui.h>
#include <stdio.h>
#include <stdlib.h>

CoopSettings g_settings;

/* The M panel's working copy, and whether it holds an edit the tick has not
   adopted yet. Written by the panel on the present thread and read by the
   tick; every field is a plain int or byte, so a torn read shows one frame of
   a half-updated panel at worst. */
static CoopSettings g_ui_copy;
static volatile int g_ui_dirty;

/* Spyro's own purple at zero strength for both, exactly as the PS1 build
   seeded it: a fresh install looks like retail, and turning STRENGTH up walks
   toward the chosen colour rather than lurching from an unrelated one. */
static const uint8_t k_default_color[4] = { 0x78, 0x58, 0xA8, 0x00 };

static void set_defaults(CoopSettings* s) {
    memset(s, 0, sizeof *s);
    s->players        = 2;
    s->respawn_modern = 1;
    s->split_vertical = 1;
    for (int i = 0; i < COOP_MAX_PLAYERS; i++)
        memcpy(s->color[i], k_default_color, 4);
    s->extra_controls = EXTRA_CONTROLS_CONTROLLER;
    s->draw_p2        = 1;
    s->hysteresis     = 25;
}

static void clamp(CoopSettings* s) {
    s->players        = (s->players < 1) ? 1 : (s->players > COOP_MAX_PLAYERS) ? COOP_MAX_PLAYERS : s->players;
    s->respawn_modern = s->respawn_modern ? 1 : 0;
    s->split_vertical = s->split_vertical ? 1 : 0;
    s->draw_p2        = s->draw_p2 ? 1 : 0;
    if (s->extra_controls < 0 || s->extra_controls >= EXTRA_CONTROLS_COUNT)
        s->extra_controls = EXTRA_CONTROLS_CONTROLLER;
    if (s->hysteresis < 0)  s->hysteresis = 0;
    if (s->hysteresis > 50) s->hysteresis = 50;
}

static void path(char* out, size_t cap) {
    snprintf(out, cap, "%s/settings.txt", g_api->data_dir(g_self));
}

/* ------------------------------------------------------------------------
 * The file: one "key = value" per line, so a person can read and fix it.
 * Unknown keys are ignored and missing ones keep their defaults, so adding a
 * setting later never breaks an existing file.
 * ---------------------------------------------------------------------- */

void coop_settings_save(void) {
    char p[512];
    path(p, sizeof p);
    FILE* f = fopen(p, "w");
    if (!f) {
        coop_log(OP_MOD_LOG_WARN, "could not save settings to %s", p);
        return;
    }
    const CoopSettings* s = &g_settings;
    fprintf(f, "# spyro-coop settings. Edited by the in-game Multiplayer menu and the M overlay.\n");
    fprintf(f, "players = %d\n", s->players);
    fprintf(f, "respawn = %s\n", s->respawn_modern ? "modern" : "original");
    fprintf(f, "split = %s\n", s->split_vertical ? "vertical" : "horizontal");
    for (int i = 0; i < COOP_MAX_PLAYERS; i++)
        fprintf(f, "p%d_color = %d %d %d %d\n", i + 1,
                s->color[i][0], s->color[i][1], s->color[i][2], s->color[i][3]);
    fprintf(f, "extra_controls = %s\n",
            s->extra_controls == EXTRA_CONTROLS_COPY ? "copy" : "controller");
    fprintf(f, "draw_p2 = %d\n", s->draw_p2);
    fprintf(f, "enemy_switch_margin = %d\n", s->hysteresis);
    fclose(f);
}

void coop_settings_load(void) {
    set_defaults(&g_settings);
    char p[512];
    path(p, sizeof p);
    FILE* f = fopen(p, "r");
    if (f) {
        char line[256], key[64], val[128];
        while (fgets(line, sizeof line, f)) {
            if (line[0] == '#' || sscanf(line, " %63[^= ] = %127[^\n]", key, val) != 2)
                continue;
            CoopSettings* s = &g_settings;
            if (!strcmp(key, "players"))
                s->players = atoi(val);
            else if (!strcmp(key, "respawn"))
                s->respawn_modern = strncmp(val, "original", 8) != 0;
            else if (!strcmp(key, "split"))
                s->split_vertical = strncmp(val, "horizontal", 10) != 0;
            else if (key[0] == 'p' && key[1] >= '1' && key[1] < '1' + COOP_MAX_PLAYERS &&
                     !strcmp(key + 2, "_color")) {
                int i = key[1] - '1', c[4];
                if (sscanf(val, "%d %d %d %d", &c[0], &c[1], &c[2], &c[3]) == 4)
                    for (int k = 0; k < 4; k++)
                        s->color[i][k] = (uint8_t)(c[k] < 0 ? 0 : c[k] > 255 ? 255 : c[k]);
            } else if (!strcmp(key, "extra_controls"))
                s->extra_controls = strncmp(val, "copy", 4) == 0 ? EXTRA_CONTROLS_COPY
                                                                  : EXTRA_CONTROLS_CONTROLLER;
            else if (!strcmp(key, "draw_p2"))
                s->draw_p2 = atoi(val);
            else if (!strcmp(key, "enemy_switch_margin"))
                s->hysteresis = atoi(val);
        }
        fclose(f);
        coop_log(OP_MOD_LOG_INFO, "settings loaded from %s", p);
    } else {
        coop_settings_save();                /* write the defaults, so the file exists */
    }
    clamp(&g_settings);
    g_ui_copy = g_settings;
}

/* Called once per tick: adopt an edit made in the M panel. */
void coop_settings_tick(void) {
    if (!g_ui_dirty)
        return;
    g_ui_dirty = 0;
    CoopSettings next = g_ui_copy;
    clamp(&next);
    if (memcmp(&next, &g_settings, sizeof next) != 0) {
        g_settings = next;
        coop_settings_save();
    }
}

/* Called by the in-game menu after it changes g_settings. */
void coop_settings_changed(void) {
    clamp(&g_settings);
    coop_settings_save();
}

/* SQUARE on the in-game Colors page. */
void coop_settings_reset_color(int player) {
    if (player < 0 || player >= COOP_MAX_PLAYERS)
        return;
    memcpy(g_settings.color[player], k_default_color, 4);
    coop_settings_changed();
}

/* ------------------------------------------------------------------------
 * The M overlay panel
 * ---------------------------------------------------------------------- */

static void color_rows(const openpete_mod_ui_t* ui, int player, int* changed) {
    static const char* const names[4] = { "red", "green", "blue", "strength" };
    for (int k = 0; k < 4; k++) {
        char label[32];
        snprintf(label, sizeof label, "P%d %s", player + 1, names[k]);
        int v = g_ui_copy.color[player][k];
        if (ui->slider_int(label, &v, 0, 255)) {
            g_ui_copy.color[player][k] = (uint8_t)v;
            *changed = 1;
        }
    }
}

static void settings_panel(const openpete_mod_ui_t* ui) {
    /* Show the live settings unless an edit is still waiting for a tick. */
    if (!g_ui_dirty)
        g_ui_copy = g_settings;

    int changed = 0;
    static const char* const players[]  = { "1", "2", "3", "4" };
    static const char* const respawn[]  = { "Original", "Modern" };
    static const char* const split[]    = { "Horizontal", "Vertical" };

    ui->text("Also in the game: pause, then MULTIPLAYER.");
    ui->separator();

    int idx = g_ui_copy.players - 1;
    if (ui->combo("Players", &idx, players, COOP_MAX_PLAYERS)) { g_ui_copy.players = idx + 1; changed = 1; }
    ui->tooltip("How many dragons. Extra players join beside player 1.");

    static const char* const controls[] = { "Copy player 1", "Controller" };
    idx = g_ui_copy.extra_controls;
    if (ui->combo("Players 2-4 controls", &idx, controls, EXTRA_CONTROLS_COUNT)) {
        g_ui_copy.extra_controls = idx; changed = 1;
    }
    ui->tooltip("Controller: players 2 to 4 follow this mod's gamepad bindings. For "
                "player 1 on the keyboard alone, remove the \"pad:\" entries from the "
                "game's own pad keys. Copy player 1: every dragon follows player 1.");

    idx = g_ui_copy.respawn_modern;
    if (ui->combo("Respawn", &idx, respawn, 2)) { g_ui_copy.respawn_modern = idx; changed = 1; }
    ui->tooltip("Modern: only the dragon who died respawns, and one shared life is spent. "
                "Original: every death reloads both, as the original game does.");

    idx = g_ui_copy.split_vertical;
    if (ui->combo("Split screen", &idx, split, 2)) { g_ui_copy.split_vertical = idx; changed = 1; }
    ui->tooltip("Not available yet: split-screen needs multi-view support in OpenPete. "
                "The choice is saved for when it arrives.");

    ui->separator();
    ui->text("Colours (strength 0 leaves Spyro's own colour)");
    for (int p = 0; p < COOP_MAX_PLAYERS; p++)
        color_rows(ui, p, &changed);

    ui->separator();
    ui->text_disabled("Development");
    if (ui->checkbox("Draw player 2", &g_ui_copy.draw_p2)) changed = 1;
    ui->tooltip("Draw player 2 with the game's own renderers. Visible only with "
                "frame interpolation off, for now.");
    if (ui->slider_int("Enemy switch margin (%)", &g_ui_copy.hysteresis, 0, 50)) changed = 1;
    ui->tooltip("How much closer the other dragon must be before an enemy switches to him.");

    if (changed)
        g_ui_dirty = 1;
}

OPENPETE_MOD_UI_SECTION(settings_panel)
