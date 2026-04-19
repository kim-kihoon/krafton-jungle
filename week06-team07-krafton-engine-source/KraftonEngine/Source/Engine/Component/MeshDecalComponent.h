#pragma once
#include "Component/PrimitiveComponent.h"
#include "Core/PropertyTypes.h"
#include "Materials/Material.h"
#include "Math/Vector.h"
#include "Render/Resource/Buffer.h"
#include "Core/ResourceTypes.h"
class UMeshDecalComponent : public UPrimitiveComponent
{
public:
	DECLARE_CLASS(UMeshDecalComponent, UPrimitiveComponent)

	UMeshDecalComponent();

	virtual FMeshBuffer* GetMeshBuffer() const override { return const_cast<FMeshBuffer*>(&MeshBuffer); }
	virtual const FMeshData* GetMeshData() const override { return nullptr; }
	bool LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult) override;

	void UpdateDecalMeshData();
	void ClipByDecalLocalOBB(TArray<FVertexPNCT>& OutClippedVertices, const FVertexPNCT& P0, const FVertexPNCT& P1, const FVertexPNCT& P2);
	void ClipByAxis(float HExtent, uint32 AxisType, const FVertexPNCT& A, const FVertexPNCT& B, bool Larger, TArray<FVertexPNCT>& OutClippedVertices);
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;
	void PostDuplicate() override;
	void CollectEditorVisualizations(FRenderBus& RenderBus) const override;
	virtual void BeginPlay() override;
	float GetOpacity() const { return Opacity; }
	bool IsFading() const { return bFade; }
	void SetTexture(const FName& InTextureName);
	ID3D11ShaderResourceView* GetSRV();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void Serialize(FArchive& Ar) override;

private:
	FName TextureName = "None";
	FTextureResource* CachedTexture = nullptr;

	TMeshData<FVertexPNCT> DecalMeshData;
	FMeshBuffer MeshBuffer;

	float OpacityRate = 1.f;
	float Opacity = 1.f;
	bool bFade = false;

private:
	TArray<UPrimitiveComponent*> GetCandidates() const;

protected:
	void OnTransformDirty() override;
};
