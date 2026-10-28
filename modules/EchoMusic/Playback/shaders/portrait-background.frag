#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf
{
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 currentCrop;
    vec2 incomingCrop;
    float progress;
    float brightness;
    float materialOpacity;
    vec4 baseColor;
};
layout(binding = 1) uniform sampler2D current;
layout(binding = 2) uniform sampler2D incoming;
void main()
{
    vec2 uv = qt_TexCoord0 - vec2(.5);
    vec4 a = texture(current, vec2(.5) + uv * currentCrop);
    vec4 b = texture(incoming, vec2(.5) + uv * incomingCrop);
    vec4 picture = mix(a, b, progress);
    vec3 color = picture.rgb + baseColor.rgb * (1.0 - picture.a);
    // A restrained lower scrim protects subtitles without dimming foreground
    // controls. Transparency is applied once in this final background quad.
    float shade = 1.0 - .48 * smoothstep(.4, 1.0, qt_TexCoord0.y);
    color *= brightness * shade;
    float alpha = materialOpacity * qt_Opacity;
    fragColor = vec4(color * alpha, alpha);
}
