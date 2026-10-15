// Constant buffers shared by every shader. Mirrored member for member by
// Engine/Source/Renderer/ShaderConstants.h; change both together.

#ifndef ARCLIGHT_CONSTANTS_HLSLI
#define ARCLIGHT_CONSTANTS_HLSLI

// Set once per camera. Matrices are column-major (HLSL's default for cbuffers, and the engine's
// storage order), applied as mul(matrix, columnVector).
cbuffer ViewConstants : register(b0)
{
    float4x4 viewProjection;   // World space to clip space.
};

#endif
