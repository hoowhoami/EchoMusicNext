#version 440
#extension GL_GOOGLE_include_directive : require
#include "params.glsl"
#include "ink-fn.glsl"
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(binding = 1) uniform sampler2D source;
void main()
{
    vec2 local = quadRect.xy + uv * quadRect.zw;
    vec2 physical = (deviceMatrix * vec4(local, 0, 1)).xy;
    vec4 result = vec4(0);
    for (int i = 2; i >= 0; --i) {
        if (float(i) >= alphas.z)
            continue;
        vec2 t = (physical - pathRect[i].xy) / pathRect[i].zw;
        float coverage = any(lessThan(t, vec2(0))) || any(greaterThan(t, vec2(1)))
                             ? 0.
                             : texture(source, (fieldRect[i].xy + t * fieldRect[i].zw) /
                                                   inputInfo.zw)[int(channels[i])];
        coverage = floor(coverage * 255. + .5) / 255.;
        float a = clamp(coverage * shadowColor[i].a, 0., 1.);
        result = vec4(shadowColor[i].rgb * a, a) + result * (1. - a);
    }
    vec4 ink = nativeInk(local);
    float alpha = ink.a * fillColor.a;
    vec4 body = options.x > .5 ? ink * fillColor.a : vec4(fillColor.rgb * alpha, alpha);
    result = body + result * (1. - alpha);
    float clipCoverage = 1.;
    if (alphas.w > .5) {
        vec2 footprint = max(fwidth(local), vec2(.0001));
        vec2 low = clamp((local - clipRect.xy) / footprint + .5, 0., 1.);
        vec2 high = clamp((clipRect.zw - local) / footprint + .5, 0., 1.);
        clipCoverage = low.x * low.y * high.x * high.y;
        if (clipRect.z <= clipRect.x || clipRect.w <= clipRect.y)
            clipCoverage = 0.;
    }
    result.rgb *= tintColor.rgb;
    fragColor = result * (alphas.x * clipCoverage);
}
