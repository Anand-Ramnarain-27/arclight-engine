// Shared by the auto-exposure compute shaders and the tone-mapping pass.

#ifndef ARCLIGHT_EXPOSURE_HLSLI
#define ARCLIGHT_EXPOSURE_HLSLI

// Match AutoExposure in Engine/Source/Renderer/AutoExposure.h.
#define HISTOGRAM_BIN_COUNT 256
// The state buffer holds the newest histogram in its first HISTOGRAM_BIN_COUNT entries, then these
// two floats as their bits.
#define EXPOSURE_ADAPTED_LUMINANCE (HISTOGRAM_BIN_COUNT + 0)
#define EXPOSURE_FRAME_LUMINANCE (HISTOGRAM_BIN_COUNT + 1)

// Below this a pixel counts as black: it goes in bin 0, which the average leaves out, so a black
// background does not drag the exposure up.
static const float kBlackLuminance = 1.0e-5;

// Relative luminance of linear Rec. 709 / sRGB primaries.
float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

#endif
