#include "ParticleModuleDynamicParameter.h"

#include "Component/Particle/ParticleSystemComponent.h"
#include "Engine/Particle/Distributions/DistributionFloatCurve.h"
#include "Object/Object.h"
#include "Particle/ParticleEmitterInstance.h"

UParticleModuleDynamicParameter::UParticleModuleDynamicParameter()
{
    auto MakeConstant = [](float Value) -> UDistributionFloat*
    {
        auto* Dist = UObjectManager::Get().CreateObject<UDistributionFloatCurve>();
        if (Dist)
        {
            Dist->SetConstant(Value);
        }
        return Dist;
    };

    ParamX = MakeConstant(FallbackValue.X);
    ParamY = MakeConstant(FallbackValue.Y);
    ParamZ = MakeConstant(FallbackValue.Z);
    ParamW = MakeConstant(FallbackValue.W);
}

void UParticleModuleDynamicParameter::Spawn(
    FParticleEmitterInstance* Owner,
    uint32                    ModuleOffset,
    float                     SpawnTime,
    FBaseParticle*            Particle
)
{
    (void)ModuleOffset;
    if (!Particle) return;

    UObject* EvalData          = Owner ? Owner->GetComponent() : nullptr;
    Particle->DynamicParameter = Evaluate(SpawnTime, EvalData);
}

void UParticleModuleDynamicParameter::UpdateParticle(
    FParticleEmitterInstance* Owner,
    UParticleLODLevel*        SimulationLOD,
    uint32                    ModuleOffset,
    float                     DeltaTime,
    FBaseParticle*            Particle
)
{
    (void)SimulationLOD;
    (void)ModuleOffset;
    (void)DeltaTime;
    if (!Particle || bSpawnTimeOnly) return;

    UObject* EvalData          = Owner ? Owner->GetComponent() : nullptr;
    Particle->DynamicParameter = Evaluate(Particle->RelativeTime, EvalData);
}

FVector4 UParticleModuleDynamicParameter::Evaluate(float Time, UObject* EvalData) const
{
    return FVector4(
        ParamX ? ParamX->GetValue(Time, EvalData) : FallbackValue.X,
        ParamY ? ParamY->GetValue(Time, EvalData) : FallbackValue.Y,
        ParamZ ? ParamZ->GetValue(Time, EvalData) : FallbackValue.Z,
        ParamW ? ParamW->GetValue(Time, EvalData) : FallbackValue.W
    );
}