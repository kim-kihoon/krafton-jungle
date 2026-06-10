#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"

class FConstantBuffer;
class UDecalComponent;
struct ID3D11Device;
struct ID3D11DeviceContext;

// ============================================================
// FDecalSceneProxy — UDecalComponent 전용 프록시
// ============================================================
// 월드에 투영되는 데칼 정보를 관리한다.
// OBB(Oriented Bounding Box)를 기반으로 중첩되는 프리미티브에 텍스처를 투영
class FDecalSceneProxy : public FPrimitiveSceneProxy
{
public:
	FDecalSceneProxy(UDecalComponent* InComponent);
	~FDecalSceneProxy() override;

	void UpdateMaterial() override;
	void UpdateMesh() override;
	void AddReferencedObjects(FReferenceCollector& Collector) override;

	// Collector가 Receiver 프록시 목록에 접근 (Owner 역참조 없이)
	const TArray<FPrimitiveSceneProxy*>& GetReceiverProxies() const { return CachedReceiverProxies; }
	FConstantBuffer* GetDecalConstantBuffer() const { return DecalCB; }
	void UploadDecalConstantBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context) const;

    void RemoveReceiverProxy(FPrimitiveSceneProxy* ReceiverProxy);
    void InvalidateReceiverCache();

private:
	struct FDecalConstants
	{
		FMatrix WorldToDecal;
		FVector4 Color;
	};

	UDecalComponent* GetDecalComponent() const;
	void RebuildReceiverProxies();

	FConstantBuffer* DecalCB;
	FDecalConstants DecalCBData = {};
	class UMaterial* DecalMaterial = nullptr;      // 컴포넌트의 원본 Material (공유 가능)
	class UMaterial* DecalProxyMaterial = nullptr; // 원본 Material이 없을 때만 쓰는 fallback transient Material
	TArray<FPrimitiveSceneProxy*> CachedReceiverProxies;
};
