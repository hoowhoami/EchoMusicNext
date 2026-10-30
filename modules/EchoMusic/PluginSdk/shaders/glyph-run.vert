#version 440
layout(location = 0) in vec2 position;
layout(location = 1) in vec4 texcoord;
layout(location = 2) in vec4 sourceBounds;
layout(location = 3) in vec4 color;
layout(location = 4) in vec4 effect;
layout(location = 5) in vec2 haloTexcoord;
layout(location = 6) in vec4 haloSourceBounds;
layout(location = 8) in vec4 radii;
layout(location = 9) in vec4 haloColorA;
layout(location = 10) in vec4 haloColorB;
layout(location = 11) in vec4 haloColorC;
layout(location = 12) in vec4 bodyEffectsTexcoord;
layout(location = 13) in vec4 bodyEffectsSourceBounds;
layout(location = 14) in vec2 runLocalPosition;
layout(location = 15) in vec4 clipRect;
layout(location = 0) out vec4 uv;
layout(location = 1) out vec4 bounds;
layout(location = 2) out vec4 tint;
layout(location = 3) out vec4 effects;
layout(location = 4) out vec2 haloUv;
layout(location = 5) out vec4 haloBounds;
layout(location = 6) out vec4 haloRadii;
layout(location = 7) out vec4 haloA;
layout(location = 8) out vec4 haloB;
layout(location = 9) out vec4 haloC;
layout(location = 10) out vec4 bodyEffectsUv;
layout(location = 11) out vec4 bodyEffectsBounds;
layout(location = 12) out vec2 runLocal;
layout(location = 13) out vec4 clipBounds;
layout(std140, binding = 0) uniform buf
{
    mat4 matrix;
    float opacity;
};
void main()
{
    runLocal = runLocalPosition;
    clipBounds = clipRect;
    uv = texcoord;
    bounds = sourceBounds;
    tint = color;
    effects = effect;
    haloUv = haloTexcoord;
    haloBounds = haloSourceBounds;
    haloRadii = radii;
    haloA = haloColorA;
    haloB = haloColorB;
    haloC = haloColorC;
    bodyEffectsUv = bodyEffectsTexcoord;
    bodyEffectsBounds = bodyEffectsSourceBounds;
    gl_Position = matrix * vec4(position, 0.0, 1.0);
}
