#version 440
layout(location = 0) out vec4 color;
layout(std140, binding = 1) uniform Source
{
    vec4 ink;
};
void main()
{
    color = ink;
}
