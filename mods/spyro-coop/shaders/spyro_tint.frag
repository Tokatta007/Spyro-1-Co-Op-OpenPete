/**
 * @file spyro_tint.frag
 * @brief A player's color on the Spyro of a dragon rescue cutscene.
 *
 * In gameplay each dragon wears his player's color through the game's own
 * filter, g_Spyro.m_colorFilter, which mixes the model's colors toward the
 * filter color by its strength. The Spyro of a rescue cutscene is not
 * g_Spyro but a moby (class 511), which that filter never reaches, so this
 * material does the same mix on that moby instead (coop_draw.c, RESCUE).
 *
 * The block is filled per instance by the refine callback: rgb is the
 * player's color and a his strength, both 0..1. A zero block is the stock
 * look.
 */
#version 450
#extension GL_GOOGLE_include_directive : require
#include "openpete_psx_material.glsl"

layout(set = 3, binding = 1, std140) uniform op_instance {
    vec4 tint;     /**< rgb = the player's color, a = strength 0..1 */
};

vec4 op_material(vec4 base)
{
    /* The player's color, lit by this pixel: a flat mix toward the color
       erased every shade at full strength (a solid red shape, seen headless),
       while in gameplay a full-strength dragon keeps his light and shadow. */
    float lum    = dot(base.rgb, vec3(0.299, 0.587, 0.114));
    vec3  shaded = clamp(tint.rgb * lum * 2.0, 0.0, 1.0);
    return vec4(mix(base.rgb, shaded, clamp(tint.a, 0.0, 1.0)), base.a);
}
