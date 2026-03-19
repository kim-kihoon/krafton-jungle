#pragma once
#include "Defines.h"
#include "SceneComponent.h"

class UMesh;

class UPrimitiveComponent : public USceneComponent
{
	DECLARE_CLASS(UPrimitiveComponent, USceneComponent)

public:
	UPrimitiveComponent() = default;

public:
	const UMesh* GetMesh() const { return MeshData; }
	void  SetMesh(UMesh* _MeshData) { MeshData = _MeshData; }

	const FVector4& GetColor() const { return Color; }
	void SetColor(const FVector4& InColor) { Color = InColor; }
	bool  IsDebugMode() { return IsDebugUse; };
	void  SetDebugMode(bool isDebug) { IsDebugUse = isDebug; }
	// 항상 화면 위(최상단)에 렌더링할지 여부
	bool IsAlwaysOnTop() const { return bAlwaysOnTop; }
	void SetAlwaysOnTop(bool bFlag) { bAlwaysOnTop = bFlag; }

	FString GetType() const { return Type; }


public:
	void TickComponent(float DeltaTime) override;
	virtual void Render(ID3D11DeviceContext& DeviceContext) override = 0;
	void Release() override;

protected:
	bool IsDebugUse = { false };

	FVector4 Color = { 1.0f, 1.0f, 1.0f, 0.0f };

	UMesh* MeshData = nullptr;
	
	bool bAlwaysOnTop = { false };

	FString Type;
};
