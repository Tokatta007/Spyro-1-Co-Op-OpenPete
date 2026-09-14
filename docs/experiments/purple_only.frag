/* purple_only.frag - EXPERIMENT, not shipped (2026-09-14).
 *
 * Recolors only the purple parts of Spyro: pixels whose hue is purple
 * (0.65-0.9) and saturated enough become the tint, lit by their own
 * brightness; horns, crest, wing membranes, belly and eyes keep their color.
 * Tried headless as a [[material]] row on channel "player" and on the rescue
 * cutscene Spyro (moby class 511), with the game's filter at strength 0: the
 * body went green and every other part stayed stock. Where the PS1 attempt
 * failed (its vertex palette encoded lighting, not material), a material sees
 * each pixel's final color, which does separate the parts.
 *
 * Not shipped because the player channel takes one uniform block for every
 * Spyro draw of a frame, so every dragon would get the same color. See
 * BUGS.md, C4. */
#version 450
#extension GL_GOOGLE_include_directive : require
#include "openpete_psx_material.glsl"
layout(set = 3, binding = 1, std140) uniform op_instance { vec4 tint; };
vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + 1e-10)), d / (q.x + 1e-10), q.x);
}
vec4 op_material(vec4 base) {
    vec3 hsv = rgb2hsv(base.rgb);
    float purple = smoothstep(0.62, 0.68, hsv.x) * (1.0 - smoothstep(0.88, 0.95, hsv.x)) * smoothstep(0.12, 0.25, hsv.y);
    float lum = dot(base.rgb, vec3(0.299, 0.587, 0.114));
    vec3 shaded = clamp(tint.rgb * lum * 2.2, 0.0, 1.0);
    return vec4(mix(base.rgb, shaded, purple), base.a);
}
