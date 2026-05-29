#pragma once

#include "Particle/ParticleModule.h"
#include "ParticleModuleTypeDataBeam2.generated.h"

struct FBeam2TypeDataPayload
{
	FVector SourcePoint;
	FVector TargetPoint;
	FVector SourceTangent;
	FVector TargetTangent;
	float SourceStrength;
	float TargetStrength;
	int32 LockSource;         // Used as a boolean (0 or 1)
	int32 LockTarget;         // Used as a boolean (0 or 1)
	int32 LockSourceTangent;  // Used as a boolean (0 or 1)
	int32 LockTargetTangent;  // Used as a boolean (0 or 1)
};

UENUM()
enum EBeam2Method : int
{
	PEB2M_Distance,
	PEB2M_Target,
	PEB2M_Branch,
	PEB2M_MAX,
};

UENUM()
enum EBeamTaperMethod : int
{
	PEBTM_None,
	PEBTM_Full,
	PEBTM_Partial,
	PEBTM_MAX,
};

USTRUCT()
struct FBeamTargetData
{
	GENERATED_BODY(FBeamTargetData)

	UPROPERTY(Edit, Category="Beam")
	FName TargetName;

	UPROPERTY(Edit, Category="Beam", Min=0.0f, Max=100.0f, Speed=1.0f)
	float TargetPercentage = 100.0f;
};

UCLASS()
class UParticleModuleTypeDataBeam2 : public UParticleModuleTypeDataBase
{
public:
	GENERATED_BODY(UParticleModuleTypeDataBeam2)

	bool IsABeamEmitter() const override { return true; }
	bool SupportsSpecificScreenAlignmentFlags() const override { return true; }
	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData = nullptr) override { (void)TypeData; return sizeof(FBeam2TypeDataPayload); }
	UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const override;
	void Spawn(const FSpawnContext& Context) override;

	// Distance emits along local +X. Target emits from SourcePoint to TargetPoint.
	// Branch is kept for asset compatibility, but should be treated as Target until
	// parent-emitter beam branching exists in this engine.
	UPROPERTY(Edit, Category="Beam")
	EBeam2Method BeamMethod = PEB2M_Target;

	UPROPERTY(Edit, Category="Beam", Min=0, Max=128, Speed=1.0f)
	int32 InterpolationPoints = 8;

	UPROPERTY(Edit, Category="Beam", Min=1, Max=16, Speed=1.0f)
	int32 Sheets = 1;

	UPROPERTY(Edit, Category="Beam", Min=1, Max=64, Speed=1.0f)
	int32 MaxBeamCount = 1;

	UPROPERTY(Edit, Category="Beam", Min=0.0f, Max=10000.0f, Speed=1.0f)
	float Speed = 0.0f;

	UPROPERTY(Edit, Category="Beam")
	bool bAlwaysOn = true;

	UPROPERTY(Edit, Category="Beam", Min=0, Max=32, Speed=1.0f)
	int32 UpVectorStepSize = 0;

	UPROPERTY(Edit, Category="Beam", Min=0.0f, Max=10000.0f, Speed=1.0f)
	float Distance = 100.0f;

	UPROPERTY(Edit, Category="Beam")
	FVector SourcePoint = FVector::ZeroVector;

	UPROPERTY(Edit, Category="Beam")
	FVector TargetPoint = FVector(100.0f, 0.0f, 0.0f);

	UPROPERTY(Edit, Category="Beam", Min=0.0f, Max=1000.0f, Speed=0.25f)
	float Width = 8.0f;

	UPROPERTY(Edit, Category="Beam", Min=1, Max=256, Speed=1.0f)
	int32 TextureTile = 1;

	UPROPERTY(Edit, Category="Beam", Min=0.0f, Max=10000.0f, Speed=1.0f)
	float TextureTileDistance = 0.0f;

	UPROPERTY(Edit, Category="Beam")
	FVector Color = FVector::OneVector;

	UPROPERTY(Edit, Category="Beam", Min=0.0f, Max=1.0f, Speed=0.01f)
	float Alpha = 1.0f;

	UPROPERTY(Edit, Category="Branching")
	FName BranchParentName;

	UPROPERTY(Edit, Category="Taper")
	EBeamTaperMethod TaperMethod = PEBTM_None;

	UPROPERTY(Edit, Category="Taper", Min=0.0f, Max=1.0f, Speed=0.01f)
	float TaperFactor = 1.0f;

	UPROPERTY(Edit, Category="Taper", Min=0.0f, Max=100.0f, Speed=0.01f)
	float TaperScale = 1.0f;

	UPROPERTY(Edit, Category="Rendering")
	bool bRenderGeometry = true;

	UPROPERTY(Edit, Category="Rendering")
	bool bRenderDirectLine = false;

	UPROPERTY(Edit, Category="Rendering")
	bool bRenderLines = false;

	UPROPERTY(Edit, Category="Rendering")
	bool bRenderTessellation = false;

	UPROPERTY(Edit, Category="Targets")
	TArray<FBeamTargetData> TargetData;
};
