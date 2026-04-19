#pragma once
#include "FontAtlas.h"

struct FStaticFontAtlas : public FFontAtlas
{
    bool Initialize(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const wchar_t* pngPath,
        float pixelHeight,
        int imageWidth = 512,
        int imageHeight = 512,
        int cols = 16,
        int rows = 6);

    virtual void EnsureTextUTF8(const FString& text) override {}
};
