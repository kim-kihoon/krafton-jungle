#pragma once

#include "Component/PrimitiveComponent.h"

#include <d3d11.h>

#include "Source/Engine/Component/Primitive/PhotoPolaroidComponent.generated.h"

class FPrimitiveSceneProxy;

UCLASS()
class UPhotoPolaroidComponent : public UPrimitiveComponent
{
public:
	GENERATED_BODY()

	FPrimitiveSceneProxy* CreateSceneProxy() override;
	void UpdateWorldAABB() const override;
	bool SupportsOutline() const override { return false; }

	void SetTextures(ID3D11ShaderResourceView* InPhotoSRV, ID3D11ShaderResourceView* InFrameSRV);
	void SetDisplayTime(float InDisplayTime);
	void SetDevelopTime(float InDevelopTime);

	ID3D11ShaderResourceView* GetPhotoSRV() const { return PhotoSRV; }
	ID3D11ShaderResourceView* GetFrameSRV() const { return FrameSRV; }
	float GetDisplayTime() const { return DisplayTime; }
	float GetDevelopTime() const { return DevelopTime; }

private:
	ID3D11ShaderResourceView* PhotoSRV = nullptr;
	ID3D11ShaderResourceView* FrameSRV = nullptr;
	float DisplayTime = 0.0f;
	float DevelopTime = 0.0f;
};
