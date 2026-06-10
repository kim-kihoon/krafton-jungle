#pragma once

#include "Particle/TypeData/ParticleModuleTypeDataBase.h"
#include "Object/Ptr/SoftObjectPtr.h"

#include "Source/Engine/Particle/TypeData/ParticleModuleTypeDataMesh.generated.h"

UENUM()
enum EMeshScreenAlignment : int
{
	PSMA_MeshFaceCameraWithRoll,
	PSMA_MeshFaceCameraWithSpin,
	PSMA_MeshFaceCameraWithLockedAxis,
	PSMA_MAX,
};

UENUM()
enum EMeshCameraFacingUpAxis : int
{
	CameraFacing_NoneUP,
	CameraFacing_ZUp,
	CameraFacing_NegativeZUp,
	CameraFacing_YUp,
	CameraFacing_NegativeYUp,
	CameraFacing_MAX,
};

UENUM()
enum EMeshCameraFacingOptions : int
{
	XAxisFacing_NoUp,
	XAxisFacing_ZUp,
	XAxisFacing_NegativeZUp,
	XAxisFacing_YUp,
	XAxisFacing_NegativeYUp,
	LockedAxis_ZAxisFacing,
	LockedAxis_NegativeZAxisFacing,
	LockedAxis_YAxisFacing,
	LockedAxis_NegativeYAxisFacing,
	VelocityAligned_ZAxisFacing,
	VelocityAligned_NegativeZAxisFacing,
	VelocityAligned_YAxisFacing,
	VelocityAligned_NegativeYAxisFacing,
	EMeshCameraFacingOptions_MAX,
};

UENUM()
enum EParticleAxisLock : int
{
	EPAL_NONE,
	EPAL_X,
	EPAL_Y,
	EPAL_Z,
	EPAL_NEGATIVE_X,
	EPAL_NEGATIVE_Y,
	EPAL_NEGATIVE_Z,
	EPAL_ROTATE_X,
	EPAL_ROTATE_Y,
	EPAL_ROTATE_Z,
	EPAL_MAX,
};

class UStaticMesh;

// =============================================================================
// UParticleModuleTypeDataMesh
//   Mesh Emitter — 각 입자가 StaticMesh 의 instance 로 렌더된다.
//   FParticleMeshEmitterInstance 를 생성하고, ReplayData 에 Mesh 포인터를 박는다.
// =============================================================================
UCLASS()
class UParticleModuleTypeDataMesh : public UParticleModuleTypeDataBase
{
public:
	GENERATED_BODY()
	UParticleModuleTypeDataMesh() = default;

	const char* GetDisplayName() const override { return "TypeData (Mesh)"; }

	FParticleEmitterInstance* CreateInstance(UParticleSystemComponent* InComponent) override;

	UPROPERTY(Edit, Save, Category="Mesh", DisplayName="Static Mesh", AssetType="StaticMesh")
	FSoftObjectPtr MeshSlot;
	UStaticMesh*   CachedMesh = nullptr; // ResolveMesh() 에서 resolve

	UStaticMesh* ResolveMesh();

	// 메시 정렬 모드 (속도/축/카메라).
	UENUM()
	enum class EMeshAlignment : uint8 { None, Velocity, FacingCamera, AxisLock };
	UPROPERTY(Edit, Save, Category="Mesh", DisplayName="Alignment", Enum=EMeshAlignment)
	EMeshAlignment Alignment = EMeshAlignment::None;

	UPROPERTY(Edit, Save, Category="Mesh", DisplayName="Override Material")
	bool bOverrideMaterial = false;
};
