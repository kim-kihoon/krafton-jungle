#pragma once

#include "Core/Singleton.h"
#include "Core/CoreTypes.h"

class UParticleSystem;

class FParticleSystemManager : public TSingleton<FParticleSystemManager>
{
	friend class TSingleton<FParticleSystemManager>;

public:
	UParticleSystem* Load(const FString& Path);
	UParticleSystem* Find(const FString& Path) const;
	bool Save(UParticleSystem* ParticleSystem);
	bool Rename(UParticleSystem* ParticleSystem, const FString& NewName);

private:
	TMap<FString, UParticleSystem*> LoadedParticleSystems;
};
