#pragma once
#include "TextureAtlas.h"
#include "TextTypes.h"

struct FFontAtlas : public FTextureAtlas
{
    virtual void EnsureTextUTF8(const FString& text) {}
    const FGlyphMap& GetGlyphMap() const { return GlyphMap; }

    ID3D11DeviceContext* Context = nullptr;
    TComPtr<ID3D11Texture2D> AtlasTexture;
    FGlyphMap GlyphMap;
};
