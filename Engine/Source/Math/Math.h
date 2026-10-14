#pragma once

// Arclight math conventions. Every system (renderer, physics, importers, editor) follows these.
//
//   Handedness   Right-handed. +X right, +Y up, +Z toward the viewer; cameras look down -Z.
//                Matches glTF, so imported assets need no axis conversion.
//   Vectors      Column vectors: v' = M * v. Transforms compose right to left,
//                so world = translation * rotation * scale applies scale first.
//   Storage      Column-major (Mat4 stores four columns). Matches HLSL's default cbuffer
//                packing, Metal's float4x4 and glTF, so matrices upload without a transpose.
//   Angles       Radians everywhere. Degrees only at the UI boundary (toRadians / toDegrees).
//   Clip depth   NDC depth in [0, 1], as on both D3D12 and Metal.
//   Rotation     Quaternions. Euler angles only for editing and display.

#include "Math/MathUtils.h"
#include "Math/Matrix.h"
#include "Math/Quaternion.h"
#include "Math/Vector.h"
