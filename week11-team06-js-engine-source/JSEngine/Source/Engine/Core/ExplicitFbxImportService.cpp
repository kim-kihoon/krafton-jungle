#include "Core/ExplicitFbxImportService.h"

#include "Core/Logging/Log.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"

FExplicitFbxImportService::FExplicitFbxImportService(FResourceManager& InResourceManager)
    : ResourceManager(InResourceManager)
{
}

TArray<FImportedFbxAssetRecord> FExplicitFbxImportService::Import(const FString& SourceFbxPath)
{
    const FString NormalizedSourcePath = FPaths::Normalize(SourceFbxPath);
    const FFbxMeshContentInfo ContentInfo = ResourceManager.InspectFbxMeshContent(NormalizedSourcePath);

    const bool bHasMesh = ContentInfo.bHasStaticMesh || ContentInfo.bHasSkeletalMesh;
    if (bHasMesh)
    {
        ResourceManager.ImportMaterialFromFbx(NormalizedSourcePath);
    }

    TArray<USkeletonAsset*> ImportedSkeletons;
    if (ContentInfo.bHasSkeletalMesh)
    {
        ImportedSkeletons = ResourceManager.ImportSkeletonsFromFbx(NormalizedSourcePath);
    }

    if (ContentInfo.bHasStaticMesh)
    {
        ResourceManager.ImportStaticMeshFromFbx(NormalizedSourcePath, false);
    }

    if (ContentInfo.bHasSkeletalMesh)
    {
        ResourceManager.ImportSkeletalMeshFromFbx(NormalizedSourcePath, FString(), false, &ImportedSkeletons);
        ResourceManager.ImportAnimationSequencesFromFbx(NormalizedSourcePath, &ImportedSkeletons);
    }

    FImportedFbxAssetDiscovery Discovery;
    TArray<FImportedFbxAssetRecord> Records = Discovery.DiscoverForSourceFbx(NormalizedSourcePath);

    UE_LOG("[ExplicitFbxImport] Imported | Source=%s | Records=%zu",
           NormalizedSourcePath.c_str(),
           Records.size());

    return Records;
}
