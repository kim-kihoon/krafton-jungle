#pragma once
#include "EngineTypes.h"
#include <d3d11.h>

struct FTextureAtlas
{
	int TextureWidth = 0;
	int TextureHeight = 0;

	TComPtr<ID3D11ShaderResourceView> SRV;
	ID3D11SamplerState* Sampler = nullptr;

	ID3D11ShaderResourceView* GetSRV() const { return SRV.Get(); }
	ID3D11SamplerState* GetSampler() const { return Sampler; }
};
