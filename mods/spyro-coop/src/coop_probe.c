/**
 * @file coop_probe.c
 * @brief spyro-coop, step zero: prove the pipeline and answer the one
 *        question that gates everything else.
 *
 * This is NOT the co-op mod. It is the smallest thing that establishes four
 * facts, in order, before a line of co-op logic is ported:
 *
 *   1. the mod compiles against the SDK and actually loads;
 *   2. `api->guest()` reaches the guest addresses the PS1 mod used, and the
 *      struct at the other end looks like what we expect;
 *   3. the engine tolerates a mod declaring `sim_mutative = true`;
 *   4. **whether OpenPete feeds the second controller's pad buffer.**
 *
 * Fact 4 is the whole point. The PS1 mod reads two BIOS pad buffers 0x7B0
 * apart and swaps them; on OpenPete the second buffer exists as a symbol,
 * OP_GADDR_g_PadBufferSecondController, but whether anything writes into it
 * is a separate question from whether the engine has a second controller.
 * This reads the buffer and reports what is actually there rather than
 * assuming either way. The answer decides whether the port starts or waits.
 *
 * RESULT, 2026-09-12: the engine's own input log reported slot 2 present for
 * 2,933 vertical blanks and differing from slot 1 for 275 of them, so the
 * engine does read a genuinely independent second pad. This probe read the
 * guest buffer as all zeros across that same session. The gap is therefore
 * the engine-to-guest plumbing for buffer 2 and nothing else — not detection,
 * not SDL mapping, not our addresses. See docs/PORTING.md, B1.
 *
 * The probe writes NOTHING to guest RAM. Reading is enough to answer all
 * four, and an observer-only first mod means any divergence the engine's
 * gameplay oracle reports later is attributable to the first real write
 * rather than to scaffolding.
 *
 * Derived in spirit from the PS1 co-op mod's Sp1x2Pad.c, which is in turn
 * derived from Spyromain's Spyro2x2 (MIT). See LICENSE.
 */

#include <openpete_mod_api.h>
#include <openpete_sdk_symbols.h>

/** A PlayStation pad buffer is 34 bytes; the first 32 carry everything the
 *  co-op mod ever swapped (status, type, buttons, both analog sticks). */
#define PAD_BUF_BYTES 32

/** Byte 0 of a pad buffer is the status: 0 means a pad is connected and
 *  responding. Any other value means nothing is there. */
#define PAD_CONNECTED(p) ((p)[0] == 0u)

/** Bytes 2..3 are the button word, active LOW: a pressed button reads 0.
 *  0xFFFF therefore means "connected, nothing pressed". */
#define PAD_BUTTONS(p) ((unsigned)((p)[2] << 8) | (unsigned)(p)[3])

static const openpete_mod_api_t* g_api;
static openpete_mod_t*           g_self;

static const volatile unsigned char* g_pad1;  /**< port 1 buffer, in host space */
static const volatile unsigned char* g_pad2;  /**< port 2 buffer, in host space */

static unsigned g_ticks;          /**< CameraUpdate calls seen this session */
static unsigned g_p2_seen_conn;   /**< ticks where port 2 reported connected */
static unsigned g_p2_seen_input;  /**< ticks where port 2 reported a button down */
static int      g_p2_ever;        /**< latched: port 2 was live at least once */

/**
 * @brief Repaint the mod's block in the Mods panel.
 *
 * ui_status replaces the whole block per call, so every line the mod wants
 * visible is republished each time rather than appended.
 */
static void publish_status(void) {
    g_api->ui_status(g_self, "ticks: %u", g_ticks);
    g_api->ui_status(g_self, "port 1: %s",
                     PAD_CONNECTED(g_pad1) ? "connected" : "absent");
    g_api->ui_status(g_self, "port 2: %s  (connected %u, input %u)",
                     PAD_CONNECTED(g_pad2) ? "connected" : "absent",
                     g_p2_seen_conn, g_p2_seen_input);
    g_api->ui_status(g_self, "VERDICT: %s", g_p2_ever
        ? "second controller IS fed -- the port can start"
        : "second controller NOT fed -- blocked, ask upstream");
}

/**
 * @brief Per-tick pre-hook on CameraUpdate, used purely as a heartbeat.
 * @param cpu Guest register file. Untouched here; this observes only.
 *
 * CameraUpdate runs once per gameplay tick, which makes it the cheapest
 * reliable place to sample. base() is called last, so the camera still
 * updates normally and the game plays byte-identically.
 */
static void on_tick(CPUState* cpu) {
    g_ticks++;

    if (PAD_CONNECTED(g_pad2)) {
        g_p2_seen_conn++;
        /* Connected is necessary but not sufficient: a buffer can be zeroed
         * and look "connected" while nothing is ever written to it. A button
         * actually going down is the proof that input is flowing. */
        if (PAD_BUTTONS(g_pad2) != 0xFFFFu) {
            g_p2_seen_input++;
            if (!g_p2_ever) {
                g_p2_ever = 1;
                g_api->log(g_self, OP_MOD_LOG_INFO,
                           "port 2 is LIVE: first input at tick %u (buttons=0x%04X)",
                           g_ticks, PAD_BUTTONS(g_pad2));
            }
        }
    }

    /* Every ~10 seconds at 29.913 ticks/sec, so the log stays readable. */
    if (g_ticks == 1u || (g_ticks % 300u) == 0u)
        g_api->log(g_self, OP_MOD_LOG_INFO,
                   "tick %u: p1 %s (0x%04X), p2 %s (0x%04X)",
                   g_ticks,
                   PAD_CONNECTED(g_pad1) ? "conn" : "abs", PAD_BUTTONS(g_pad1),
                   PAD_CONNECTED(g_pad2) ? "conn" : "abs", PAD_BUTTONS(g_pad2));

    g_api->base(cpu);
    publish_status();
}

/**
 * @brief Entry point. Resolves the two pad buffers and installs the heartbeat.
 * @return 0 on success; non-zero disables the mod and unwinds registrations.
 *
 * The addresses come from the SDK's own symbol header rather than the raw
 * constants the PS1 mod carried, so a layout change upstream is a compile
 * error here instead of silent corruption. That is the single biggest
 * ergonomic win of the OpenPete SDK over the PS1 construction, and it is the
 * class of fault that cost the PS1 project the g_PadBackup offset bug.
 */
int openpete_mod_entry(const openpete_mod_api_t* api, openpete_mod_t* self) {
    g_api  = api;
    g_self = self;

    g_pad1 = (const volatile unsigned char*)api->guest(OP_GADDR_g_PadBuffer);
    g_pad2 = (const volatile unsigned char*)api->guest(OP_GADDR_g_PadBufferSecondController);

    if (!g_pad1 || !g_pad2) {
        api->log(self, OP_MOD_LOG_ERROR,
                 "could not resolve a pad buffer (p1=%p p2=%p) -- refusing to load",
                 (const void*)g_pad1, (const void*)g_pad2);
        return 1;
    }

    api->log(self, OP_MOD_LOG_INFO,
             "spyro-coop probe up: pad1 guest 0x%08X, pad2 guest 0x%08X, %d bytes each",
             (unsigned)OP_GADDR_g_PadBuffer,
             (unsigned)OP_GADDR_g_PadBufferSecondController,
             PAD_BUF_BYTES);

    publish_status();
    return api->override_name(self, "CameraUpdate", on_tick);
}
