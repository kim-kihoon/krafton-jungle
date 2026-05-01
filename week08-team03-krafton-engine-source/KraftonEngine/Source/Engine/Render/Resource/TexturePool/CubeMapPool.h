//#pragma once
//#include "TexturePool.h"
//
//class FCubeManagerBase // 사용중인 slice 번호와 미사용 번호를 관리
//{
//public:
//	virtual void Initialize(uint32 InTextureSize) {  };
//	virtual bool GetHandle(float TextureSize, uint32& OutHandle) = 0;
//	virtual void ReleaseHandle(uint32 Handle) = 0;
//	virtual void BroadCastEntries() = 0;
//	virtual void SetSize(uint32 InNewTextureSize)
//	{
//		BroadCastEntries();
//	}
//
//private:
//
//
//};
//
//class FCubeMapPool final : public FTexturePoolBase
//{
//	template<typename T>
//	using TComPtr = Microsoft::WRL::ComPtr<T>;
//
//public:
//	virtual TexturePoolHandleSet* GetTextureHandle(TexturePoolHandleRequest HandleRequest) override;
//	virtual void ReleaseHandle(TexturePoolHandle& InHandle) override; //internalIndex => Manager번호, ArrayIndex => Slice번호
//	virtual ID3D11ShaderResourceView* GetDebugSRV(const TexturePoolHandle& InHandle) { return nullptr; }
//
//protected:
//	virtual TComPtr<ID3D11Texture2D> CreateTexture(ID3D11Device* Device) override;
//	virtual void RebuildSRV(ID3D11Device* Device, ID3D11Texture2D* InTexture) override;
//
//	virtual void OnSetTextureSize() override;
//	virtual void OnSetTextureLayerSize() override;
//
//	virtual uint32 GetTextureLayerSize() { return TextureLayerSize; }
//
//private:
//	ID3D11ShaderResourceView* SRV;
//	TArray<ID3D11DepthStencilView*> DSVs;
//	uint32 TextureSize;
//	std::unique_ptr<FCubeManagerBase> CubeManager;
//};
//
