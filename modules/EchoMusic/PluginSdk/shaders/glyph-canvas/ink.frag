#version 440
#extension GL_GOOGLE_include_directive : require
#include "params.glsl"
#include "ink-fn.glsl"
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
void main()
{
    vec2 physical = uv * outputInfo.xy + fontPose.yz;
    vec2 local = (inverseDevice * vec4(physical, 0, 1)).xy;
    float v = nativeInk(local).a;
    fragColor = vec4(floor(v * 255. + .5) / 255.);
}
