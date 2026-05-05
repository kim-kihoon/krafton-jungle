#include "UberSurface.hlsli"

FUberPSInput mainVS(FUberVSInput Input)
{
    return BuildSurfaceVertex(Input);
}

FUberPSOutput mainPS(FUberPSInput Input)
{
    const FUberSurfaceData Surface = EvaluateSurface(Input);

    FUberPSOutput Output;
    Output.Color = float4(Surface.Albedo, 1.0f);
    Output.Normal = float4(Surface.WorldNormal * 0.5f + 0.5f, 1.0f);
    Output.WorldPos = float4(Surface.WorldPos, 1.0f);
    return Output;
}
