#include "ParticleModuleRequired.h"

#include "Materials/Material.h"
#include "Materials/MaterialManager.h"

void UParticleModuleRequired::SetToSensibleDefaults(UParticleEmitter* Owner)
{
	MaterialSlot = "None";
	CachedMaterial = nullptr;

	bUseLocalSpace = false;

	SubImagesHorizontal = 1;
	SubImagesVertical = 1;

	EmitterDuration = 1.0f;
	EmitterLoops = 0;

	SortMode = ESortMode::None;
	ScreenAlignment = EScreenAlignment::Square;
}

UMaterial* UParticleModuleRequired::ResolveMaterial()
{
	if (CachedMaterial) return CachedMaterial;

	const FString& Path = MaterialSlot.ToString();
	if (Path.empty() || Path == "None") return nullptr;

	CachedMaterial = FMaterialManager::Get().GetOrCreateMaterial(Path);
	return CachedMaterial;
}
