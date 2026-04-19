#include "DynamicFontAtlas.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "Imgui/imstb_truetype.h"
#include <fstream>
#include <cstring>
#include "EngineTypes.h"
#include "TextTypes.h"
#include "DirectXTex.h"

static bool LoadBinaryFile(const wchar_t* path, TArray<unsigned char>& outData)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file)
		return false;

	std::streamsize size = file.tellg();
	if (size <= 0)
		return false;

	file.seekg(0, std::ios::beg);

	outData.resize(static_cast<size_t>(size));
	if (!file.read(reinterpret_cast<char*>(outData.data()), size))
		return false;

	return true;
}

// FDynamicFontAtlas에서 직접 생성을 담당해야 할지 고민해보기
bool FDynamicFontAtlas::Initialize(
    ID3D11Device* device,
	ID3D11DeviceContext* context,
    const wchar_t* ttfPath,
    float pixelHeight,
    int atlasWidth,
    int atlasHeight)
{
	bInitialized = false;
    FontFileData.clear();
    GlyphMap.Regions.clear();

    if (!device || !context || !ttfPath)
        return false;

	Context = context;

    if (!LoadBinaryFile(ttfPath, FontFileData))
        return false;

    if (FontFileData.empty())
        return false;

    int fontOffset = stbtt_GetFontOffsetForIndex(FontFileData.data(), 0);
    if (fontOffset < 0)
        return false;

    memset(&FontInfo, 0, sizeof(FontInfo));
    if (!stbtt_InitFont(&FontInfo, FontFileData.data(), fontOffset))
        return false;

    pixelHeight = (pixelHeight > 0.0f) ? pixelHeight : 16.0f;
    Scale = stbtt_ScaleForPixelHeight(&FontInfo, pixelHeight);
    if (Scale <= 0.0f)
        return false;

	int ascent = 0;
	int descent = 0;
	int lineGap = 0;
	stbtt_GetFontVMetrics(&FontInfo, &ascent, &descent, &lineGap);

	GlyphMap.Ascent = ascent * Scale;
	GlyphMap.Descent = descent * Scale;
	GlyphMap.LineGap = lineGap * Scale;

    AtlasWidth = atlasWidth;
    AtlasHeight = atlasHeight;

    AtlasPixels.assign(static_cast<size_t>(AtlasWidth * AtlasHeight), 0);

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = AtlasWidth;
    desc.Height = AtlasHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = AtlasPixels.data();
    initData.SysMemPitch = AtlasWidth;

    HRESULT hr = device->CreateTexture2D(&desc, &initData, AtlasTexture.GetAddressOf());
    if (FAILED(hr))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = device->CreateShaderResourceView(AtlasTexture.Get(), &srvDesc, SRV.GetAddressOf());
    if (FAILED(hr))
        return false;

    GlyphMap.AtlasWidth = AtlasWidth;
    GlyphMap.AtlasHeight = AtlasHeight;
	bInitialized = true;

    return true;
}

bool FDynamicFontAtlas::EnsureGlyph(uint32 codepoint)
{
	if (GlyphMap.Regions.find(codepoint) != GlyphMap.Regions.end())
		return true;

	FAtlasRegion region = {};
	if (!RasterizeGlyph(codepoint, region, AtlasPixels, AtlasWidth, AtlasHeight))
		return false;

	GlyphMap.Regions[codepoint] = region;
	UploadRect(region.x, region.y, region.width, region.height);
	return true;
}

void FDynamicFontAtlas::EnsureTextUTF8(const FString& text)
{
    TArray<uint32> codepoints = DecodeUTF8(text);

    for (uint32 cp : codepoints)
	{
		if (cp == '\n')
			continue;
		EnsureGlyph(static_cast<uint32>(cp));
	}
}

bool FDynamicFontAtlas::RasterizeGlyph(uint32 codepoint, FAtlasRegion& outRegion, TArray<uint8_t>& outBitmap, int& outW, int& outH)
{
	int w = 0, h = 0, xoff = 0, yoff = 0;
	unsigned char* bitmap = stbtt_GetCodepointBitmap(&FontInfo, Scale, Scale, (int)codepoint, &w, &h, &xoff, &yoff);
	if (!bitmap)
		return false;

	if (!AllocateRect(w, h, outRegion.x, outRegion.y))
	{
		stbtt_FreeBitmap(bitmap, nullptr);
		return false;
	}

	outRegion.width = w;
	outRegion.height = h;

	int advance = 0;
	int lsb = 0;
	stbtt_GetCodepointHMetrics(&FontInfo, (int)codepoint, &advance, &lsb);

	outRegion.advanceX = advance * Scale;
	outRegion.bearingX = (float)xoff;
	outRegion.bearingY = (float)yoff;

	for (int row = 0; row < h; ++row)
	{
		for (int col = 0; col < w; ++col)
		{
			outBitmap[(outRegion.y + row) * AtlasWidth + outRegion.x + col] = bitmap[row * w + col];
		}
	}

	stbtt_FreeBitmap(bitmap, nullptr);
	return true;
}

bool FDynamicFontAtlas::AllocateRect(int w, int h, int& outX, int& outY)
{
	const int Padding = 1;

	w += Padding * 2;
	h += Padding * 2;

	if (PenX + w > AtlasWidth)
	{
		PenX = 0;
		PenY += ShelfHeight;
		ShelfHeight = 0;
	}

	if (PenY + h > AtlasHeight)
		return false;

	outX = PenX + Padding;
	outY = PenY + Padding;

	PenX += w;
	ShelfHeight = (std::max)(ShelfHeight, h);
	return true;
}

void FDynamicFontAtlas::UploadRect(int x, int y, int w, int h)
{
	D3D11_BOX box = {};
	box.left = x;
	box.top = y;
	box.right = x + w;
	box.bottom = y + h;
	box.front = 0;
	box.back = 1;

	Context->UpdateSubresource(
		AtlasTexture.Get(),
		0,
		&box,
		AtlasPixels.data() + y * AtlasWidth + x,
		AtlasWidth,
		0);
}