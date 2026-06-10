#pragma once
#include "Component/PrimitiveComponent.h"
#include "Core/Types/ResourceTypes.h"
#include "Collision/Math/ConvexVolume.h"
#include "Object/Ptr/SoftObjectPtr.h"
#include "Object/Ptr/WeakObjectPtr.h"

class UStaticMeshComponent;

// class DecalProxy;

#include "Source/Engine/Component/Primitive/DecalComponent.generated.h"

UCLASS()
class UDecalComponent : public UPrimitiveComponent
{
public:
	GENERATED_BODY()
	UDecalComponent() = default;
	~UDecalComponent() override = default;

	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

	FPrimitiveSceneProxy* CreateSceneProxy() override;

	// Property Editor 지원
	void PostEditProperty(const char* PropertyName) override;
	
	void PostDuplicate() override;

	// Color (with Color)
	UFUNCTION(Callable, Category="Decal|Rendering")
	void SetColor(FVector4 InColor)
	{
		Color = InColor;
		MarkProxyDirty(EDirtyFlag::Material);
	}
	UFUNCTION(Pure, Category="Decal|Rendering")
	FVector4 GetColor() const;

	// --- Material ---
	UFUNCTION(Callable, Category="Materials")
	void SetMaterial(class UMaterial* InMaterial);
	UFUNCTION(Pure, Category="Materials")
	class UMaterial* GetMaterial() const { return Material; }
	void AddReferencedObjects(FReferenceCollector& Collector) override;

	const FConvexVolume GetDecalVolume() { return ConvexVolume; }
	void UpdateDecalVolumeFromTransform();
	void OnTransformDirty() override;

	UFUNCTION(Pure, Category="Decal")
	TArray<UStaticMeshComponent*> GetReceivers() const;

	class UBillboardComponent* EnsureEditorBillboard();

protected:
	virtual bool ShouldReceivePrimitive(UPrimitiveComponent* PrimitiveComp) const;

private:
	void HandleFade(float DeltaTime);
	void UpdateReceivers();

private:
	FConvexVolume ConvexVolume;
	TArray<TWeakObjectPtr<UStaticMeshComponent>> Receivers;
	UPROPERTY(Edit, Save, Category="Rendering", DisplayName="Material", AssetType="Material")
	FSoftObjectPtr MaterialSlot;
	UMaterial* Material = nullptr;
	UPROPERTY(Edit, Save, Category="Rendering", DisplayName="Color", Type=Vec4)
	FVector4 Color = {1,1,1,1};
	UPROPERTY(Edit, Save, Category="Rendering", DisplayName="FadeInDelay")
	float FadeInDelay = 0;
	UPROPERTY(Edit, Save, Category="Rendering", DisplayName="FadeInDuration")
	float FadeInDuration = 0;
	UPROPERTY(Edit, Save, Category="Rendering", DisplayName="FadeOutDelay")
	float FadeOutDelay = 0;
	UPROPERTY(Edit, Save, Category="Rendering", DisplayName="FadeOutDuration")
	float FadeOutDuration = 0;
	float FadeTimer = 0;
	float FadeOpacity = 1.0f;		// 페이드 효과 사용 시 Color.A에 곱함
};
