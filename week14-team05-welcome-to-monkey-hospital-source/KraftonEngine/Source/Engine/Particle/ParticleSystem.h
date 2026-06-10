#pragma once

#include "Object/Object.h"
#include "Math/Vector.h"
#include "Particle/ParticleEmitter.h"

#include "Source/Engine/Particle/ParticleSystem.generated.h"

// =============================================================================
// UParticleSystem
//   디스크 직렬화 단위의 파티클 에셋. 여러 UParticleEmitter 를 묶는다.
//   PSC 가 이 에셋을 참조하여 EmitterInstance 를 만든다.
// =============================================================================
UCLASS()
class UParticleSystem : public UObject
{
public:
	GENERATED_BODY()
	UParticleSystem() = default;
	~UParticleSystem() override = default;

	UPROPERTY(Edit, Save, Instanced, Category="System", DisplayName="Emitters", Type=Array, AllowedClass=UParticleEmitter)
	TArray<UParticleEmitter*> Emitters;

	// 시스템 자체의 재시작 정책.
	UPROPERTY(Edit, Save, Category="System", DisplayName="Looping")
	bool bLooping = true;

	UPROPERTY(Edit, Save, Category="System", DisplayName="Update Time FPS", Min=10.0f, Max=120.0f)
	float UpdateTimeFPS = 60.0f;

	// 시스템 경계 박스 (모든 emitter 합산). 매 프레임 PSC 에서 갱신.
	UPROPERTY(Edit, Save, Category="System", DisplayName="System Bounds Min")
	FVector SystemBoundsMin = { -100, -100, -100 };
	UPROPERTY(Edit, Save, Category="System", DisplayName="System Bounds Max")
	FVector SystemBoundsMax = {  100,  100,  100 };

	UPROPERTY(Edit, Save, Category="System", DisplayName="Use Fixed Relative Bounding Box")
	bool bUseFixedRelativeBoundingBox = false;

	UPROPERTY(Edit, Save, Category="LOD", DisplayName="Use Automatic LOD")
	bool bUseAutomaticLOD = false;

	// Tune distance thresholds first. Close-range/high-importance gameplay VFX
	// usually keep LOD0 farther out, while environment/background VFX can switch
	// earlier. Beam/ribbon-heavy effects often need more conservative thresholds
	// because reduction and silhouette changes are easier to notice. Treat these
	// thresholds as the main authoring control before touching hysteresis/delay.
	UPROPERTY(Edit, Save, Category="LOD", DisplayName="LOD Distances", Type=Array)
	TArray<float> LODDistances;

	// Tune hysteresis after distance thresholds. This is a spatial stabilizer, not
	// the primary quality dial. Keep it large enough to suppress boundary chatter
	// but smaller than the intentional distance gap between adjacent LODs.
	UPROPERTY(Edit, Save, Category="LOD", DisplayName="LOD Distance Hysteresis", Min=0.0f)
	float LODDistanceHysteresis = 100.0f;

	// Tune switch delay last. Start with distance + hysteresis, then add delay only
	// if camera motion still causes distracting one-frame transitions. If lower-LOD
	// reduction is very strong, prefer adjusting distance thresholds first.
	UPROPERTY(Edit, Save, Category="LOD", DisplayName="LOD Switch Delay", Min=0.0f)
	float LODSwitchDelay = 0.0f;

	// 로드 후 BuildEmitters (반사 직렬화는 UObject 템플릿이 자동 처리).
	void OnPostLoad(class FArchive& Ar) override;
	UObject* Duplicate(UObject* NewOuter = nullptr) const override;
	void PostDuplicate() override;

	// 에디터/런타임 헬퍼
	UParticleEmitter* AddEmitter();
	void              RemoveEmitter(int32 Index);
	void              MoveEmitter(int32 FromIndex, int32 ToIndex);

	int32 GetEmitterCount() const { return static_cast<int32>(Emitters.size()); }
	UParticleEmitter* GetEmitter(int32 Index) const;

	int32 GetMaxLODCount() const;
	void  EnsureLODDistances();
	void  NormalizeAutomaticLODTuning();
	int32 GetLODIndexForDistance(float Distance) const;
	float GetLODDistance(int32 LODIndex) const;
	void  SetLODDistance(int32 LODIndex, float Distance);

	// 모든 emitter 의 CacheEmitterModuleInfo() 호출.
	void BuildEmitters();

	void SetSourcePath(const FString& InPath) { SourcePath = InPath; }
	const FString& GetSourcePath() const { return SourcePath; }

private:
	FString MakeUniqueEmitterName();
	
private:
	FString SourcePath;
};
