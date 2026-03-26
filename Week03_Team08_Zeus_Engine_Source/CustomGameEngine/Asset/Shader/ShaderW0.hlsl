cbuffer FrameConstant : register(b0)
{
    row_major matrix View;
    row_major matrix Projection;
};

cbuffer ObjectConstant : register(b1)
{
    row_major matrix World;
    float4 Color;
    int bIsSelected;
    uint ViewMode;
    float2 Padding;
};

struct VS_INPUT
{
    float4 Pos : POSITION;
    float4 Color : COLOR;
    float3 Normal : NORMAL;
};

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float4 Color : COLOR;
    float3 Normal : NORMAL;
    float LinearDepth : TEXCOORD0;
};

PS_INPUT mainVS(VS_INPUT Input)
{
    PS_INPUT Output;
    matrix ClipSpace = mul(mul(World, View), Projection);
    float4 Pos = mul(Input.Pos, ClipSpace);
    Output.Pos = Pos;
    Output.Color = Input.Color;
    Output.Normal = mul(Input.Normal, (float3x3) World);
    Output.LinearDepth = Pos.w;
    return Output;
}

float4 mainPS(PS_INPUT Input) : SV_TARGET
{  
    float4 FinalColor;
    
    // Unlit 및 Wireframe Mode 처리    
    if (ViewMode == 1 || ViewMode == 2)
    {
        FinalColor = Input.Color * Color;
    }
    
    // Normals Mode 시각화
    else if (ViewMode == 3)
    {
        float3 n = normalize(Input.Normal);
        FinalColor = float4(n * 0.5f + 0.5f, 1.0f);
    }
    
    // Depth Mode 시각화
    else if (ViewMode == 4)
    {
        float MaxDistance = 50.0f;
        float NormalizedDepth = saturate(Input.LinearDepth / MaxDistance);
        
        float FinalDepth = pow(1.0f - NormalizedDepth, 2.0f);
        FinalColor = float4(FinalDepth, FinalDepth, FinalDepth, 1.0f);
    }
    
    // Lit Mode 시각화
    else
    {
        float3 LightDir = normalize(float3(1, 1, 1));
        float3 Normal = normalize(Input.Normal);
        float Diff = max(dot(Normal, LightDir), 0.1f);
        
        FinalColor.rgb = (Input.Color.rgb * Color.rgb) * Diff; // Diff는 빛의 세기   
        FinalColor.a = Input.Color.a * Color.a;
    }
    
    if (bIsSelected > 0)
    {
        FinalColor.a = FinalColor.a * 0.5f; // 선택 시 투명화.
    }
    return FinalColor;
}