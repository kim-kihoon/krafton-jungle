#pragma once

#include "Core/CoreTypes.h"
#include "Core/Singleton.h"

class USkeleton;

class FSkeletonManager : public TSingleton<FSkeletonManager>
{
	friend class TSingleton<FSkeletonManager>;

public:
	USkeleton* Load(const FString& Path);
	USkeleton* Find(const FString& Path) const;
	void RegisterSkeleton(const FString& Path, USkeleton* Skeleton);
	bool IsSkeletonPackage(const FString& Path) const;

private:
	TMap<FString, USkeleton*> SkeletonCache;
};
