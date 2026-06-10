#pragma once

#include "Particle/TypeData/ParticleModuleTypeDataBase.h"
#include "Math/Vector.h"
#include "Engine/Particle/Distributions/DistributionFloat.h"

#include "Source/Engine/Particle/TypeData/ParticleModuleTypeDataBeam.generated.h"

UENUM()
enum Beam2SourceTargetMethod : int
{
	PEB2STM_Default,
	PEB2STM_UserSet,
	PEB2STM_Emitter,
	PEB2STM_Particle,
	PEB2STM_Actor,
	PEB2STM_MAX,
};

UENUM()
enum Beam2SourceTargetTangentMethod : int
{
	PEB2STTM_Direct,
	PEB2STTM_UserSet,
	PEB2STTM_Distribution,
	PEB2STTM_Emitter,
	PEB2STTM_MAX,
};

UENUM()
enum BeamModifierType : int
{
	PEB2MT_Source,
	PEB2MT_Target,
	PEB2MT_MAX,
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

// =============================================================================
// UParticleModuleTypeDataBeam
//   Beam Emitter — Source ↔ Target 두 endpoint 사이에 tessellated quad-strip
//   beam 을 그린다. TypeData는 Beam 공통 렌더/방식 설정만 보유하고,
//   Source/Target/Noise는 Beam 전용 모듈에서 분리 관리한다.
// =============================================================================
UCLASS()
class UParticleModuleTypeDataBeam : public UParticleModuleTypeDataBase
{
public:
	GENERATED_BODY()
	UParticleModuleTypeDataBeam();


	const char* GetDisplayName() const override { return "TypeData Beam"; }

	FParticleEmitterInstance* CreateInstance(UParticleSystemComponent* InComponent) override;

	float EvaluateWidth(float EmitterTime, UObject* Data = nullptr) const;
	float EvaluateDistance(float EmitterTime, UObject* Data = nullptr) const;

	// Distance: Source + local X * Distance. Target: Beam Target module/default target 사용.
	UPROPERTY(Edit, Save, Category="Beam", DisplayName="Beam Method", Enum=EBeam2Method)
	EBeam2Method BeamMethod = PEB2M_Target;

	// 0이면 Source에서 Target까지 즉시 연결한다. 0보다 크면 초당 해당 거리만큼 beam이 자라난다.
	UPROPERTY(Edit, Save, Category="Beam", DisplayName="Speed", Min=0.0f, Max=100000.0f)
	float Speed = 0.0f;

	UPROPERTY(Edit, Save, Category="Beam", DisplayName="Interpolation Points", Min=0.0f, Max=128.0f)
	int32 InterpolationPoints = 8;

	// Evaluated with EmitterTime. Beam strip 전체 폭.
	UPROPERTY(Edit, Save, Instanced, Category="Beam", DisplayName="Width", Type=ObjectRef, AllowedClass=UDistributionFloat)
	UDistributionFloat* WidthDistribution = nullptr;

	// Evaluated with EmitterTime. BeamMethod == Distance이거나 Target module이 Distance일 때 사용.
	UPROPERTY(Edit, Save, Instanced, Category="Beam", DisplayName="Distance", Type=ObjectRef, AllowedClass=UDistributionFloat)
	UDistributionFloat* DistanceDistribution = nullptr;

	UPROPERTY(Edit, Save, Category="Beam", DisplayName="Tile UV")
	bool bTileUV = true;

	UPROPERTY(Edit, Save, Category="Beam", DisplayName="Taper Method", Enum=EBeamTaperMethod)
	EBeamTaperMethod TaperMethod = PEBTM_None;

	// TaperMethod == Full일 때 target 쪽 폭 배율. 0이면 끝이 뾰족해지고, 1이면 동일 폭이다.
	UPROPERTY(Edit, Save, Category="Beam", DisplayName="Taper Factor", Min=0.0f, Max=10.0f)
	float TaperFactor = 1.0f;

	UPROPERTY(Edit, Save, Category="Beam", DisplayName="Render Geometry")
	bool bRenderGeometry = true;

	// Beam Source/Target module이 없을 때 쓰는 fallback endpoint.
	UPROPERTY(Edit, Save, Category="Beam Defaults", DisplayName="Default Source")
	FVector DefaultSource = { 0, 0, 0 };

	UPROPERTY(Edit, Save, Category="Beam Defaults", DisplayName="Default Target")
	FVector DefaultTarget = { 1, 0, 0 };

	// Legacy fields kept for old assets. New editor/runtime path uses distributions above.
	UPROPERTY(Save, Category="Beam Legacy", DisplayName="Legacy Width")
	float Width = 0.2f;

	UPROPERTY(Save, Category="Beam Legacy", DisplayName="Legacy Distance")
	float Distance = 1.0f;
};
