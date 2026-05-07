#include "../Common.hlsl"

cbuffer SkyBuffer : register(b2)
{
    row_major float4x4 InvView;
    row_major float4x4 InvProjection;
    float4 SkyZenithColor;
    float4 SkyHorizonColor;
    float4 SunColor;
    float4 SunDirectionAndDiskSize;
    float4 SkyParams0;
    float4 SkyParams1;
    float4 CameraForward;
    float4 CameraRight;
    float4 CameraUp;
};

struct VSOutput
{
    float4 ClipPos : SV_POSITION;
};

VSOutput mainVS(uint vertexID : SV_VertexID)
{
    VSOutput output;
    float2 pos;
    if (vertexID == 0)
        pos = float2(-1.0f, -1.0f);
    else if (vertexID == 1)
        pos = float2(-1.0f, 3.0f);
    else
        pos = float2(3.0f, -1.0f);

    output.ClipPos = float4(pos, 0.0f, 1.0f);
    return output;
}

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float3 BuildWorldViewDirection(float4 clipPos)
{
    float2 viewport = max(ViewportSize, float2(1.0f, 1.0f));
    float2 uv = clipPos.xy / viewport;
    float2 ndc = float2(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f);

    const bool bOrthographic = abs(Projection[3][3] - 1.0f) < 1e-4f;
    if (bOrthographic)
    {
        return normalize(CameraForward.xyz);
    }

    float4 clip = float4(ndc, 1.0f, 1.0f);
    float4 view = mul(clip, InvProjection);
    view /= max(abs(view.w), 1e-4f);

    float3 viewDir = normalize(view.xyz);
    float3 worldDir = mul(float4(viewDir, 0.0f), InvView).xyz;
    return normalize(worldDir);
}

float4 mainPS(VSOutput input) : SV_TARGET
{
    float3 worldDir = BuildWorldViewDirection(input.ClipPos);

    float upness = saturate(worldDir.z * 0.5f + 0.5f);
    float zenithT = saturate(pow(upness, max(SkyParams0.z, 0.05f)));
    float3 skyColor = lerp(SkyHorizonColor.rgb, SkyZenithColor.rgb, zenithT);

    float horizonBand = pow(saturate(1.0f - abs(worldDir.z)), max(SkyParams0.z * 0.7f, 0.05f));
    skyColor += SkyHorizonColor.rgb * horizonBand * (0.08f + SkyParams1.x * 0.24f);

    float belowHorizon = saturate(-worldDir.z);
    float3 groundColor = SkyHorizonColor.rgb * lerp(0.18f, 0.06f, SkyParams1.y);
    skyColor = lerp(skyColor, groundColor, pow(belowHorizon, 0.4f));

    float3 sunDir = normalize(SunDirectionAndDiskSize.xyz);
    float sunAmount = saturate(dot(worldDir, sunDir));
    float diskCos = cos(radians(max(SunDirectionAndDiskSize.w, 0.05f)));
    float haloCos = cos(radians(max(SunDirectionAndDiskSize.w * 8.0f, 0.25f)));

    float sunDisk = smoothstep(diskCos, 1.0f, sunAmount);
    float sunHalo = smoothstep(haloCos, 1.0f, sunAmount);
    sunHalo = pow(sunHalo, 2.0f);

    skyColor += SunColor.rgb * sunDisk * SkyParams0.x;
    skyColor += SunColor.rgb * sunHalo * SkyParams0.y;

    if (SkyParams0.w > 0.0f && worldDir.z > 0.0f)
    {
        float2 starCoord = float2(atan2(worldDir.y, worldDir.x), acos(saturate(worldDir.z)));
        starCoord *= 240.0f;

        float starNoise = Hash12(floor(starCoord));
        float starMask = step(0.9965f, starNoise);
        starMask *= pow(saturate((starNoise - 0.9965f) / 0.0035f), 3.0f);
        starMask *= saturate(worldDir.z) * SkyParams0.w;

        skyColor += starMask.xxx;
    }

    return float4(max(skyColor, 0.0f), 1.0f);
}
