#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 firstColor;
layout(location = 1) out vec4 secondColor;
void main()
{
    firstColor = vec4(.25, 0, 0, 1);
    secondColor = vec4(0, .25, 0, 1);
}
