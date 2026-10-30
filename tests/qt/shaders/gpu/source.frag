#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(binding = 2) uniform sampler2D sourceImage;
// Qt Quick RGBA8 layers contain premultiplied sRGB. Plugin working targets
// contain premultiplied linear values; the existing final compositor encodes SDR.
void main()
{
    vec4 sampleColor = texture(sourceImage, uv);
    vec3 straight = sampleColor.rgb / max(sampleColor.a, 0.000001);
    vec3 linear = mix(straight / 12.92, pow((straight + 0.055) / 1.055, vec3(2.4)),
                      greaterThan(straight, vec3(0.04045)));
    color = vec4(linear * sampleColor.a, sampleColor.a);
}
