#pragma once

#include "Object/Object.h"
#include "Particle/ParticleHelper.h"

#include "Source/Engine/Particle/ParticleModule.generated.h"

class FParticleEmitterInstance;
class UParticleLODLevel;
struct FBaseParticle;

// =============================================================================
// UParticleModule
//   모든 파티클 모듈의 베이스. 모듈은 (1) 새로 생성된 입자에 1회 적용되는
//   Spawn 동작, (2) 매 프레임 활성 입자에 적용되는 Update 동작을 갖는다.
//   추가로 입자별 payload 바이트 (RequiredBytes) 와 emitter 인스턴스 전역
//   payload (RequiredBytesPerInstance) 를 요청할 수 있다.
//
//   [Distribution 평가 시간 규칙]
//   - Initial 계열 모듈은 Spawn()에서 SpawnTime을 Distribution에 넘긴다.
//     SpawnTime은 현재 emitter loop 안에서 이 particle이 생성된 시간(seconds)이다.
//   - Over-Life 계열 모듈은 Update()에서 Particle->RelativeTime을 Distribution에 넘긴다.
//     RelativeTime은 particle 생명 기준 0..1 정규화 시간이다.
//   - Spawn Rate처럼 emitter 전체 흐름을 따르는 값은 CurrentLoopTimeSeconds 계열을 쓴다.
//   - FrameRate처럼 초 단위 진행이 필요한 값은 particle age seconds를 별도로 계산해서 쓴다.
//
//   Emitter::CacheEmitterModuleInfo() 가 모든 모듈을 훑어 ParticleSize 와
//   ModuleOffsetMap 을 계산한다. 모듈 자신은 layout 을 모르고, 호출 시점에
//   주어진 Offset 만 사용한다.
// =============================================================================
UCLASS()
class UParticleModule : public UObject
{
public:
	GENERATED_BODY()
	UParticleModule() = default;
	~UParticleModule() override = default;

	void PostDuplicate() override;

	// --- 카테고리/식별 ----------------------------------------------------------
	// 카테고리 enum (Required/Spawn/Lifetime/...) — 에디터 그룹화 & 동일 카테고리
	enum class EModuleCategory : uint8
	{
		None,
		Required,
		TypeData,
		Spawn,
		Lifetime,
		Location,
		Velocity,
		Acceleration,
		Color,
		Size,
		Rotation,
		Collision,
		Event,
		SubUV,
		Beam,
	};
	virtual EModuleCategory GetCategory() const { return EModuleCategory::None; }

	// 에디터에 표시될 이름 (한국어 OK). 기본은 GetClass()->GetName().
	virtual const char* GetDisplayName() const { return "Module"; }

	// 같은 LODLevel 안에서 단 1개만 허용되는 모듈인지 (Required, TypeData 등).
	virtual bool IsUnique() const { return false; }

	// --- 라이프사이클 동작 ------------------------------------------------------

	// 입자가 막 생성된 직후 1회 호출.
	//   Owner          : 호출한 emitter 인스턴스
	//   ModuleOffset   : 이 모듈의 payload 가 BaseParticle 끝에서 시작하는 byte offset
	//                    (모듈이 RequiredBytes()>0 일 때만 의미가 있음)
	//   SpawnTime      : 현재 emitter loop 안에서 이 particle이 생성된 시간(seconds).
	//                    Initial Distribution / Curve 평가용 시간이며,
	//                    Particle->RelativeTime 이 아니다.
	//                    Over-Life 계열 모듈은 SpawnTime 대신 Particle->RelativeTime을 사용한다.
	//   Particle       : 초기화 대상 입자 (BaseParticle 헤더, 뒤에 payload)
	virtual void Spawn(FParticleEmitterInstance* Owner, uint32 ModuleOffset,
	                   float SpawnTime, FBaseParticle* Particle) {}

	// 매 프레임 활성 입자 전체에 대해 호출.
	//   Owner          : 호출한 emitter 인스턴스
	//   ModuleOffset   : 위와 동일
	//   DeltaTime      : 프레임 델타 (seconds)
	virtual void Update(FParticleEmitterInstance* Owner, uint32 ModuleOffset,
	                    float DeltaTime) {}

	// Spawn-time simulation LOD continuity를 위해, runtime은 active particle 전체를
	// current emitter LOD 하나로 update하지 않고 particle subset을 해당 particle의
	// spawn-time LOD contract로 dispatch할 수 있다. 기본 구현은 subset을 순회하며
	// UpdateParticle()를 호출한다.
	virtual void UpdateParticle(FParticleEmitterInstance* Owner, UParticleLODLevel* SimulationLOD,
	                            uint32 ModuleOffset, float DeltaTime, FBaseParticle* Particle) {}
	virtual void UpdateParticleSubset(FParticleEmitterInstance* Owner, UParticleLODLevel* SimulationLOD,
	                                  uint32 ModuleOffset, float DeltaTime,
	                                  const TArray<uint32>& ParticleIndices);

	// 시뮬레이션 시작 시 1회 (LODLevel 활성화 시점).
	virtual void FinalUpdate(FParticleEmitterInstance* Owner, uint32 ModuleOffset,
	                         float DeltaTime) {}

	// --- 메모리 요구량 ----------------------------------------------------------

	// 입자 1개당 추가로 요구하는 payload 바이트 수.
	virtual uint32 RequiredBytes(UParticleLODLevel* LODLevel) const { return 0; }

	// EmitterInstance 단위 추가 메모리 (모듈 전역 state). 매 입자에 추가되지 않음.
	virtual uint32 RequiredBytesPerInstance() const { return 0; }

	// --- 정합성/기본값 ----------------------------------------------------------

	// 모듈 추가 직후 호출 — 합리적 기본값을 채운다. 에디터에서도 호출.
	virtual void SetToSensibleDefaults(class UParticleEmitter* Owner) {}

	// LOD 변경 시 (LOD 0 의 모듈로부터 본인 LOD 값 보간/추출).
	virtual void RefreshFromLOD0(const UParticleModule* SourceModule) {}

	// 에디터 enable 토글. 비활성 모듈은 Spawn/Update skip.
	bool IsEnabled() const { return bEnabled; }
	void SetEnabled(bool b) { bEnabled = b; }

protected:
	UPROPERTY(Edit, Save, Category = "Module", DisplayName = "Enabled")
	bool bEnabled = true;
};
