#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf
{
    mat4 qt_Matrix;
    float qt_Opacity;
};
layout(binding = 1) uniform sampler2D source;
void main()
{
    vec4 value = texture(source, qt_TexCoord0);
    fragColor = vec4((value.r + 1.0) / 2.0, value.g / 4.0, value.b, value.a) * qt_Opacity;
}
