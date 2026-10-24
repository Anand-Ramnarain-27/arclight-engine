// An infinite grid on the y = 0 plane, drawn as one full-screen triangle with no vertex buffer.
// Each pixel casts a ray from the camera, intersects the plane, and is covered by a line where the
// hit point lies near a multiple of the cell size. Alpha-blended over the scene, depth-tested
// against it with the depth of the hit point, never writing depth.

#include "Constants.hlsli"

// Mirrored by GridConstants in Editor/Source/Scene/EditorGrid.cpp.
cbuffer GridConstants : register(b1)
{
    float cellSize;       // World units between minor lines.
    float majorEvery;     // Minor cells per major cell.
    float fadeDistance;   // World units from the camera, along the plane, where the grid has faded out.
    float opacity;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 ndc : TEXCOORD0;
};

struct PSOutput
{
    float4 color : SV_Target0;
    float depth : SV_Depth;
};

// Vertices (-1, -1), (3, -1) and (-1, 3): one triangle covering the whole viewport once clipped.
// A single triangle has no diagonal seam, along which two would shade some pixels twice.
VSOutput VSMain(uint vertexId : SV_VertexID)
{
    const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VSOutput output;
    output.ndc = uv * 2.0 - 1.0;
    // Any depth inside [0, 1] does; the pixel shader replaces it.
    output.position = float4(output.ndc, 0.5, 1.0);
    return output;
}

float3 Unproject(float2 ndc, float depth)
{
    const float4 world = mul(inverseViewProjection, float4(ndc, depth, 1.0));
    return world.xyz / world.w;
}

// How much of a pixel is covered by the lines through every multiple of `spacing`, each about one
// pixel wide. fwidth() gives the change across one pixel, so the distance to the nearest line is
// measured in pixels whatever the distance or angle. Fades out where the lines are so dense they
// would merge into moire.
float GridCoverage(float2 planePosition, float spacing)
{
    const float2 cells = planePosition / spacing;
    const float2 cellsPerPixel = max(fwidth(cells), 1e-6);
    const float2 pixelsToLine = abs(frac(cells - 0.5) - 0.5) / cellsPerPixel;
    const float2 coverage = 1.0 - saturate(pixelsToLine);
    const float density = max(cellsPerPixel.x, cellsPerPixel.y);
    return max(coverage.x, coverage.y) * (1.0 - smoothstep(0.1, 0.3, density));
}

// Coverage of the single line where `coordinate` is 0.
float AxisCoverage(float coordinate)
{
    return 1.0 - saturate(abs(coordinate) / max(fwidth(coordinate), 1e-6));
}

PSOutput PSMain(VSOutput input)
{
    // Any depth inside the view volume gives a second point on the pixel's ray. 0.5 is valid for
    // both the standard and the reversed projection; 0 would be at infinity for reversed-Z.
    const float3 direction = normalize(Unproject(input.ndc, 0.5) - cameraPosition);

    // Rays parallel to the plane or pointing away from it hit nothing. Every pixel still computes
    // a hit point, clamped, because fwidth() needs neighbouring pixels to run the same code;
    // they are discarded at the end.
    const float distanceAlongRay = abs(direction.y) > 1e-6 ? -cameraPosition.y / direction.y : -1.0;
    const float3 hit = cameraPosition + direction * max(distanceAlongRay, 0.0);
    const float4 clipPosition = mul(viewProjection, float4(hit, 1.0));
    const float depth = clipPosition.z / clipPosition.w;

    const float minor = GridCoverage(hit.xz, cellSize) * 0.3;
    const float major = GridCoverage(hit.xz, cellSize * majorEvery) * 0.55;
    const float xAxis = AxisCoverage(hit.z);   // The line z = 0 runs along X.
    const float zAxis = AxisCoverage(hit.x);

    // Display-referred colours: the grid is drawn into the display target after tone mapping
    // (see Renderer::beginOverlayPass).
    float3 color = float3(0.55, 0.55, 0.58);
    float alpha = max(minor, major);
    color = lerp(color, float3(0.90, 0.25, 0.25), xAxis);
    alpha = max(alpha, xAxis * 0.9);
    color = lerp(color, float3(0.25, 0.45, 0.95), zAxis);
    alpha = max(alpha, zAxis * 0.9);

    const float distanceOnPlane = length(hit.xz - cameraPosition.xz);
    alpha *= opacity * (1.0 - saturate(distanceOnPlane / fadeDistance));

    // Behind the camera, beyond a standard projection's far plane, or uncovered.
    clip(distanceAlongRay > 0.0 && depth >= 0.0 && depth <= 1.0 && alpha > 0.002 ? 1.0 : -1.0);

    PSOutput output;
    output.color = float4(color, alpha);
    output.depth = depth;
    return output;
}
