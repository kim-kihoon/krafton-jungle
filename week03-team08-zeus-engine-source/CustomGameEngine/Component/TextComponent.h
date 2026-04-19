#pragma once
#include "PrimitiveComponent.h"

class UTextComponent : public UPrimitiveComponent
{
	DECLARE_OBJECT(UTextComponent, UPrimitiveComponent)
public:
	UTextComponent();
	virtual ~UTextComponent() override;

	void CreateRenderObjects() override {}
	void UpdateRenderObjects() override;

	void DrawProperties() override;

	const FString& GetText() const { return Text; }
	void SetText(const FString& InText) { Text = InText; MarkRenderStateDirty(); }

	void SetVertices(TArray<FVector>& vertices, TArray<uint32>& indices); // Picking을 위한 함수

	virtual json::JSON Serialize() override;
	virtual void Deserialize(json::JSON) override;

private:
	FString Text;
};

