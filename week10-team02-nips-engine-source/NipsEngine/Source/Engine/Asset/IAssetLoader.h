#pragma once

#include "Core/CoreMinimal.h"

class IAssetLoader
{
public:
	virtual ~IAssetLoader() = default;

    // 어떤 확장자를 지원하는지 반환하는 함수. 대문자 소문자 이런 것도 비교해야 함.
	virtual bool SupportsExtension(const FString& Extesion) const = 0;
};
