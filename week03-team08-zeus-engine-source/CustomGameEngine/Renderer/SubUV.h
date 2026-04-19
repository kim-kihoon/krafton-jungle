#pragma once
#include "ConstantBuffer.h"
#include "TextureAtlas.h"

struct FSubUV : public FTextureAtlas
{
    FConstantBuffer* CB = nullptr;
    int Columns = 1;
    int Rows = 1;
    int CurrentFrame = 0;

    void Bind(ID3D11DeviceContext* Context)
    {
        if (!CB || !SRV || !Sampler) throw;
        struct FSubUVConstant { float UVOffset[2]; float UVScale[2]; };
        float uScale = 1.0f / Columns;
        float vScale = 1.0f / Rows;
        float halfU = (TextureWidth  > 0) ? (0.5f / TextureWidth)  : 0.0f;
        float halfV = (TextureHeight > 0) ? (0.5f / TextureHeight) : 0.0f;
        FSubUVConstant data;
        data.UVOffset[0] = (CurrentFrame % Columns) * uScale + halfU;
        data.UVOffset[1] = (CurrentFrame / Columns) * vScale + halfV;
        data.UVScale[0] = uScale - 2.0f * halfU;
        data.UVScale[1] = vScale - 2.0f * halfV;
        CB->Update(Context, &data, sizeof(FSubUVConstant));
        ID3D11ShaderResourceView* RawSRV = SRV.Get();
        Context->PSSetShaderResources(0, 1, &RawSRV);
        Context->PSSetSamplers(0, 1, &Sampler);
    }
};
