#version 440
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 offset;
layout(location = 2) in vec4 tint;
layout(location = 0) out vec4 color;
layout(std140, binding = 0) uniform Frame
{
    vec4 viewport;
    vec4 outputInfo;
    mat4 correction;
}
frame;
void main()
{
    gl_Position = frame.correction * vec4(position + offset, 0, 1);
    color = tint;
}
