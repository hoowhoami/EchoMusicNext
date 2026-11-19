#version 440
#extension GL_GOOGLE_include_directive : require
#include "filter-params.glsl"
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(binding = 1) uniform sampler2D source;
layout(std140, binding = 6) uniform kernelBuf
{
    vec4 taps[128];
};
float readPixel(vec2 p)
{
    return any(lessThan(p, vec2(0))) || any(greaterThanEqual(p, inputInfo.xy))
               ? 0.
               : texture(source, (p + .5) / inputInfo.zw).r;
}
float axisCoverage(vec2 p)
{
    bool horizontal = prior.z < .5;
    float perpendicular = horizontal ? p.y : p.x;
    float perpendicularLimit = horizontal ? inputInfo.y : inputInfo.x;
    if (perpendicular < 0. || perpendicular >= perpendicularLimit)
        return 0.;
    vec2 axis = horizontal ? vec2(1., 0.) : vec2(0., 1.);
    float total = 0.;
    for (int i = 0; i < int(options.w); ++i) {
        vec4 tap = taps[i];
        vec2 first = p + axis * tap.x;
        // Preserve the original individual coefficients and accumulation order.
        // Hardware linear filtering quantizes its fractional weights and can
        // accumulate a visible error across overlapping masks.
        total += tap.z * readPixel(first);
        if (tap.w != 0.)
            total += tap.w * readPixel(first + axis);
    }
    return floor(total / options.z * 255. + .5) / 255.;
}
vec4 verticalQuad(vec2 p)
{
    vec4 total = vec4(0.);
    // Keep the four independently quantized convolution results. Share only
    // the loop and uniform loads before the original bilinear resampling.
    for (int i = 0; i < int(options.w); ++i) {
        vec4 tap = taps[i];
        vec2 at = p + vec2(0., tap.x);
        total += tap.z * vec4(readPixel(at), readPixel(at + vec2(1., 0.)),
                              readPixel(at + vec2(0., 1.)), readPixel(at + vec2(1.)));
        if (tap.w != 0.)
            total += tap.w * vec4(readPixel(at + vec2(0., 1.)), readPixel(at + vec2(1.)),
                                  readPixel(at + vec2(0., 2.)), readPixel(at + vec2(1., 2.)));
    }
    return floor(total / options.z * 255. + .5) / 255.;
}
void main()
{
    vec2 sampleUv = uv;
    if (prior.y > .5) {
        vec2 pixel = uv * (outputInfo.xy + 2.) - 1.;
        if (any(lessThan(pixel, vec2(0))) || any(greaterThanEqual(pixel, outputInfo.xy))) {
            fragColor = vec4(0);
            return;
        }
        sampleUv = pixel / outputInfo.xy;
    }
    float v;
    if (prior.z > .5 && outputInfo.z > 0. &&
        (outputInfo.x < outputInfo.z || outputInfo.y < outputInfo.w)) {
        vec2 p = sampleUv * outputInfo.zw - .5, lo = floor(p), f = fract(p);
        vec4 q = verticalQuad(lo);
        v = mix(mix(q.x, q.y, f.x), mix(q.z, q.w, f.x), f.y);
    } else
        v = axisCoverage(floor(sampleUv * outputInfo.xy) - vec2(mapping.w));
    fragColor = vec4(v);
}
