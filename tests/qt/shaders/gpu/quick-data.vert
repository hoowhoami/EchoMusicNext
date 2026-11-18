#version 440
layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;
layout(location = 0) out vec2 qt_TexCoord0;
layout(std140, binding = 0) uniform buf
{
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 ink;
};
layout(binding = 1) uniform sampler2D samples;
void main()
{
    vec4 value = texelFetch(samples, ivec2(2, 1), 0);
    qt_TexCoord0 = qt_MultiTexCoord0;
    vec4 position = qt_Vertex;
    position.xy += vec2(value.r - .75, value.g - 2.) * 64.;
    gl_Position = qt_Matrix * position;
}
