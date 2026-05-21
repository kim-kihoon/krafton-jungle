#pragma once

#include "Core/CoreMinimal.h"
#include "Core/ImportedFbxAssetDiscovery.h"

class FResourceManager;

class FExplicitFbxImportService
{
public:
    explicit FExplicitFbxImportService(FResourceManager& InResourceManager);

    TArray<FImportedFbxAssetRecord> Import(const FString& SourceFbxPath);

private:
    FResourceManager& ResourceManager;
};
