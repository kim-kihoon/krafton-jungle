#pragma once
#include "CoreMinimal.h"
#include "Object/Object.h"

class FMaterialTexture;

class ENGINE_API UTexture : public UObject
{
public:
	DECLARE_RTTI(UTexture, UObject)

	int32_t GetWidth()  const { return Width; }
	int32_t GetHeight() const { return Height; }

	void SetResource(FMaterialTexture* InResource) { Resource = InResource; }
	FMaterialTexture* GetResource() const { return Resource; }

protected:
	int32_t Width = 0;
	int32_t Height = 0;

	/** 참조용으로만 들고 있으므로 메모리 관리 필요x */
	FMaterialTexture* Resource = nullptr;
};