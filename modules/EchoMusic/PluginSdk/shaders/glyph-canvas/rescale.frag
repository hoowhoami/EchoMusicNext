#version 440
#extension GL_GOOGLE_include_directive : require
#include "filter-params.glsl"
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(binding = 1) uniform sampler2D source;
float readPixel(vec2 p)
{
    return any(lessThan(p, vec2(0))) || any(greaterThanEqual(p, inputInfo.xy))
               ? 0.
               : texture(source, (p + .5) / inputInfo.zw).r;
}
float samplePixel(vec2 p)
{
    vec2 lo = floor(p), f = fract(p);
    return mix(mix(readPixel(lo), readPixel(lo + vec2(1, 0)), f.x),
               mix(readPixel(lo + vec2(0, 1)), readPixel(lo + vec2(1, 1)), f.x), f.y);
}
void main()
{
    vec2 p = (uv * outputInfo.xy + mapping.yz) / mapping.x + prior.xy - .5;
    float v = floor(samplePixel(p) * 255. + .5) / 255.;
    fragColor = vec4(v);
}
