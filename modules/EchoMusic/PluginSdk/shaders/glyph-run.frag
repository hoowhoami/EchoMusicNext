#version 440
layout(location = 0) in vec4 uv;
layout(location = 1) in vec4 bounds;
layout(location = 2) in vec4 tint;
layout(location = 3) in vec4 effects;
layout(location = 4) in vec2 haloUv;
layout(location = 5) in vec4 haloBounds;
layout(location = 6) in vec4 haloRadii;
layout(location = 7) in vec4 haloA;
layout(location = 8) in vec4 haloB;
layout(location = 9) in vec4 haloC;
layout(location = 10) in vec4 bodyEffectsUv;
layout(location = 11) in vec4 bodyEffectsBounds;
layout(location = 12) in vec2 runLocal;
layout(location = 13) in vec4 clipBounds;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf
{
    mat4 matrix;
    float opacity;
};
layout(binding = 1) uniform sampler2D source;
layout(binding = 2) uniform sampler2D haloSource;
layout(binding = 3) uniform sampler2D bodyEffectsSource;
float within(vec2 point, vec4 box)
{
    return step(box.x, point.x) * step(box.y, point.y) * step(point.x, box.z) *
           step(point.y, box.w);
}
float bodyBlur(vec4 mask, float amount, float ceiling)
{
    float first = min(8.0, ceiling);
    float second = min(16.0, ceiling);
    if (amount <= 0.0)
        return mask.r;
    if (amount <= first)
        return mix(mask.r, mask.g, amount / first);
    if (amount <= second)
        return mix(mask.g, mask.b, (amount - first) / (second - first));
    return mix(mask.b, mask.a, (amount - second) / (ceiling - second));
}
float haloBlurRaw(vec4 mask, float ink, float radius, float ceiling)
{
    float a = min(16.0, ceiling);
    float b = min(ceiling > 160.0 ? 128.0 : 40.0, ceiling);
    float c = min(ceiling > 512.0 ? 512.0 : ceiling > 160.0 ? 256.0 : 80.0, ceiling);
    if (radius <= a)
        return mix(ink, mask.r, radius / a);
    if (radius <= b)
        return mix(mask.r, mask.g, (radius - a) / (b - a));
    if (radius <= c)
        return mix(mask.g, mask.b, (radius - b) / (c - b));
    return mix(mask.b, mask.a, (radius - c) / (ceiling - c));
}
float haloBlur(vec4 mask, float ink, float radius, float ceiling)
{
    float value = haloBlurRaw(mask, ink, radius, ceiling);
    return ceiling > 160.0 ? floor(value * 255.0 + 0.5) / 255.0 : value;
}

vec4 over(vec4 bottom, vec4 color, float coverage)
{
    float alpha = clamp(color.a * coverage, 0.0, 1.0);
    return vec4(color.rgb * alpha, alpha) + bottom * (1.0 - alpha);
}
void main()
{
    vec4 ink = texture(source, clamp(uv.xy, bounds.xy, bounds.zw)) * within(uv.xy, bounds);
    bool colorInk = effects.w >= 2048.0;
    float haloCeiling = mod(effects.w, 2048.0);
    vec4 mask = vec4(colorInk ? ink.a : ink.r, 0.0, 0.0, 0.0);
    vec4 blurMask = texture(bodyEffectsSource,
                            clamp(bodyEffectsUv.xy, bodyEffectsBounds.xy, bodyEffectsBounds.zw)) *
                    within(bodyEffectsUv.xy, bodyEffectsBounds);
    mask.gba = blurMask.gba;
    vec4 haze = texture(haloSource, clamp(haloUv, haloBounds.xy, haloBounds.zw)) *
                within(haloUv, haloBounds);
    vec4 result = vec4(0.0);
    if (haloRadii.w > 2.5)
        result = over(result, haloC, haloBlur(haze, mask.r, haloRadii.z, haloCeiling));
    if (haloRadii.w > 1.5)
        result = over(result, haloB, haloBlur(haze, mask.r, haloRadii.y, haloCeiling));
    if (haloRadii.w > 0.5)
        result = over(result, haloA, haloBlur(haze, mask.r, haloRadii.x, haloCeiling));
    float legacy = effects.z > 0.0 ? mask.g * effects.y : 0.0;
    result = over(result, tint, legacy);
    if (colorInk && effects.x <= 0.0) {
        vec4 body = ink * tint.a;
        result = body + result * (1.0 - body.a);
    } else {
        result = over(result, tint, bodyBlur(mask, effects.x, effects.z));
    }
    float clipCoverage = 1.0;
    if (clipBounds.z >= clipBounds.x) {
        vec2 footprint = max(fwidth(runLocal), vec2(0.0001));
        vec2 low = clamp((runLocal - clipBounds.xy) / footprint + 0.5, 0.0, 1.0);
        vec2 high = clamp((clipBounds.zw - runLocal) / footprint + 0.5, 0.0, 1.0);
        clipCoverage = low.x * low.y * high.x * high.y;
        if (clipBounds.z <= clipBounds.x || clipBounds.w <= clipBounds.y)
            clipCoverage = 0.0;
    }
    result.rgb *= vec3(uv.w, bodyEffectsUv.zw);
    fragColor = result * (opacity * clipCoverage * uv.z);
}
