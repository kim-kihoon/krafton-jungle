#include "Animation/AnimationAsset.h"

#include "Mesh/Skeleton.h"
#include "Mesh/SkeletonAsset.h"
#include "Mesh/SkeletonManager.h"
#include "Object/ObjectFactory.h"
#include "Platform/Paths.h"
#include "Serialization/Archive.h"

IMPLEMENT_CLASS(UAnimationAsset, UObject)

void UAnimationAsset::Serialize(FArchive& Ar)
{
	Ar << SkeletonPath;

	if (Ar.IsLoading())
	{
		ResolveSkeleton();
	}
}

void UAnimationAsset::SetSkeleton(USkeleton* InSkeleton)
{
	Skeleton = InSkeleton;
	SkeletonPath = Skeleton ? Skeleton->GetAssetPathFileName() : FString();
}

FSkeletonAsset* UAnimationAsset::GetSkeletonAsset() const
{
	return Skeleton ? Skeleton->GetSkeletonAsset() : nullptr;
}

void UAnimationAsset::SetSkeletonPath(const FString& InSkeletonPath)
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(InSkeletonPath);
	if (SkeletonPath != NormalizedPath)
	{
		Skeleton = nullptr;
	}

	SkeletonPath = NormalizedPath;
}

bool UAnimationAsset::ResolveSkeleton()
{
	if (Skeleton && Skeleton->GetAssetPathFileName() == SkeletonPath)
	{
		return true;
	}

	Skeleton = nullptr;
	if (SkeletonPath.empty())
	{
		return false;
	}

	Skeleton = FSkeletonManager::Get().Load(SkeletonPath);
	return Skeleton != nullptr;
}
