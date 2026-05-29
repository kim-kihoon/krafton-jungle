#include "Common/Functions.hlsli"
#include "Common/ForwardLighting.hlsli"

cbuffer PerShader1 : register(b2)
{
    float4 TopColor;
    float4 SideColor;
    float4 MinorLineColor;
    float4 MajorLineColor;
    float GridScale;
    float MajorLineInterval;
    float MinorLineWidth;
    float MajorLineWidth;
};

struct PrototypeGridVS_Output
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR0;
    float3 worldPos : TEXCOORD0;
};

struct PrototypeGridPS_Output
{
    float4 Color : SV_TARGET0;
    float4 Normal : SV_TARGET1;
    float4 Culling : SV_TARGET2;
};

PrototypeGridVS_Output VS(VS_Input_PNCTT input)
{
    PrototypeGridVS_Output output;

    float4 worldPos = mul(float4(input.position, 1.0f), Model);
    output.worldPos = worldPos.xyz;
    output.position = mul(mul(worldPos, View), Projection);
    output.normal = normalize(mul(input.normal, (float3x3) NormalMatrix));
    output.color = input.color;

    return output;
}

float2 SelectProjectionCoords(float3 worldPos, float3 normal)
{
    float3 axis = abs(normal);

    if (axis.z >= axis.x && axis.z >= axis.y)
    {
        return worldPos.xy;
    }

    if (axis.x >= axis.y)
    {
        return worldPos.yz;
    }

    return worldPos.xz;
}

float4 SelectBaseColor(float3 normal)
{
    float3 axis = abs(normal);
    return (axis.z >= axis.x && axis.z >= axis.y) ? TopColor : SideColor;
}

float GridLineMask(float2 coord, float lineWidth)
{
    float2 wrapped = frac(coord);
    float2 distToLine = min(wrapped, 1.0f - wrapped);
    float2 aa = max(fwidth(coord), float2(0.0001f, 0.0001f));
    float2 lineMask = 1.0f - smoothstep(lineWidth, lineWidth + aa, distToLine);

    return saturate(max(lineMask.x, lineMask.y));
}

PrototypeGridPS_Output PS(PrototypeGridVS_Output input)
{
    PrototypeGridPS_Output output;

    float3 N = normalize(input.normal);
    float cellSize = max(abs(GridScale), 0.0001f);
    float majorInterval = max(abs(MajorLineInterval), 1.0f);
    float minorWidth = max(abs(MinorLineWidth), 0.0f);
    float majorWidth = max(abs(MajorLineWidth), 0.0f);

    float2 gridCoord = SelectProjectionCoords(input.worldPos, N) / cellSize;
    float minorMask = GridLineMask(gridCoord, minorWidth);
    float majorMask = GridLineMask(gridCoord / majorInterval, majorWidth / majorInterval);

    float4 baseColor = SelectBaseColor(N) * input.color;
    float3 gridAlbedo = lerp(baseColor.rgb, MinorLineColor.rgb, minorMask * MinorLineColor.a);
    gridAlbedo = lerp(gridAlbedo, MajorLineColor.rgb, majorMask * MajorLineColor.a);

    float3 V = normalize(CameraWorldPos - input.worldPos);
    float3 diffuse = AccumulateDiffuse(input.worldPos, N, input.position);
    float3 specular = AccumulateSpecular(input.worldPos, N, V, 32.0f, input.position);
    float3 finalColor = gridAlbedo * diffuse + specular;
    finalColor = ApplyWireframe(finalColor);

    output.Color = float4(finalColor, baseColor.a);
    output.Normal = float4(N, 1.0f);
    output.Culling = ComputeCullingHeatmap(input.position, input.worldPos);

    return output;
}
