#include "Mesh/Skeleton.h"

#include "Object/ObjectFactory.h"
#include "Serialization/Archive.h"


void USkeleton::Serialize(FArchive& Ar)
{
	if (Ar.IsLoading() && !SkeletonAsset)
	{
		SkeletonAsset = new FSkeletonAsset();
	}

	SkeletonAsset->Serialize(Ar);
}

void USkeleton::SetSkeletonAsset(FSkeletonAsset* InAsset)
{
	SkeletonAsset = InAsset;
}

FSkeletonAsset* USkeleton::GetSkeletonAsset() const
{
	return SkeletonAsset;
}
