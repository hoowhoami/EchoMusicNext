// SPDX-License-Identifier: GPL-3.0-only
#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf
{
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 viewSize;
    float cornerRadius;
    vec2 cropScale;
    vec4 qt_SubRect_source;
};
layout(binding = 1) uniform sampler2D source;
void main()
{
    vec2 halfSize = viewSize * 0.5;
    float radius = clamp(cornerRadius, 0.0, min(halfSize.x, halfSize.y));
    vec2 q = abs(qt_TexCoord0 * viewSize - halfSize) - halfSize + radius;
    float distance = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
    // Derivatives keep a one-device-pixel boundary through DPR, item scaling
    // and circular avatars. The premultiplied source keeps its own alpha.
    float edge = max(length(vec2(dFdx(distance), dFdy(distance))), 0.0001);
    float coverage = clamp(0.5 - distance / edge, 0.0, 1.0);
    vec2 uv = (qt_TexCoord0 - 0.5) * cropScale + 0.5;
    // Qt supplies the atlas subrect, avoiding a standalone image copy while
    // keeping local 0..1 coordinates for crop and rounded geometry.
    uv = qt_SubRect_source.xy + uv * qt_SubRect_source.zw;
    fragColor = texture(source, uv) * (qt_Opacity * coverage);
}
