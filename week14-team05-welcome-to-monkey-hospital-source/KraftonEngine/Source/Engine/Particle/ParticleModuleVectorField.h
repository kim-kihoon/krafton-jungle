#pragma once

#include "Particle/ParticleModule.h"
#include "Math/Rotator.h"
#include "Object/Ptr/SoftObjectPtr.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/Particle/ParticleModuleVectorField.generated.h"

class UVectorFieldAsset;
class FScene;
class UParticleLODLevel;

UCLASS()
class UParticleModuleVectorFieldRotation : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleVectorFieldRotation() = default;

	EModuleCategory GetCategory() const override { return EModuleCategory::Rotation; }
	const char* GetDisplayName() const override { return "Vector Field Rotation"; }

	UPROPERTY(Edit, Save, Category="Vector Field Rotation", DisplayName="Rotation", Type=Rotator, Min=0.0f, Max=0.0f, Speed=0.1f)
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(Edit, Save, Category="Vector Field Rotation", DisplayName="Rotation Rate", Type=Rotator, Min=0.0f, Max=0.0f, Speed=0.1f)
	FRotator RotationRate = FRotator::ZeroRotator;

	FQuat GetRotation(float EmitterTime) const;
};

// Applies a vector-field asset in emitter-local space.
// The .fga source is never parsed here; this module only loads the serialized
// UVectorFieldAsset package produced by the importer.
UCLASS()
class UParticleModuleVectorFieldLocal : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleVectorFieldLocal() = default;

	EModuleCategory GetCategory() const override { return EModuleCategory::Velocity; }
	const char* GetDisplayName() const override { return "Local Vector Field"; }
	void UpdateParticle(FParticleEmitterInstance* Owner, UParticleLODLevel* SimulationLOD,
	                    uint32 ModuleOffset, float DeltaTime, FBaseParticle* Particle) override;

	UPROPERTY(Edit, Save, Category="Local Vector Field", DisplayName="Vector Field", AssetType="UVectorFieldAsset")
	FSoftObjectPtr VectorFieldPath = "None";

	// Treat sampled vectors as acceleration/force and accumulate them into BaseVelocity.
	UPROPERTY(Edit, Save, Category="Local Vector Field", DisplayName="Intensity", Min=0.0f, Max=10000.0f, Speed=0.1f)
	float Intensity = 1.0f;

	// Moves the field volume in emitter/component-local space. This does not move the emitter.
	UPROPERTY(Edit, Save, Category="Local Vector Field", DisplayName="Field Bounds Offset", Type=Vec3)
	FVector FieldBoundsOffset = FVector(0.0f, 0.0f, 0.0f);

	// Scales the imported vector-field bounds in emitter/component-local space.
	// For example, an imported [-1, 1] field with scale (100,100,100) affects [-100,100].
	// This changes where the field is sampled, not the vector strength. Use Intensity for strength.
	UPROPERTY(Edit, Save, Category="Local Vector Field", DisplayName="Field Bounds Scale", Type=Vec3, Min=0.001f, Speed=0.1f)
	FVector FieldBoundsScale = FVector(1.0f, 1.0f, 1.0f);

	// Nearest is useful for debugging the grid cells. Trilinear gives smoother motion
	// and is still cheap for typical 8^3 / 16^3 vector-field assets.
	UPROPERTY(Edit, Save, Category="Local Vector Field", DisplayName="Use Trilinear Sampling")
	bool bUseTrilinearSampling = true;

	// Draws the effective local vector-field bounds after Field Bounds Offset/Scale are applied.
	UPROPERTY(Edit, Save, Category="Local Vector Field", DisplayName="Draw Bounds")
	bool bDrawBounds = true;

	UPROPERTY(Edit, Save, Category="Local Vector Field", DisplayName="Draw Vectors")
	bool bDrawVectors = false;

	UPROPERTY(Edit, Save, Category="Local Vector Field", DisplayName="Vector Draw Scale", Min=0.0f, Max=10000.0f, Speed=0.1f)
	float DebugVectorScale = 0.3f;

	bool ShouldExposeProperty(const FProperty& Property) const override;
	void AppendFieldDebugLines(FScene& Scene, const FMatrix& ComponentToWorld, const UParticleLODLevel* LODLevel, float EmitterTime);

private:
	UVectorFieldAsset* ResolveVectorField();
	FVector GetSafeFieldBoundsScale() const;
	FQuat ResolveFieldRotation(const UParticleLODLevel* LODLevel, float EmitterTime) const;
	FVector TransformComponentLocalToFieldLocal(const FVector& ComponentLocalPosition, const FQuat& FieldRotation) const;
	FVector TransformFieldLocalToComponentLocal(const FVector& FieldLocalPosition, const FQuat& FieldRotation) const;
	FVector TransformFieldVectorToComponentLocal(const FVector& FieldVector, const FQuat& FieldRotation) const;

	FString CachedVectorFieldPath;
	// 비소유 캐시 — 매니저(FGCObject)가 에셋을 살려두고, 회수/리임포트 시엔 weak 가 자동 null 처리해
	// dangling 을 막는다. null 이면 ResolveVectorField 가 경로로 재로드한다.
	TWeakObjectPtr<UVectorFieldAsset> CachedVectorField;
};
