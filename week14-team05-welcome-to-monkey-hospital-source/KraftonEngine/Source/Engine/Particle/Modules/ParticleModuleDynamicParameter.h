#pragma once

#include "Engine/Particle/Distributions/DistributionFloat.h"
#include "Math/Vector.h"
#include "Particle/ParticleModule.h"

#include "Source/Engine/Particle/Modules/ParticleModuleDynamicParameter.generated.h"

UCLASS()
class UParticleModuleDynamicParameter : public UParticleModule
{
public:
    GENERATED_BODY()
    UParticleModuleDynamicParameter();

    EModuleCategory GetCategory() const override
    {
        return EModuleCategory::Color;
    }

    const char* GetDisplayName() const override
    {
        return "Dynamic Parameter";
    }

    void Spawn(
        FParticleEmitterInstance* Owner,
        uint32                    ModuleOffset,
        float                     SpawnTime,
        FBaseParticle*            Particle
    ) override;
    void UpdateParticle(
        FParticleEmitterInstance* Owner,
        UParticleLODLevel*        SimulationLOD,
        uint32                    ModuleOffset,
        float                     DeltaTime,
        FBaseParticle*            Particle
    ) override;

    // When false, values are re-evaluated every update from Particle->RelativeTime.
    // When true, values are evaluated only at spawn time from SpawnTime.
    UPROPERTY(Edit, Save, Category="Dynamic Parameter", DisplayName="Spawn Time Only")
    bool bSpawnTimeOnly = false;

    UPROPERTY(Edit, Save, Category="Dynamic Parameter", DisplayName="Fallback Value")
    FVector4 FallbackValue = { 0, 0, 0, 0 };

    UPROPERTY(Edit, Save, Instanced, Category="Dynamic Parameter", DisplayName="Param X", Type=ObjectRef, AllowedClass=UDistributionFloat)
    UDistributionFloat* ParamX = nullptr;

    UPROPERTY(Edit, Save, Instanced, Category="Dynamic Parameter", DisplayName="Param Y", Type=ObjectRef, AllowedClass=UDistributionFloat)
    UDistributionFloat* ParamY = nullptr;

    UPROPERTY(Edit, Save, Instanced, Category="Dynamic Parameter", DisplayName="Param Z", Type=ObjectRef, AllowedClass=UDistributionFloat)
    UDistributionFloat* ParamZ = nullptr;

    UPROPERTY(Edit, Save, Instanced, Category="Dynamic Parameter", DisplayName="Param W", Type=ObjectRef, AllowedClass=UDistributionFloat)
    UDistributionFloat* ParamW = nullptr;

private:
    FVector4 Evaluate(float Time, UObject* EvalData) const;
};