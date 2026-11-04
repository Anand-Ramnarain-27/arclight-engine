// Shared by the passes that turn an environment into image-based lighting (EnvironmentMaps.cpp).
// Each pass draws one full-screen triangle into one face (and mip level) of a cube render target;
// the pixel's position on the face gives the direction it stands for.

#ifndef ARCLIGHT_IBL_HLSLI
#define ARCLIGHT_IBL_HLSLI

#include "Brdf.hlsli"

// Mirrored by IblConstants in Engine/Source/Renderer/EnvironmentMaps.cpp.
cbuffer IblConstants : register(b1)
{
    uint face;            // The cube face being drawn, in D3D12/Metal order: +X, -X, +Y, -Y, +Z, -Z.
    float sourceLod;      // Mip level of the source to read when copying or reflecting it sharply.
    float roughness;      // Of this prefiltered mip level (the prefilter pass).
    float sourceSize;     // Pixels across one face of the source cube's mip 0.
    uint sampleCount;     // Importance samples per pixel.
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 st : TEXCOORD0;   // [-1, 1] across the face, left to right and top to bottom.
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VSOutput output;
    output.position = float4(uv * 2.0 - 1.0, 0.5, 1.0);
    // Clip space has +Y up; a face's t runs down its image.
    output.st = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    return output;
}

// The face selection table of the D3D12 and Vulkan specifications, as cubeFaceDirection() in
// Engine/Source/Renderer/CubeMap.cpp: the cube-space direction through (s, t) of a face.
float3 CubeFaceDirection(uint cubeFace, float s, float t)
{
    switch (cubeFace)
    {
        case 0:  return float3(1.0, -t, -s);
        case 1:  return float3(-1.0, -t, s);
        case 2:  return float3(s, 1.0, t);
        case 3:  return float3(s, -1.0, -t);
        case 4:  return float3(s, -t, 1.0);
        default: return float3(-s, -t, -1.0);
    }
}

#endif
