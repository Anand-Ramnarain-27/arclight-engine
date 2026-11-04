// The irradiance cube: for every normal direction, the light arriving over the hemisphere around
// it, weighted by the cosine to the normal, divided by pi. A Lambertian surface with that normal
// then reflects albedo * this value. Irradiance changes slowly with the normal, so a 32-pixel cube
// holds it.

#include "Ibl.hlsli"

TextureCube<float4> g_environment : register(t0);
SamplerState g_sampler : register(s0);   // Trilinear, clamped.

float4 PSMain(VSOutput input) : SV_Target0
{
    const float3 normal = normalize(CubeFaceDirection(face, input.st.x, input.st.y));
    const float3 up = abs(normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, 1.0);
    const float3 right = normalize(cross(up, normal));
    const float3 forward = cross(normal, right);

    // A Riemann sum over the hemisphere in polar coordinates. Each sample is weighted by
    // cos(theta) sin(theta) (the cosine law and the solid angle of the polar cell); the sum times
    // pi / count is the integral over pi. The steps are about 3 degrees, so the source is read at
    // the mip whose texels are about that size, which keeps bright small features from aliasing.
    const float step = 0.05;
    const float lod = max(log2(sourceSize / 32.0), 0.0);
    float3 sum = 0.0;
    float count = 0.0;
    for (float phi = 0.0; phi < 2.0 * PI; phi += step)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += step)
        {
            const float3 local = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            const float3 direction = local.x * right + local.y * forward + local.z * normal;
            sum += g_environment.SampleLevel(g_sampler, direction, lod).rgb * cos(theta) * sin(theta);
            count += 1.0;
        }
    }
    return float4(PI * sum / count, 1.0);
}
