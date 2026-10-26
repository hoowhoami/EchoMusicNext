#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf
{
    mat4 qt_Matrix;
    float qt_Opacity;
    float blend;
    float hasPrevious;
    vec4 baseColor;
    float backgroundOpacity;
};
layout(binding = 1) uniform sampler2D source;
layout(binding = 2) uniform sampler2D previous;
void main()
{
    vec4 art = texture(source, qt_TexCoord0);
    // EchoMusic's lyric-blur-bg-overlay dims artwork by 50%. Keep the scrim
    // inside the existing final quad, below lyrics, controls and plugin spectra.
    art.rgb *= 0.5;
    vec4 background = art;
    if (blend < 1.0) {
        vec4 old = hasPrevious > 0.5 ? texture(previous, qt_TexCoord0) * vec4(0.5, 0.5, 0.5, 1.0)
                                     : baseColor;
        background = mix(old, art, blend);
    }
    background += baseColor * (1.0 - background.a);
    // Stable sub-LSB triangular dither at final SDR quantization. No moving
    // grain, no extra FBO, and exactly one application of window transparency.
    vec2 pixel = floor(gl_FragCoord.xy);
    float a = fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
    float b = fract(52.9829189 * fract(dot(pixel + 17.0, vec2(0.06711056, 0.00583715))));
    background.rgb += (a - b) / 255.0 * background.a;
    fragColor = background * backgroundOpacity * qt_Opacity;
}
