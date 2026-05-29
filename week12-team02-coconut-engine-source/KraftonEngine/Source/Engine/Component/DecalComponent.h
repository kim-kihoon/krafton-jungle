#pragma once
#include "PrimitiveComponent.h"
#include "Core/ResourceTypes.h"
#include "Collision/ConvexVolume.h"
#include "DecalComponent.generated.h"

class UStaticMeshComponent;

// class DecalProxy;

UCLASS()
class UDecalComponent : public UPrimitiveComponent
{
public:
	GENERATED_BODY(UDecalComponent)

	UDecalComponent() = default;
	~UDecalComponent() override = default;

	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

	FPrimitiveSceneProxy* CreateSceneProxy() override;

	// Property Editor 지원
	void PostEditProperty(const char* PropertyName) override;
	void PostDuplicate() override;

	// Color (with Color)
	void SetColor(FVector4 InColor)
	{
		Color = InColor;
		MarkProxyDirty(EDirtyFlag::Material);
	}
	FVector4 GetColor() const;

	// --- Material ---
	void SetMaterial(class UMaterial* InMaterial);
	class UMaterial* GetMaterial() const { return Material; }

	const FConvexVolume GetDecalVolume() { return ConvexVolume; }
	void UpdateDecalVolumeFromTransform();
	void OnTransformDirty() override;

	const TArray<UStaticMeshComponent*>& GetReceivers() const { return Receivers; }

	class UBillboardComponent* EnsureEditorBillboard();

protected:
	virtual bool ShouldReceivePrimitive(UPrimitiveComponent* PrimitiveComp) const;

private:
	void HandleFade(float DeltaTime);
	void UpdateReceivers();

private:
	FConvexVolume ConvexVolume;
	TArray<UStaticMeshComponent*> Receivers;
	UPROPERTY(Edit, Category="Rendering", DisplayName="Material", Type=MaterialSlot)
	FMaterialSlot MaterialSlot;
	UMaterial* Material = nullptr;
	UPROPERTY(Edit, Category="Rendering", DisplayName="Color", Type=Vec4)
	FVector4 Color = {1,1,1,1};
	UPROPERTY(Edit, Category="Rendering", Min=0.0f, Max=0.0f, Speed=0.1f)
	float FadeInDelay = 0;
	UPROPERTY(Edit, Category="Rendering", Min=0.0f, Max=0.0f, Speed=0.1f)
	float FadeInDuration = 0;
	UPROPERTY(Edit, Category="Rendering", Min=0.0f, Max=0.0f, Speed=0.1f)
	float FadeOutDelay = 0;
	UPROPERTY(Edit, Category="Rendering", Min=0.0f, Max=0.0f, Speed=0.1f)
	float FadeOutDuration = 0;
	float FadeTimer = 0;
	float FadeOpacity = 1.0f;		// 페이드 효과 사용 시 Color.A에 곱함
};
