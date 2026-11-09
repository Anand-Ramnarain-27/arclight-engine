// Counts the scene's pixels into HISTOGRAM_BIN_COUNT bins by the logarithm of their luminance. Each
// thread group first counts its 16x16 pixels in group-shared memory, where atomics are cheap, then
// adds its counts to the histogram in the storage buffer with one atomic per bin: 256 global atomics
// per group instead of one per pixel.

#include "Exposure.hlsli"

// Mirrored by HistogramConstants in Engine/Source/Renderer/AutoExposure.cpp.
cbuffer HistogramConstants : register(b0)
{
    uint2 sceneSize;                  // Pixels.
    float minLogLuminance;            // log2 of the luminance at the bottom of bin 1.
    float inverseLogLuminanceRange;   // 1 / (log2 of the top of the last bin - minLogLuminance).
};

Texture2D<float4> g_scene : register(t0);
// Zero before the dispatch; ExposureAverage.hlsl clears it after reading it.
RWStructuredBuffer<uint> g_histogram : register(u0);

// One per thread: the group has exactly HISTOGRAM_BIN_COUNT threads.
groupshared uint s_bins[HISTOGRAM_BIN_COUNT];

uint LuminanceBin(float3 color)
{
    // Negative and NaN values fail the test and count as black.
    const float luminance = Luminance(color);
    if (!(luminance >= kBlackLuminance))
        return 0;
    const float position = saturate((log2(luminance) - minLogLuminance) * inverseLogLuminanceRange);
    return 1 + uint(position * (HISTOGRAM_BIN_COUNT - 2) + 0.5);
}

[numthreads(16, 16, 1)]
void CSMain(uint3 pixel : SV_DispatchThreadID, uint threadIndex : SV_GroupIndex)
{
    s_bins[threadIndex] = 0;
    GroupMemoryBarrierWithGroupSync();

    // Groups on the right and bottom edges reach past the scene when its size is not a multiple of 16.
    if (all(pixel.xy < sceneSize))
    {
        const float3 color = g_scene.Load(int3(pixel.xy, 0)).rgb;
        InterlockedAdd(s_bins[LuminanceBin(color)], 1);
    }
    GroupMemoryBarrierWithGroupSync();

    const uint count = s_bins[threadIndex];
    if (count != 0)
        InterlockedAdd(g_histogram[threadIndex], count);
}
