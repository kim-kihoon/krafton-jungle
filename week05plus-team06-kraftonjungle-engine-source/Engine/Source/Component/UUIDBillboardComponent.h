#pragma once

#include "TextRenderComponent.h"


class ENGINE_API UUUIDBillboardComponent : public UTextRenderComponent
{
public:
	DECLARE_RTTI(UUUIDBillboardComponent, UTextRenderComponent)
	GENERATE_SHALLOW_CLONE(UUUIDBillboardComponent);

	void PostConstruct() override;

	virtual FString GetDisplayText() const override;
	// SetWorldOffset 반영해서 오브젝트 머리 위에 뜨도록 함
	virtual FVector GetRenderWorldPosition() const override;
	virtual FVector GetRenderWorldScale() const override;

	const FVector& GetWorldOffset() const { return WorldOffset; }
	void SetWorldOffset(const FVector& InOffset) { WorldOffset = InOffset; }

	virtual FBoxSphereBounds GetWorldBounds() const override;

	void FixupReferences(const FDuplicateionContext& Context) override;

private:
	FVector WorldOffset = FVector(0.0f, 0.0f, 0.3f);
	uint32 LastDisplayUUID = 0;
};
