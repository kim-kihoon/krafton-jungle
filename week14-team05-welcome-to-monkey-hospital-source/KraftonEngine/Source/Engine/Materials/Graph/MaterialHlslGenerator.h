#pragma once

#include "Materials/Graph/MaterialGraphTypes.h"
#include "Materials/MaterialSourceTypes.h"

struct FMaterialCompileOptions
{
    FString               MaterialPath;
    FString               MaterialGuid;
    EMaterialGraphTarget  Domain            = EMaterialGraphTarget::Surface;
    ERenderPass           RenderPass        = ERenderPass::Opaque;
    EBlendState           BlendState        = EBlendState::Opaque;
    EBlendMode            BlendMode         = EBlendMode::Opaque;
    EDepthStencilState    DepthStencilState = EDepthStencilState::Default;
    ERasterizerState      RasterizerState   = ERasterizerState::SolidBackCull;
    bool                  bReceiveLighting  = false; // Surface/ParticleMesh lighting enable. Off면 ShadingModel과 무관하게 Unlit path.
    // Surface 도메인 라이팅 모델 — ReceiveLighting이 켜져 있고 Unlit이 아닐 때만
    // ForwardLighting.hlsli를 통해 directional/point/spot + shadow가 적용된다.
    EMaterialShadingModel ShadingModel      = EMaterialShadingModel::DefaultLit;
    float                 OpacityMaskClipValue = 0.333f;
};

struct FMaterialCompileResult
{
    FString GeneratedShaderPath;
    FString GeneratedHlsl;

    // Stable generated shader files are keyed in memory by source hash/revision, not by filename.
    FString SourceHashString;
    uint64  SourceHash = 0;
    uint32  CompileRevision = 0;

    TMap<FString, FMaterialCompiledParameter> Parameters;
    TMap<FString, FMaterialCompiledTexture>   Textures;
    TArray<FString>                           Errors;
};

class FMaterialHlslGenerator
{
public:
    static bool Generate(const FMaterialGraph& Graph, const FMaterialCompileOptions& Options, FMaterialCompileResult& OutResult);
};