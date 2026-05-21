#pragma once

#include "Core/CoreMinimal.h"

class FResourceManager;
class USkeletonAsset;
struct FSkeleton;

class FSkeletonLoadService
{
public:
    explicit FSkeletonLoadService(FResourceManager& InResourceManager);

    USkeletonAsset* Load(const FString& Path);

private:
    USkeletonAsset* FinalizeLoadedSkeleton(FSkeleton* SkeletonData, const FString& CacheKey);

    FResourceManager& ResourceManager;
};
