#pragma once

#include "Renderer/RenderObject.h"
#include "EngineTypes.h"
#include "TextTypes.h"
#include "VertexFont.h"
#include <d3d11.h>

struct FMaterial;
struct FFontAtlas;

struct TextRenderObject : public RenderObject
{
    void UploadVertices(ID3D11DeviceContext* Context);

    TArray<FVertexFont> Vertices;
    TArray<uint32> Indices;
	FFontAtlas* FontAtlas = nullptr;
    
    static constexpr UINT MaxQuads = 16384;
    static constexpr UINT MaxVertices = MaxQuads * 4;

    // 외부로 뺄 수 있을까? TextRenderObject는 데이터만을 들고 있도록 해보기
    void BuildFromEntries(
        const TArray<FTextEntry>& Entries);

    virtual bool Render(ID3D11DeviceContext* Context) override;
    void AppendText(const FTextEntry& Entry);
    void AppendGlyphQuad(
        const FVector& Anchor, const FVector& Right, const FVector& Up,
        float Width, float Height, float rOffset, float uOffset,
        float U0, float V0, float U1, float V1,
        TArray<FVector>& outVertices, TArray<uint32>& outIndices);
};
