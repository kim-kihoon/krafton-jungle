#include "TextRenderObject.h"
#include "Renderer/Geometry.h"
#include "Renderer/Material.h"
#include "Math/MathHelper.h"
#include "Component/CameraComponent.h"
#include "Component/TextComponent.h"
#include "FontAtlas.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "EngineStatics.h"

void TextRenderObject::UploadVertices(ID3D11DeviceContext* Context)
{
    uint32 VertexCount = static_cast<UINT>(Vertices.size());
    uint32 IndexCount = static_cast<UINT>(Indices.size());

    D3D11_MAPPED_SUBRESOURCE Mapped = {};
    if (SUCCEEDED(Context->Map(Geometry->VertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
    {
        std::memcpy(Mapped.pData, Vertices.data(), sizeof(FVertexFont) * VertexCount);
        Context->Unmap(Geometry->VertexBuffer.Get(), 0);
    }

    Mapped = {};
    if (SUCCEEDED(Context->Map(Geometry->IndexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
    {
        std::memcpy(Mapped.pData, Indices.data(), sizeof(uint32) * IndexCount);
        Context->Unmap(Geometry->IndexBuffer.Get(), 0);
    }

    Geometry->VertexCount = VertexCount;
    Geometry->IndexCount = IndexCount;
}

bool TextRenderObject::Render(ID3D11DeviceContext* Context)
{
    if (!Context || !Material || !Geometry || !FontAtlas)
        return false;

    if (Vertices.empty() || Indices.empty())
        return false;

    if (MaxVertices < 4)
        return false;

    if ((Vertices.size() % 4) != 0)
        return false;

    const size_t expectedIndexCount = (Vertices.size() / 4) * 6;
    if (Indices.size() < expectedIndexCount)
        return false;

    ID3D11ShaderResourceView* srv = FontAtlas->GetSRV();
    ID3D11SamplerState* sampler = FontAtlas->GetSampler();
    if (!srv || !sampler)
        return false;

    Geometry->BindBuffers(Context);
    Material->BindShaders(Context);
    Context->IASetPrimitiveTopology(PrimitiveTopology);
    Context->PSSetShaderResources(0, 1, &srv);
    Context->PSSetSamplers(0, 1, &sampler);

    const UINT totalVertices = static_cast<UINT>(Vertices.size());
    const UINT safeMaxVertices = (MaxVertices / 4) * 4;
    if (safeMaxVertices == 0)
        return false;

    std::vector<uint32_t> rebasedIndices;
    bool drewAny = false;

    UINT chunkStart = 0;
    while (chunkStart < totalVertices)
    {
        const UINT remaining = totalVertices - chunkStart;
        const UINT chunkV = (std::min)(safeMaxVertices, remaining);
        const UINT chunkI = (chunkV / 4) * 6;
        const UINT indexStart = (chunkStart / 4) * 6;

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = Context->Map(
            Geometry->VertexBuffer.Get(),
            0,
            D3D11_MAP_WRITE_DISCARD,
            0,
            &mapped
        );
        if (FAILED(hr))
            return false;

        std::memcpy(
            mapped.pData,
            Vertices.data() + chunkStart,
            sizeof(FVertexFont) * chunkV
        );
        Context->Unmap(Geometry->VertexBuffer.Get(), 0);

        rebasedIndices.resize(chunkI);
        for (UINT i = 0; i < chunkI; ++i)
            rebasedIndices[i] = Indices[indexStart + i] - chunkStart;

        mapped = {};
        hr = Context->Map(
            Geometry->IndexBuffer.Get(),
            0,
            D3D11_MAP_WRITE_DISCARD,
            0,
            &mapped
        );
        if (FAILED(hr))
            return false;

        std::memcpy(
            mapped.pData,
            rebasedIndices.data(),
            sizeof(uint32_t) * chunkI
        );
        Context->Unmap(Geometry->IndexBuffer.Get(), 0);

        Context->DrawIndexed(chunkI, 0, 0);
        drewAny = true;

        chunkStart += chunkV;
        UEngineStatics::TotalDrawCalls++;
    }

    return drewAny;
}

void TextRenderObject::BuildFromEntries(
    const TArray<FTextEntry>& Entries)
{
    Vertices.clear();
    Indices.clear();

    for (const FTextEntry& Entry : Entries)
    {
        if (!Entry.bVisible) continue;

        AppendText(Entry);
    }
}

void TextRenderObject::AppendText(const FTextEntry& Entry)
{
    if (Entry.Text.empty() || !FontAtlas)
        return;

    UCameraComponent* Camera = UCameraComponent::GetMainCamera();
    if (Camera == nullptr) return;

    const FGlyphMap& GlyphMap = FontAtlas->GetGlyphMap();
    const float FontLineHeightPx = GlyphMap.Ascent - GlyphMap.Descent;
    if (FontLineHeightPx <= MathHelper::Epsilon)
        return;


    FMatrix EntryRotation = Entry.Rotation.ToMatrix();
    FVector Right;
    FVector Up;

    // Biilboard
    if (Entry.bUUID)
    {
        Right = Camera->GetRelativeRotation().ToMatrix().TransformVector(FVector::Right());
		Up = Camera->GetRelativeRotation().ToMatrix().TransformVector(FVector::Up());
    }
    else
    {
		Right = EntryRotation.TransformVector(FVector::Right());
		Up = EntryRotation.TransformVector(FVector::Up());
    }

    const float WidthScale = (MathHelper::Abs(Entry.Scale.y) > MathHelper::Epsilon) ? Entry.Scale.y : 1.0f;
    const float HeightScale = (MathHelper::Abs(Entry.Scale.z) > MathHelper::Epsilon) ? Entry.Scale.z : 1.0f;

    const float LineHeight = Entry.Height * HeightScale;

    const float PixelToWorldY = LineHeight / FontLineHeightPx;
    const float PixelToWorldX = PixelToWorldY * WidthScale;

    TArray<uint32> codepoints = DecodeUTF8(Entry.Text);

    TArray<float> TotalWidth = { 0.0f };
    for (uint32 cp : codepoints)
    {
        if (cp == '\n')
            TotalWidth.push_back(0.0f);

        auto It = GlyphMap.Regions.find(cp);
        if (It == GlyphMap.Regions.end())
        {
            TotalWidth[TotalWidth.size() - 1] += LineHeight * 0.5f;
            continue;
        }

        const FAtlasRegion& Region = It->second;
        TotalWidth[TotalWidth.size() - 1] += Region.advanceX * PixelToWorldX;
    }

    const FVector BaseOrigin = Entry.Position;

    float CursorX = 0.0f;
    float CursorY = 0.0f;
    uint32 LineNum = 0;

    TArray<FVector> entryVertices;
    TArray<uint32> entryIndices;

    for (uint32 cp : codepoints)
    {
        if (cp == '\n')
        {
            CursorX = 0.0f;
            CursorY += LineHeight;
            ++LineNum;
            continue;
        }

        auto It = GlyphMap.Regions.find(cp);
        if (It == GlyphMap.Regions.end())
        {
            CursorX += LineHeight * 0.5f;
            continue;
        }

        const FAtlasRegion& Region = It->second;

        const float Advance = Region.advanceX * PixelToWorldX;

        if (Region.width > 0 && Region.height > 0)
        {
            const float GlyphWidth = Region.width * PixelToWorldX;
            const float GlyphHeight = Region.height * PixelToWorldY;
            const float BearingX = Region.bearingX * PixelToWorldX;
            const float BearingY = Region.bearingY * PixelToWorldY;

            const float U0 = (static_cast<float>(Region.x) + 0.5f) / static_cast<float>(GlyphMap.AtlasWidth);
            const float V0 = (static_cast<float>(Region.y) + 0.5f) / static_cast<float>(GlyphMap.AtlasHeight);
            const float U1 = (static_cast<float>(Region.x + Region.width) - 0.5f) / static_cast<float>(GlyphMap.AtlasWidth);
            const float V1 = (static_cast<float>(Region.y + Region.height) - 0.5f) / static_cast<float>(GlyphMap.AtlasHeight);

            float rOffset = CursorX + BearingX + GlyphWidth * 0.5f - TotalWidth[LineNum] * 0.5f;
            float uOffset = CursorY - BearingY - GlyphHeight * 0.5f;

            AppendGlyphQuad(BaseOrigin, Right, Up, GlyphWidth, GlyphHeight, rOffset, uOffset, U0, V0, U1, V1, entryVertices, entryIndices);
        }

        CursorX += Advance;
    }



    if (Entry.TextComponent)
    {
        Entry.TextComponent->SetVertices(entryVertices, entryIndices);
    }
}

void TextRenderObject::AppendGlyphQuad(
    const FVector& Anchor, const FVector& Right, const FVector& Up,
    float Width, float Height, float rOffset, float uOffset,
    float U0, float V0, float U1, float V1,
    TArray<FVector>& outVertices, TArray<uint32>& outIndices)
{
    const float hw = Width * 0.5f;
    const float hh = Height * 0.5f;
    UINT base = static_cast<UINT>(Vertices.size());
	UINT baseOut = static_cast<UINT>(outVertices.size());

    auto Corner = [&](float r, float u) -> FVector {
        return Anchor + Right * r + Up * u;
    };

    FVector C0 = Corner(rOffset - hw, uOffset - hh);
    FVector C1 = Corner(rOffset + hw, uOffset - hh);
    FVector C2 = Corner(rOffset + hw, uOffset + hh);
    FVector C3 = Corner(rOffset - hw, uOffset + hh);

    Vertices.emplace_back(C0.x, C0.y, C0.z, 0.f, 0.f, U0, V1);
    Vertices.emplace_back(C1.x, C1.y, C1.z, 0.f, 0.f, U1, V1);
    Vertices.emplace_back(C2.x, C2.y, C2.z, 0.f, 0.f, U1, V0);
    Vertices.emplace_back(C3.x, C3.y, C3.z, 0.f, 0.f, U0, V0);

    Indices.push_back(base + 0); Indices.push_back(base + 1); Indices.push_back(base + 2);
    Indices.push_back(base + 0); Indices.push_back(base + 2); Indices.push_back(base + 3);

	outVertices.push_back(C0);
	outVertices.push_back(C1);
	outVertices.push_back(C2);
	outVertices.push_back(C3);

	outIndices.push_back(baseOut + 0); outIndices.push_back(baseOut + 1); outIndices.push_back(baseOut + 2);
	outIndices.push_back(baseOut + 0); outIndices.push_back(baseOut + 2); outIndices.push_back(baseOut + 3);
}
