#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(binding = 1) uniform sampler2D sourceDepth;
void main()
{
    color = vec4(vec3(texture(sourceDepth, uv).r), 1.0);
}
