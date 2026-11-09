// Reduces the luminance histogram to the scene's average luminance (the mean of log2 luminance, so
// a few very bright pixels do not dominate it), then moves the adapted luminance toward it, faster
// when the scene gets brighter, as eyes adapt. One group of HISTOGRAM_BIN_COUNT threads, a thread
// per bin.

#include "Exposure.hlsli"

// Mirrored by ExposureConstants in Engine/Source/Renderer/AutoExposure.cpp.
cbuffer ExposureConstants : register(b0)
{
    uint pixelCount;
    float minLogLuminance;
    float logLuminanceRange;
    float adaptBrighter;   // Fraction of the way to move this frame: 1 - exp(-dt * speed).
    float adaptDarker;
    uint resetAdaptation;  // Non-zero: jump straight to this frame's average.
    uint2 padding;
};

RWStructuredBuffer<uint> g_histogram : register(u0);
// See Exposure.hlsli for the layout.
RWStructuredBuffer<uint> g_state : register(u1);

groupshared float s_sums[HISTOGRAM_BIN_COUNT];

[numthreads(HISTOGRAM_BIN_COUNT, 1, 1)]
void CSMain(uint bin : SV_GroupIndex)
{
    const uint count = g_histogram[bin];
    g_state[bin] = count;
    g_histogram[bin] = 0;

    // Sum of count x bin over all bins. First within each wave: WaveActiveSum adds across the lanes
    // in hardware, with no memory traffic. Each wave's first lane then keeps the wave's total and the
    // other lanes 0, and a tree in group-shared memory adds those up. The tree does not depend on
    // which threads share a wave, or on how many lanes a wave has (32 on NVIDIA and Apple, 32 or 64
    // on AMD).
    const float waveSum = WaveActiveSum(float(count) * float(bin));
    s_sums[bin] = WaveIsFirstLane() ? waveSum : 0.0;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint stride = HISTOGRAM_BIN_COUNT / 2; stride > 0; stride >>= 1)
    {
        if (bin < stride)
            s_sums[bin] += s_sums[bin + stride];
        GroupMemoryBarrierWithGroupSync();
    }

    if (bin != 0)
        return;

    float adapted = asfloat(g_state[EXPOSURE_ADAPTED_LUMINANCE]);
    // Thread 0 read bin 0, the black pixels, which the average leaves out.
    const uint lit = pixelCount - min(count, pixelCount);
    if (lit == 0)
    {
        // An all-black frame has no average; the exposure stays where it was.
        g_state[EXPOSURE_FRAME_LUMINANCE] = asuint(0.0);
        return;
    }

    const float averageBin = s_sums[0] / float(lit);
    const float averageLog = (averageBin - 1.0) / float(HISTOGRAM_BIN_COUNT - 2) * logLuminanceRange + minLogLuminance;
    const float average = exp2(averageLog);

    // The buffer starts as zeros, and a NaN must not stick.
    if (resetAdaptation != 0 || !(adapted > 0.0) || isinf(adapted))
        adapted = average;
    else
        adapted += (average - adapted) * (average > adapted ? adaptBrighter : adaptDarker);

    g_state[EXPOSURE_ADAPTED_LUMINANCE] = asuint(adapted);
    g_state[EXPOSURE_FRAME_LUMINANCE] = asuint(average);
}
