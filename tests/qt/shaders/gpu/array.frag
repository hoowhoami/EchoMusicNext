#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(binding = 2) uniform sampler2DArray image;
void main()
{
    color = texture(image, vec3(uv, 1));
}
