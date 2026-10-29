#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(std140, binding = 1) uniform Color
{
    vec4 multiplier;
}
params;
layout(binding = 2) uniform sampler2D image;
void main()
{
    color = texture(image, uv) * params.multiplier;
}
