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
float choose2(float n)
{
    return n < 0. ? 0. : (n + 1.) * (n + 2.) * .5;
}
float boxWeight(float offset)
{
    float w = options.x, w3 = w + (mod(w, 2.) < .5 ? 1. : 0.),
          border = mod(w, 2.) > .5 ? 3. * (w - 1.) * .5 : 3. * w * .5 - 1.;
    float k = offset + border;
    return (choose2(k) - 2. * choose2(k - w) - choose2(k - w3) + choose2(k - 2. * w) +
            2. * choose2(k - w - w3) - choose2(k - 2. * w - w3)) /
           (w * w * w3);
}
float axisCoverage(vec2 p)
{
    float total = 0., weight = 0.;
    int radius = options.x > 0. ? (int(mod(options.x, 2.)) == 1 ? 3 * (int(options.x) - 1) / 2
                                                                : 3 * int(options.x) / 2 - 1)
                                : int(ceil(3. * prior.w));
    for (int i = -127; i <= 127; ++i) {
        if (abs(i) > radius)
            continue;
        float w =
            options.x > 0. ? boxWeight(float(i)) : exp(-.5 * float(i * i) / (prior.w * prior.w));
        weight += w;
        total +=
            w * readPixel(p + vec2(prior.z < .5 ? float(i) : 0., prior.z < .5 ? 0. : float(i)));
    }
    return floor(total / weight * 255. + .5) / 255.;
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
        v = mix(mix(axisCoverage(lo), axisCoverage(lo + vec2(1, 0)), f.x),
                mix(axisCoverage(lo + vec2(0, 1)), axisCoverage(lo + vec2(1, 1)), f.x), f.y);
    } else
        v = axisCoverage(floor(sampleUv * outputInfo.xy) - vec2(mapping.w));
    fragColor = vec4(v);
}
