#pragma once
#include "Math/Rotator.h"
#include "Math/Color.h"

class UTextComponent;

struct FTextEntry
{
    FString Text;
    FVector Position;
	FVector Scale;
	FRotator Rotation;
    FColor Color = FColor(1, 1, 1, 1);
    float Height = 0.35f;
    bool bUUID = false;
    bool bVisible = true;
    UTextComponent* TextComponent = nullptr;
};

struct FAtlasRegion
{
    int32 x = 0;
    int32 y = 0;
    int32 width = 0;
    int32 height = 0;
    float advanceX = 0.0f;
	float bearingX = 0.0f;
	float bearingY = 0.0f;
};

struct FGlyphMap
{
    TMap<uint32, FAtlasRegion> Regions;
    int32 AtlasWidth = 0;
    int32 AtlasHeight = 0;

    float Ascent = 0.0f;
    float Descent = 0.0f;
    float LineGap = 0.0f;
};


static TArray<uint32> DecodeUTF8(const FString& text)
{
    TArray<uint32> out;
    const unsigned char* s = reinterpret_cast<const unsigned char*>(text.data());
    size_t i = 0;
    const size_t n = text.size();

    while (i < n)
    {
        uint32 cp = 0;
        unsigned char c = s[i];

        if (c < 0x80)
        {
            cp = c;
            i += 1;
        }
        else if ((c >> 5) == 0x6 && i + 1 < n)
        {
            cp = ((c & 0x1F) << 6) |
                (s[i + 1] & 0x3F);
            i += 2;
        }
        else if ((c >> 4) == 0xE && i + 2 < n)
        {
            cp = ((c & 0x0F) << 12) |
                ((s[i + 1] & 0x3F) << 6) |
                (s[i + 2] & 0x3F);
            i += 3;
        }
        else if ((c >> 3) == 0x1E && i + 3 < n)
        {
            cp = ((c & 0x07) << 18) |
                ((s[i + 1] & 0x3F) << 12) |
                ((s[i + 2] & 0x3F) << 6) |
                (s[i + 3] & 0x3F);
            i += 4;
        }
        else
        {
            cp = '?';
            i += 1;
        }

        out.push_back(cp);
    }

    return out;
}