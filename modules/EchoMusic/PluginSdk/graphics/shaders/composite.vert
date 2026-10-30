#version 440
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
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    uv = vec2(p.x, frame.outputInfo.w > 0.5 ? 1.0 - p.y : p.y);
    gl_Position = frame.correction * vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
