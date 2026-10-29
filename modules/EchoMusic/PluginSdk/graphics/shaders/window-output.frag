#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform Buf
{
    mat4 qt_Matrix;
    float qt_Opacity;
    float hdrActive;
    float whiteLevel;
    mat4 gamutTransform;
}
ubuf;
layout(binding = 1) uniform sampler2D source;
vec3 pq(vec3 c)
{
    vec3 y = pow(max(c, vec3(0)) * ubuf.whiteLevel / 10000.0, vec3(2610.0 / 16384.0));
    return pow((vec3(3424.0 / 4096.0) + (2413.0 / 128.0) * y) / (vec3(1) + (2392.0 / 128.0) * y),
               vec3(2523.0 / 32.0));
}
void main()
{
    vec4 c = texture(source, qt_TexCoord0);
    if (ubuf.hdrActive > 0.5 && c.a > 0.000001) {
        vec3 rgb = max(c.rgb / c.a, vec3(0));
        rgb = mix(rgb / 12.92, pow((rgb + 0.055) / 1.055, vec3(2.4)),
                  greaterThan(rgb, vec3(0.04045)));
        rgb = (ubuf.gamutTransform * vec4(rgb, 0)).rgb;
        if (ubuf.hdrActive > 1.5 && ubuf.hdrActive < 2.5)
            rgb = pq(rgb);
        c.rgb = rgb * c.a;
    }
    fragColor = c * ubuf.qt_Opacity;
}
