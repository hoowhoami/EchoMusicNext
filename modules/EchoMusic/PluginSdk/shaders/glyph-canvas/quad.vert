#version 440
#extension GL_GOOGLE_include_directive : require
#include "params.glsl"
layout(location = 0) out vec2 uv;
void main()
{
    uv = vec2(gl_VertexIndex & 1, (gl_VertexIndex >> 1) & 1);
    gl_Position = mvp * vec4(uv * outputInfo.xy, 0., 1.);
}
