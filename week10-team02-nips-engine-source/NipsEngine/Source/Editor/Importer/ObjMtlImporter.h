#pragma once

#include "Core/CoreMinimal.h"
#include "Render/Resource/Material.h"

/**
 * OBJ import 전용 .mtl 파서입니다. Runtime material asset은 .mat/.matinst를 사용합니다.
 */
class FObjMtlImporter
{
public:
	static bool Load(const FString& FilePath, TMap<FString, UMaterial*>& OutMaterialAssets, ID3D11Device* Device);
};
