#version 440
layout(location = 0) in vec2 position;
layout(location = 0) out vec2 uv;
layout(std140, binding = 0) uniform Frame
{
    vec4 viewport;
    vec4 outputInfo;
    mat4 correction;
}
frame;
void main()
{
    uv = vec2(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);
    gl_Position = frame.correction * vec4(position, 0, 1);
}
