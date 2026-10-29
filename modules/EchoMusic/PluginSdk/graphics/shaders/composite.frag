#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform Frame
{
    vec4 viewport;
    vec4 outputInfo;
    mat4 correction;
}
frame;
layout(binding = 1) uniform sampler2D source;
vec3 encode(vec3 c)
{
    return mix(c * 12.92, 1.055 * pow(max(c, vec3(0)), vec3(1.0 / 2.4)) - 0.055,
               greaterThan(c, vec3(0.0031308)));
}
void main()
{
    vec4 sampleColor = texture(source, uv);
    float a = clamp(sampleColor.a, 0.0, 1.0);
    vec3 c = clamp(sampleColor.rgb / max(a, 0.000001), vec3(0), vec3(65504));
    c = mix(c, vec3(0), isnan(c));
    if (frame.outputInfo.x < -0.5)
        c = clamp(c, 0.0, 1.0);
    else if (frame.outputInfo.x < 0.5) {
        float limit = 1.0;
        c /= limit;
        float peak = max(c.r, max(c.g, c.b));
        float mapped = peak <= 0.75 ? peak : 0.75 + 0.25 * (peak - 0.75) / (peak - 0.5);
        c *= peak > 0.0 ? mapped / peak : 1.0;
        c *= limit;
    }
    fragColor = vec4(encode(c) * a, a);
}
