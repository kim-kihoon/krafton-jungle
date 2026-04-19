#pragma once

#include "Engine/Component/PrimitiveComponent.h"
#include "Render/Proxy/ArrowSceneProxy.h"

class UArrowComponent : public UPrimitiveComponent
{
public:
	DECLARE_CLASS(UArrowComponent, UPrimitiveComponent)

	UArrowComponent();
	~UArrowComponent() override = default;

	FMeshBuffer* GetMeshBuffer() const override;
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	void SetSelected(bool bNewSelected) override;

	void Serialize(FArchive& Ar) override;
	void PostDuplicate() override;

	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
};

