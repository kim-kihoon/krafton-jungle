#include "StaticFontAtlas.h"
#include "DirectXTex.h"

static const char* GCharset =
    "!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";

bool FStaticFontAtlas::Initialize(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const wchar_t* pngPath,
    float pixelHeight,
    int imageWidth,
    int imageHeight,
    int cols,
    int rows)
{
    if (!device || !context || !pngPath)
        return false;

    Context = context;

    // PNG 로드
    DirectX::TexMetadata metadata = {};
    DirectX::ScratchImage image;
    HRESULT hr = DirectX::LoadFromWICFile(pngPath, DirectX::WIC_FLAGS_NONE, &metadata, image);
    if (FAILED(hr))
        return false;

    // SRV 생성
    ID3D11ShaderResourceView* rawSRV = nullptr;
    hr = DirectX::CreateShaderResourceView(device, image.GetImages(), image.GetImageCount(), metadata, &rawSRV);
    if (FAILED(hr))
        return false;

    SRV = rawSRV;
    rawSRV->Release();

    // 내부 텍스처 참조 보관
    {
        ID3D11Resource* res = nullptr;
        SRV->GetResource(&res);
        if (res)
        {
            res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(AtlasTexture.GetAddressOf()));
            res->Release();
        }
    }

    // 실제 이미지 크기 사용
    const int AtlasW = (metadata.width  > 0) ? static_cast<int>(metadata.width)  : imageWidth;
    const int AtlasH = (metadata.height > 0) ? static_cast<int>(metadata.height) : imageHeight;

    TextureWidth  = AtlasW;
    TextureHeight = AtlasH;

    GlyphMap.AtlasWidth  = AtlasW;
    GlyphMap.AtlasHeight = AtlasH;

    // 셀 크기 계산
    const int CellW = AtlasW / cols;
    const int CellH = AtlasH / rows;

    if (pixelHeight <= 0.0f)
        pixelHeight = static_cast<float>(CellH);

    GlyphMap.Ascent  = pixelHeight;
    GlyphMap.Descent = 0.0f;
    GlyphMap.LineGap = 0.0f;

    TArray<uint32> codepoints = DecodeUTF8(FString(GCharset));
    int i = 0;
    for (uint32 codepoint : codepoints)
    {
        const int col = i % cols;
        const int row = i / cols;

        FAtlasRegion region;
        region.x        = col * CellW;
        region.y        = row * CellH;
        region.width    = CellW;
        region.height   = CellH;
        region.advanceX = static_cast<float>(CellW);
        region.bearingX = 0.0f;
        region.bearingY = -static_cast<float>(CellH);

        GlyphMap.Regions[codepoint] = region;
        ++i;
    }

    return true;
}
