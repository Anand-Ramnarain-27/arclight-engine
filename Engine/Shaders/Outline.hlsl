// An outline around the selection, drawn as one full-screen triangle over the scene: a pixel is on
// the outline when it is outside the selection mask but has a masked pixel within `radius` pixels.
// Alpha-blended, with no depth test, so the outline shows through whatever covers the selection.

// Mirrored by OutlineConstants in Editor/Source/Scene/SelectionOutline.cpp.
cbuffer OutlineConstants : register(b1)
{
    float4 color;
    int radius;   // Pixels, at most kMaxRadius.
};

// Read with Load(), which needs no sampler: the mask is the scene's size, so pixels match one to one.
Texture2D<float4> g_mask : register(t0);

static const int kMaxRadius = 4;

float4 VSMain(uint vertexId : SV_VertexID) : SV_Position
{
    const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    return float4(uv * 2.0 - 1.0, 0.5, 1.0);
}

bool IsMasked(int2 pixel, int2 lastPixel)
{
    // Clamped: out-of-range reads return 0 on D3D12 but are undefined in Metal.
    return g_mask.Load(int3(clamp(pixel, int2(0, 0), lastPixel), 0)).r > 0.5;
}

float4 PSMain(float4 position : SV_Position) : SV_Target0
{
    uint width;
    uint height;
    g_mask.GetDimensions(width, height);
    const int2 lastPixel = int2(width, height) - 1;
    const int2 pixel = int2(position.xy);

    if (IsMasked(pixel, lastPixel))
        discard;

    const int r = clamp(radius, 1, kMaxRadius);
    for (int y = -r; y <= r; ++y)
    {
        for (int x = -r; x <= r; ++x)
        {
            if (x * x + y * y <= r * r && IsMasked(pixel + int2(x, y), lastPixel))
                return color;
        }
    }
    discard;
    return float4(0.0, 0.0, 0.0, 0.0);
}
