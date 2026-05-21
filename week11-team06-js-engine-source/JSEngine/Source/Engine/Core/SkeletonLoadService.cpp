#include "Core/SkeletonLoadService.h"

#include "Asset/SkeletonAsset.h"
#include "Core/Logging/Log.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"

#include <algorithm>

FSkeletonLoadService::FSkeletonLoadService(FResourceManager& InResourceManager)
    : ResourceManager(InResourceManager)
{
}

USkeletonAsset* FSkeletonLoadService::Load(const FString& Path)
{
    const FString NormalizedPath = FPaths::Normalize(Path);

    if (USkeletonAsset* FoundSkeleton = ResourceManager.FindSkeleton(NormalizedPath))
    {
        return FoundSkeleton;
    }

    FSkeleton* LoadedSkeletonData = new FSkeleton();
    if (!ResourceManager.SkeletonSerializer.LoadSkeleton(NormalizedPath, *LoadedSkeletonData))
    {
        UE_LOG_ERROR("[SkeletonLoad] Failed | Path=%s", NormalizedPath.c_str());
        delete LoadedSkeletonData;
        return nullptr;
    }

    return FinalizeLoadedSkeleton(LoadedSkeletonData, NormalizedPath);
}

USkeletonAsset* FSkeletonLoadService::FinalizeLoadedSkeleton(FSkeleton* SkeletonData, const FString& CacheKey)
{
    USkeletonAsset* LoadedSkeleton = UObjectManager::Get().CreateObject<USkeletonAsset>();
    LoadedSkeleton->SetSkeletonData(SkeletonData);

    ResourceManager.SkeletonMap[CacheKey] = LoadedSkeleton;
    if (std::find(ResourceManager.SkeletonFilePaths.begin(), ResourceManager.SkeletonFilePaths.end(), CacheKey)
        == ResourceManager.SkeletonFilePaths.end())
    {
        ResourceManager.SkeletonFilePaths.push_back(CacheKey);
    }

    UE_LOG("[SkeletonLoad] Loaded | Path=%s | Root=%s | Bones=%zu | Sockets=%zu",
           CacheKey.c_str(),
           LoadedSkeleton->GetRootNodeName().c_str(),
           LoadedSkeleton->GetBones().size(),
           LoadedSkeleton->GetSockets().size());

    return LoadedSkeleton;
}
