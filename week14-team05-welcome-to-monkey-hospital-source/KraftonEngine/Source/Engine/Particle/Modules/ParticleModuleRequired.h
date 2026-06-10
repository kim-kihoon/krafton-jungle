#pragma once

#include "Particle/ParticleModule.h"
#include "Math/Vector.h"
#include "Object/Ptr/SoftObjectPtr.h"
#include "Render/Types/RenderStateTypes.h"

#include "Source/Engine/Particle/Modules/ParticleModuleRequired.generated.h"

UENUM()
enum class EParticleUVFlipMode : uint8
{
	None,
	FlipUV,
	FlipUOnly,
	FlipVOnly,
	RandomFlipUV,
	RandomFlipUOnly,
	RandomFlipVOnly,
	RandomFlipUVIndependent,
};

UENUM()
enum EParticleScreenAlignment : int
{
	PSA_FacingCameraPosition,
	PSA_Square,
	PSA_Rectangle,
	PSA_Velocity,
	PSA_AwayFromCenter,
	PSA_TypeSpecific,
	PSA_FacingCameraDistanceBlend,
	PSA_MAX,
};

UENUM()
enum EParticleSortMode : int
{
	PSORTMODE_None,
	PSORTMODE_ViewProjDepth,
	PSORTMODE_DistanceToView,
	PSORTMODE_Age_OldestFirst,
	PSORTMODE_Age_NewestFirst,
	PSORTMODE_MAX,
};

class UMaterial;

// =============================================================================
// UParticleModuleRequired
//   Emitter 에 필수적인 설정 모듈. LODLevel 의 RequiredModule 슬롯에 1개만.
//   - Material, SubUV grid, Sort mode, Local/World space, Loop policy 등
// =============================================================================
UCLASS()
class UParticleModuleRequired : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleRequired() = default;

	EModuleCategory GetCategory() const override { return EModuleCategory::Required; }
	const char*     GetDisplayName() const override { return "Required"; }
	bool            IsUnique() const override { return true; }

	void SetToSensibleDefaults(UParticleEmitter* Owner) override;

	// --- Material ---
	UPROPERTY(Edit, Save, Category="Required", DisplayName="Material", AssetType="Material")
	FSoftObjectPtr MaterialSlot;
	UMaterial* CachedMaterial = nullptr; // ResolveMaterial() 에서 resolve

	UMaterial* ResolveMaterial(); // soft → hard

	// 렌더 BlendState는 Material(.mat)이 single source of truth — Required에는 필드를 두지 않는다.

	// --- Space ---
	UPROPERTY(Edit, Save, Category="Required", DisplayName="Use Local Space")
	bool bUseLocalSpace = false;

	// --- SubUV ---
	UPROPERTY(Edit, Save, Category="SubUV", DisplayName="SubImages Horizontal", Min=1.0f, Max=64.0f)
	int32 SubImagesHorizontal = 1;
	UPROPERTY(Edit, Save, Category="SubUV", DisplayName="SubImages Vertical", Min=1.0f, Max=64.0f)
	int32 SubImagesVertical = 1;

	// --- Lifetime / Loop ---
	UPROPERTY(Edit, Save, Category="Required", DisplayName="Emitter Duration")
	float EmitterDuration = 1.0f; // seconds; 0 = infinite

	UPROPERTY(Edit, Save, Category="Required", DisplayName="Emitter Loops")
	int32 EmitterLoops = 0; // 0 = infinite

	// --- Sort ---
	UENUM()
	enum class ESortMode : uint8 { None, ViewProjDepth, ViewDistance, Age_OldestFirst, Age_NewestFirst };
	UPROPERTY(Edit, Save, Category="Required", DisplayName="Sort Mode", Enum=ESortMode)
	ESortMode SortMode = ESortMode::None;

	// --- Screen alignment (Sprite 전용) ---
	UENUM()
	enum class EScreenAlignment : uint8 { Square, Rectangle, Velocity, FacingCameraPosition };
	UPROPERTY(Edit, Save, Category="Required", DisplayName="Screen Alignment", Enum=EScreenAlignment)
	EScreenAlignment ScreenAlignment = EScreenAlignment::Square;
};
