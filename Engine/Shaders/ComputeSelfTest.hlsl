// Run once at start-up in debug builds (Renderer.cpp) to check the compute path of the RHI on the
// real GPU: storage buffer and texture writes through u slots, constants, group-shared memory, group
// barriers and wave intrinsics. A 16x16 dispatch of four 8x8 groups.

// Mirrored by SelfTestConstants in Engine/Source/Renderer/Renderer.cpp.
cbuffer SelfTestConstants : register(b0)
{
    uint multiplier;
    uint3 padding;
};

// 16 * 16 values written per thread, then one per group: the threads that group counted.
RWStructuredBuffer<uint> g_values : register(u0);
RWTexture2D<float4> g_image : register(u1);

groupshared uint s_counts[64];

[numthreads(8, 8, 1)]
void CSMain(uint3 thread : SV_DispatchThreadID, uint3 group : SV_GroupID, uint threadIndex : SV_GroupIndex)
{
    const uint index = thread.y * 16 + thread.x;
    g_values[index] = index * multiplier + 1;
    g_image[thread.xy] = float4(thread.x / 15.0, thread.y / 15.0, 0.0, 1.0);

    // Same reduction as ExposureAverage.hlsl: per wave in hardware, then a tree across the group.
    const uint waveCount = WaveActiveSum(1u);
    s_counts[threadIndex] = WaveIsFirstLane() ? waveCount : 0u;
    GroupMemoryBarrierWithGroupSync();
    [unroll]
    for (uint stride = 32; stride > 0; stride >>= 1)
    {
        if (threadIndex < stride)
            s_counts[threadIndex] += s_counts[threadIndex + stride];
        GroupMemoryBarrierWithGroupSync();
    }
    if (threadIndex == 0)
        g_values[256 + group.y * 2 + group.x] = s_counts[0];
}
