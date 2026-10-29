layout(binding = 2) uniform sampler2D ink0;
layout(binding = 3) uniform sampler2D ink1;
layout(binding = 4) uniform sampler2D ink2;
layout(binding = 5) uniform sampler2D ink3;
vec4 inkLevel(sampler2D s, int level, vec2 p)
{
    vec2 t = (p - bodyRect[level].xy) / bodyRect[level].zw;
    return any(lessThan(t, vec2(0))) || any(greaterThan(t, vec2(1)))
               ? vec4(0)
               : texture(s, bodyUv[level].xy + t * bodyUv[level].zw);
}
vec4 nativeInk(vec2 local)
{
    float lod = clamp(-log2(max(options.y, .000001)), 0., 3.);
    vec4 a = inkLevel(ink0, 0, local), b = inkLevel(ink1, 1, local), c = inkLevel(ink2, 2, local),
         d = inkLevel(ink3, 3, local);
    if (lod < 1.)
        return mix(a, b, lod);
    if (lod < 2.)
        return mix(b, c, lod - 1.);
    return mix(c, d, lod - 2.);
}
