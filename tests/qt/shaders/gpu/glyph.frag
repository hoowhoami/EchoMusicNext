#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(std140, binding = 1) uniform Glyph
{
    vec4 rect;
    vec4 tint;
}
glyph;
layout(binding = 2) uniform sampler2D ink;
void main()
{
    vec4 v = texture(ink, glyph.rect.xy + uv * glyph.rect.zw);
    // Mask tiles store linear coverage in R; native colour tiles use premultiplied RGBA.
    float coverage = glyph.tint.a > 0.5 ? v.a : v.r;
    vec3 straight = v.rgb / max(v.a, 0.000001);
    vec3 linear = mix(straight / 12.92, pow((straight + 0.055) / 1.055, vec3(2.4)),
                      greaterThan(straight, vec3(0.04045)));
    color = vec4((glyph.tint.a > 0.5 ? linear : vec3(1)) * glyph.tint.rgb * coverage, coverage);
}
