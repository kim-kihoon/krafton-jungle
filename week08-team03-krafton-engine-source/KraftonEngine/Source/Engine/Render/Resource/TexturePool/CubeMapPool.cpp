//#include "CubeMapPool.h"
//
//template<typename T>
//using TComPtr = Microsoft::WRL::ComPtr<T>;
//using TexturePoolHandle = FTexturePoolBase::TexturePoolHandle;
//using TexturePoolHandleSet = FTexturePoolBase::TexturePoolHandleSet;
//
//TexturePoolHandleSet* FCubeMapPool::GetTextureHandle(TexturePoolHandleRequest HandleRequest)
//{
//	return nullptr;
//}
//
//void FCubeMapPool::ReleaseHandle(TexturePoolHandle& InHandle)
//{
//}
//
//TComPtr<ID3D11Texture2D> FCubeMapPool::CreateTexture(ID3D11Device* Device)
//{
//	D3D11_TEXTURE2D_DESC desc = {};
//	desc.Width = TextureSize;
//	desc.Height = TextureSize;
//	desc.MipLevels = 1;
//	desc.ArraySize = TextureLayerSize * 6;
//	desc.Format = DXGI_FORMAT_R32_TYPELESS;
//	desc.SampleDesc.Count = 1;
//	desc.Usage = D3D11_USAGE_DEFAULT;
//	desc.MiscFlags = 0;
//	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL;
//	desc.CPUAccessFlags = 0;
//
//	TComPtr<ID3D11Texture2D> NewTexture;
//	HRESULT hr = Device->CreateTexture2D(&desc, nullptr, NewTexture.GetAddressOf());
//	assert(SUCCEEDED(hr));
//
//	return NewTexture;
//}
//
//void FCubeMapPool::RebuildSRV(ID3D11Device* Device, ID3D11Texture2D* InTexture)
//{
//}
//
//void FCubeMapPool::OnSetTextureSize()
//{
//}
//
//void FCubeMapPool::OnSetTextureLayerSize()
//{
//}
