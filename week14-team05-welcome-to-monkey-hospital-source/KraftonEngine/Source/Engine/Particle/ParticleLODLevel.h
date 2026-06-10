#pragma once

#include "Object/Object.h"

#include "Source/Engine/Particle/ParticleLODLevel.generated.h"

class UParticleModule;
class UParticleModuleRequired;
class UParticleModuleSpawn;
class UParticleModuleTypeDataBase;

// =============================================================================
// UParticleLODLevel
//   하나의 LOD 단계에 해당하는 모듈 집합.
//   Required / TypeData 는 단 1개씩만 허용되고 (IsUnique=true),
//   그 외 모듈은 Modules 배열에 자유롭게 추가된다.
//   LOD 0 이 기준이고 그 외 LOD 는 RefreshFromLOD0() 로 값을 추출/보간.
// =============================================================================
UCLASS()
class UParticleLODLevel : public UObject
{
public:
	enum class ELODModuleSyncMode : uint8
	{
		InheritFromLOD0 = 0,
		Override = 1,
	};

	GENERATED_BODY()
	UParticleLODLevel() = default;
	~UParticleLODLevel() override = default;

	UPROPERTY(Edit, Save, Category="LOD", DisplayName="Level")
	int32 Level = 0;

	UPROPERTY(Edit, Save, Category="LOD", DisplayName="Enabled")
	bool bEnabled = true;

	// Required: emitter 의 필수 설정 (Material, Sub UV, Sort 등) — 항상 존재.
	UPROPERTY(Save, Instanced, Category="Modules", DisplayName="Required Module", Type=ObjectRef, AllowedClass=UParticleModuleRequired)
	UParticleModuleRequired* RequiredModule = nullptr;

	// Spawn: 분당 spawn rate / Burst — 항상 존재.
	UPROPERTY(Save, Instanced, Category="Modules", DisplayName="Spawn Module", Type=ObjectRef, AllowedClass=UParticleModuleSpawn)
	UParticleModuleSpawn* SpawnModule = nullptr;

	// TypeData: 입자가 Sprite/Mesh/Beam/Ribbon 중 무엇인지 결정. nullptr 이면 Sprite.
	UPROPERTY(Save, Instanced, Category="Modules", DisplayName="Type Data", Type=ObjectRef, AllowedClass=UParticleModuleTypeDataBase)
	UParticleModuleTypeDataBase* TypeDataModule = nullptr;

	// 그 외 (Lifetime, Location, Velocity, Color, Size, Collision, EventGenerator, ...)
	UPROPERTY(Save, Instanced, Category="Modules", DisplayName="Modules", Type=Array, AllowedClass=UParticleModule)
	TArray<UParticleModule*> Modules;

	// Phase 4/5 keeps lightweight module-level sync metadata as a bridge toward
	// a future Base + Override LOD model. Full property-level overrides and
	// deeper reduction/scaling policy are still intentionally deferred.
	// Authoring meaning:
	// - true  : this slot is still inherited/materialized from LOD0
	// - false : this derived LOD now owns the slot as an explicit override
	// Future "Reset to Inherited" style actions should restore the sync flag and
	// then let UpdateFromLOD0() rebuild the inherited slot from the LOD0 source.
	UPROPERTY(Edit, Save, Category="LOD", DisplayName="Sync Required Module From LOD0")
	bool bSyncRequiredModuleFromLOD0 = true;

	UPROPERTY(Edit, Save, Category="LOD", DisplayName="Sync Spawn Module From LOD0")
	bool bSyncSpawnModuleFromLOD0 = true;

	UPROPERTY(Edit, Save, Category="LOD", DisplayName="Sync Type Data From LOD0")
	bool bSyncTypeDataModuleFromLOD0 = true;

	UPROPERTY(Save, Category="LOD", DisplayName="Regular Module Sync Modes", Type=Array)
	TArray<uint8> RegularModuleSyncModes;

	// Internal authoring support metadata for inherited regular modules.
	// Most authors only need to know whether a module is inherited or overridden;
	// this source binding exists to keep inherited matching stable after LOD0
	// changes and should not usually be treated as a primary editing control.
	UPROPERTY(Save, Category="LOD", DisplayName="Regular Module Source LOD0 Indices", Type=Array)
	TArray<int32> RegularModuleSourceLOD0Indices;

	void PostDuplicate() override;

	// --- API ---
	// Derived LOD resync entry point. Inherited data follows LOD0 changes,
	// explicit overrides stay local, deferred reduction reapplies afterward,
	// and malformed metadata can fall back to a full-copy rebuild.
	// This currently refreshes the stored materialized derived-LOD graph itself.
	// A future runtime effective-LOD materialization step may consume a separate
	// built result from the same source/override metadata, but that split is not
	// active yet.
	// LOD 변경 시 LOD 0 으로부터 본인 값을 재추출.
	void UpdateFromLOD0(UParticleLODLevel* LOD0);
	ELODModuleSyncMode GetRegularModuleSyncMode(int32 ModuleIndex) const;
	void SetRegularModuleSyncMode(int32 ModuleIndex, ELODModuleSyncMode InMode);
	int32 GetRegularModuleSourceLOD0Index(int32 ModuleIndex) const;
	void SetRegularModuleSourceLOD0Index(int32 ModuleIndex, int32 SourceIndex);
	bool HasRegularModuleOverrides() const;
	// "Reset to Inherited" for regular modules conceptually means restoring
	// inherited sync mode/source binding metadata and then resyncing from LOD0.
	void ResetRegularModuleSyncModes(ELODModuleSyncMode DefaultMode = ELODModuleSyncMode::InheritFromLOD0);
	void ResetRegularModuleSourceLOD0Indices(int32 DefaultSourceIndex = -1, bool bMapToCurrentIndex = false);
	void NormalizeCoreSlotSyncMetadata();
	void NormalizeRegularModuleSyncMetadata();

	// 동일 카테고리 중복/required 충돌 등을 검증. true 면 정상.
	bool ValidateModules() const;

	// 모듈 1개 추가/제거 (에디터 hook). 카테고리 정책 (Unique) 을 검사.
	bool AddModule(UParticleModule* InModule);
	bool RemoveModule(UParticleModule* InModule);

	// 동일 클래스 첫 번째 모듈을 찾는다.
	template<typename T>
	T* FindModuleByClass() const
	{
		for (UParticleModule* M : Modules)
		{
			if (T* Hit = Cast<T>(M))
			{
				return Hit;
			}
		}
		return nullptr;
	}
};
