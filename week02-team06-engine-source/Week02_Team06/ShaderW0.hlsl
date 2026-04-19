cbuffer TransformData : register(b0)
{
    row_major float4x4 MVP;
    float4 Color;
};

struct VS_INPUT
{
    float4 position : POSITION;
    float4 color : COLOR;
};


struct PS_INPUT
{
    float4 position : SV_Position;
    float4 color : COLOR;
};

PS_INPUT VS_MAIN(VS_INPUT input)
{
    PS_INPUT output;
    
    output.position = mul(input.position, MVP);
    
    if (Color.a == 1.f)
    {
        output.color = Color;
    }
    else
    {
        output.color = input.color;
    }
    return output;
}

float4 PS_MAIN(PS_INPUT input) : SV_Target
{
    if (Color.a > 0.0f)
    {
        return Color;
    }
    
    return input.color;
}