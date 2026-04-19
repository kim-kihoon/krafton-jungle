#pragma once
#include "EngineTypes.h"
#include "TextTypes.h"
#include "ImGui/imstb_truetype.h"
#include "FontAtlas.h"

struct FDynamicFontAtlas : public FFontAtlas
{
    bool Initialize(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const wchar_t* ttfPath,
        float pixelHeight,
        int atlasWidth = 1024,
        int atlasHeight = 1024);

    bool EnsureGlyph(uint32 codepoint);
    virtual void EnsureTextUTF8(const FString& text) override;

    bool RasterizeGlyph(uint32 codepoint, FAtlasRegion& outRegion, TArray<uint8_t>& outBitmap, int& outW, int& outH);
    bool AllocateRect(int w, int h, int& outX, int& outY);
    void UploadRect(int x, int y, int w, int h);

    stbtt_fontinfo FontInfo = {};
    TArray<unsigned char> FontFileData;
    TArray<uint8_t> AtlasPixels;

    float Scale = 1.0f;
    int AtlasWidth = 1024;
    int AtlasHeight = 1024;
    bool bInitialized = false;

    int PenX = 1;
    int PenY = 1;
    int ShelfHeight = 0;
    int Padding = 1;
};
