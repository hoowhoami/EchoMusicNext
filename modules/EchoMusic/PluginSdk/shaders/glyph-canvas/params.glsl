layout(std140, binding = 0) uniform buf
{
    mat4 mvp;
    mat4 deviceMatrix;
    mat4 inverseDevice;
    vec4 outputInfo;
    vec4 inputInfo;
    vec4 mapping;
    vec4 prior;
    vec4 options;
    vec4 fontPose;
    vec4 bodyRect[4];
    vec4 bodyUv[4];
    vec4 pathRect[3];
    vec4 fieldRect[3];
    vec4 quadRect;
    vec4 fillColor;
    vec4 tintColor;
    vec4 shadowColor[3];
    vec4 clipRect;
    vec4 alphas;
    vec4 channels;
};
