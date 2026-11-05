// Reader CRT: the glass's blurred copies of the picture, which crt_shader.glsl reads through
// CRT_GLOW_TEX when a renderer makes them. Each is drawn by one step of Crt::GlowPlan
// (reader/core/crt.h), with this text after crt_shader.glsl's and the same defines, and:
//   CRT_GLOW_V    the step's vector (Crt::GlowStep::vector)
// and the step's input bound where CRT_TEX reads: the game's picture for the first steps,
// another copy for the rest. The renderer calls ReaderCrtGlow(f), f being the pixel's
// place in its target as the graphics API counts it (gl_FragCoord.xy, SV_Position.xy): a
// copy's texture coordinate is then its place in the picture in every API.
//
// What a step does (the vector's x):
//   0  from the picture, in linear light, at half its size or less (a box of 2 x 2 taps)
//   1  the same, of the picture's bright part only
//   2  from another copy, at half its size (a box of 2 x 2 taps)
//   3  a blur along the rows, of the vector's y in pixels (sigma)
//   4  the same along the columns
// The vector's zw: one over the target's width and height.
//
// Of the eight vectors these steps read only CRT_U(0) and CRT_U(1) (where the picture is):
// a renderer with no room for a ninth (128 bytes of push constants) carries CRT_GLOW_V in
// CRT_U(7)'s place (PCSX2's Vulkan and Direct3D 12). Keep it so.

vec3 ReaderCrtGlow(vec2 f) {
    vec4 v = CRT_GLOW_V;
    vec2 uv = f * v.zw;
    vec3 s = vec3(0.0, 0.0, 0.0);
    if (v.x < 1.5) {
        for (int j = 0; j < 2; j++) {
            for (int i = 0; i < 2; i++) {
                vec3 c = crtSrc(uv + (vec2(float(i), float(j)) - 0.5) * 0.5 * v.zw);
                float part = v.x > 0.5 ? crtBright(c) : 1.0;
                s += crtLinear(c) * part;
            }
        }
        return s * 0.25;
    }
    if (v.x < 2.5) {
        for (int j = 0; j < 2; j++) {
            for (int i = 0; i < 2; i++) {
                s += CRT_TEX(uv + (vec2(float(i), float(j)) - 0.5) * 0.5 * v.zw).rgb;
            }
        }
        return s * 0.25;
    }
    vec2 dir = v.x < 3.5 ? vec2(v.z, 0.0) : vec2(0.0, v.w);
    float w = 0.0;
    for (int k = -12; k <= 12; k++) {
        float x = float(k);
        float g = exp(-0.5 * x * x / (v.y * v.y));
        s += g * CRT_TEX(uv + dir * x).rgb;
        w += g;
    }
    return s / w;
}
