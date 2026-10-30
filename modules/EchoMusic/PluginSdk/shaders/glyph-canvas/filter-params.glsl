layout(std140, binding = 0) uniform filterBuf
{
    mat4 mvp;
    vec4 outputInfo;
    vec4 inputInfo;
    vec4 mapping;
    vec4 prior;
    vec4 options;
};
