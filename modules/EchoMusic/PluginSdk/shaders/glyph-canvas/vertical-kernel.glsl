#extension GL_GOOGLE_include_directive : require
#include "filter-params.glsl"
layout(local_size_x = 1, local_size_y = 32, local_size_z = 1) in;
layout(binding = 1) uniform sampler2D source;
layout(std140, binding = 6) uniform kernelBuf
{
    vec4 taps[128];
};
layout(rgba16f, binding = 7) uniform image2D result;
void storeResult(ivec2 position, float value)
{
    // Each dispatch writes one field/channel. QRhi tracks the read-write atlas
    // across separate compute passes, so overlapping channels never race.
    ivec2 at = ivec2(mvp[0].xy) + position;
    vec4 previous = imageLoad(result, at);
    previous[int(mvp[0].z)] = value;
    imageStore(result, at, previous);
}
// The box path has at most256 input rows; the pyramid keeps sigma near4.
// Select the small workspace when its complete32-row footprint fits.
shared vec2 rows[ROW_CAPACITY];
vec2 fetchRow(int x, int y)
{
    if (y < 0 || y >= int(inputInfo.y))
        return vec2(0.);
    bool reduced = outputInfo.x < inputInfo.x || outputInfo.y < inputInfo.y;
    return vec2(x < 0 || x >= int(inputInfo.x) ? 0. : texelFetch(source, ivec2(x, y), 0).r,
                !reduced || x + 1 < 0 || x + 1 >= int(inputInfo.x)
                    ? 0.
                    : texelFetch(source, ivec2(x + 1, y), 0).r);
}
void main()
{
    int lane = int(gl_LocalInvocationID.y);
    int x = int(mapping.x) + int(gl_WorkGroupID.x);
    int y0 = int(mapping.y) + int(gl_WorkGroupID.y) * 32;
    int y = y0 + lane;
    bool covered = int(gl_WorkGroupID.y) * 32 + lane < int(mapping.w);
    if (x < 0 || x >= int(outputInfo.x) || y0 >= int(outputInfo.y) || y0 + 31 < 0) {
        if (covered)
            storeResult(ivec2(x, y), 0.);
        return;
    }
    vec2 scale = inputInfo.xy / outputInfo.xy;
    float px = (float(x) + .5) * scale.x - .5;
    int loX = int(floor(px));
    int firstLo = int(floor((float(max(y0, 0)) + .5) * scale.y - .5));
    int lastCovered = y0 + min(31, int(mapping.w) - int(gl_WorkGroupID.y) * 32 - 1);
    int lastLo = int(floor((float(min(lastCovered, int(outputInfo.y) - 1)) + .5) * scale.y - .5));
    int radius = -int(taps[0].x);
    int first = firstLo - radius;
    int count = lastLo + 1 + radius - first + 2;
    for (int i = lane; i < count; i += 32)
        rows[i] = fetchRow(loX, first + i);
    barrier();
    if (!covered)
        return;
    if (y < 0 || y >= int(outputInfo.y)) {
        storeResult(ivec2(x, y), 0.);
        return;
    }
    float py = (float(y) + .5) * scale.y - .5;
    int loY = int(floor(py));
    vec4 total = vec4(0.);
    bool reduced = outputInfo.x < inputInfo.x || outputInfo.y < inputInfo.y;
    if (!reduced) {
        float sum = 0.;
        for (int i = 0; i < int(options.w); ++i) {
            vec4 tap = taps[i];
            int sy = loY + int(tap.x), row = sy - first;
            sum += tap.z * rows[row].x;
            if (tap.w != 0.)
                sum += tap.w * rows[row + 1].x;
        }
        storeResult(ivec2(x, y), floor(sum / options.z * 255. + .5) / 255.);
        return;
    }
    for (int i = 0; i < int(options.w); ++i) {
        vec4 tap = taps[i];
        int sy = loY + int(tap.x);
        int row = sy - first;
        total += tap.z * vec4(rows[row], rows[row + 1]);
        if (tap.w != 0.)
            total += tap.w * vec4(rows[row + 1], rows[row + 2]);
    }
    vec4 q = floor(total / options.z * 255. + .5) / 255.;
    float v = reduced ? mix(mix(q.x, q.y, fract(px)), mix(q.z, q.w, fract(px)), fract(py)) : q.x;
    storeResult(ivec2(x, y), v);
}
