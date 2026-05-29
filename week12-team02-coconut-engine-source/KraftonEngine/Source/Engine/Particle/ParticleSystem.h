#pragma once

#include "Object/Object.h"
#include "ParticleSystem.generated.h"

class UParticleEmitter;

UCLASS()
class UFXSystemAsset : public UObject
{
public:
	GENERATED_BODY(UFXSystemAsset)
};

UCLASS()
class UParticleSystem : public UFXSystemAsset
{
public:
	GENERATED_BODY(UParticleSystem)

	const FString& GetAssetPathFileName() const override { return AssetPathFileName; }
	void SetAssetPathFileName(const FString& InPath) { AssetPathFileName = InPath; }

	TArray<UParticleEmitter*> Emitters;

	//LOD Related
	int32 GetLODCount() const;
	int32 CreateLOD(float Distance = -1.0f);
	bool RemoveLOD(int32 LODIndex);

	float GetLODDistance(int32 LODIndex) const;
	bool SetLODDistance(int32 LODIndex, float Distance);
	const TArray<float>& GetLODDistances() const { return LODDistances; }

	void NormalizeLODData();

private:
	TArray<float> LODDistances = { 0.0f };
	FString AssetPathFileName;
};
