/**
 * @file coop_sound.c
 * @brief Sounds are as loud as they are near ANY dragon. Ported from
 *        Sp1x2SoundListenerDistance.
 *
 * TickActiveSoundVoices measures each 3D voice's distance for its range kill
 * and falloff with one call, `jal VecMagnitude` at 0x80056528 in the retail
 * executable. The caller has just computed diff = source - live camera
 * (func_8001778C, from g_Camera + 0x28), so the source's position is
 * recoverable, and the same distance can be measured from player 2's camera.
 * Returning the smaller one means a sound near either player is near:
 * Spyromain's Sp2x2GetSoundVolumeFromDistance semantic, per voice, with the
 * vanilla kill radius. (A PS1 experiment with a 4x radius made ambient loops
 * audible across half the map and was rejected by the user.)
 *
 * The distance is measured by the game's own VecMagnitude both times, so the
 * scale is identical. The second measurement borrows the caller's diff vector
 * on the guest stack and puts it back, since the caller may read it after.
 *
 * min() is symmetric, so this stays right on a handover frame when the live
 * and shadow cameras are exchanged.
 *
 * Known PS1 residual, not addressed: looping voices are re-attenuated per
 * frame elsewhere (SoundsUpdate) against the live camera only.
 */

#include "coop.h"

#define RA_SOUND_DISTANCE 0x80056530u  /* jal VecMagnitude at 0x80056528 */

static void on_vec_magnitude(CPUState* cpu) {
    int n = coop_seeded_shadows();
    if (cpu->ra != RA_SOUND_DISTANCE || !coop_enabled() || n == 0) {
        g_api->base(cpu);
        return;
    }

    SavedRegs regs;
    save_regs(cpu, &regs);

    g_api->base(cpu);                        /* from the live camera */
    uint32_t d_live = cpu->v0;

    int32_t* diff = (int32_t*)g_api->guest(regs.a0);
    if (!diff) {
        cpu->v0 = d_live;
        return;
    }

    const int32_t* live_cam = guest32(OP_GADDR_g_Camera + CAMERA_OFF_POSITION);
    int32_t saved[3] = { diff[0], diff[1], diff[2] };
    uint32_t nearest = d_live;

    for (int k = 1; k <= n; k++) {
        const int32_t* other_cam = (const int32_t*)(coop_shadow(k).camera + CAMERA_OFF_POSITION);
        for (int i = 0; i < 3; i++)
            diff[i] = saved[i] + live_cam[i] - other_cam[i];  /* source - his camera */
        load_regs(cpu, &regs);
        g_api->base(cpu);                    /* from shadow k's camera */
        if (cpu->v0 < nearest)
            nearest = cpu->v0;
    }

    diff[0] = saved[0]; diff[1] = saved[1]; diff[2] = saved[2];

    load_regs(cpu, &regs);
    cpu->v0 = nearest;
    if (nearest < d_live)
        g_stats.sounds_nearer_p2++;
}

int coop_sound_install(void) {
    if (g_api->override_name(g_self, "VecMagnitude", on_vec_magnitude) != 0) {
        coop_log(OP_MOD_LOG_ERROR, "could not install the two-camera sound distance");
        return 1;
    }
    return 0;
}
