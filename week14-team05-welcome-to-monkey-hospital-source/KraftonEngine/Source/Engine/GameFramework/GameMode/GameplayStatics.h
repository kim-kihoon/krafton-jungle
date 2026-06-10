#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"

class AActor;
class UWorld;
class FName;
class UParticleSystem;
class UParticleSystemComponent;

// ============================================================
// FGameplayStatics — 게임플레이 측에서 자주 쓰는 검색/유틸 모음 (UE 의 UGameplayStatics 대응).
//
// 범용 액터 쿼리는 여기에 모아두고, 게임-specific (예: AGameModeCarGame 한정 헬퍼) 은
// 해당 GameMode/State 에 둔다. 모든 함수는 static — 인스턴스화 불필요.
// ============================================================
class FGameplayStatics
{
public:
	// Tag 검색 — AActor::HasTag(InTag) 가 true 인 액터들. 큰 월드에서 매 frame 호출은
	// 권장 안 함 (선형 스캔). 결과 캐싱 또는 BeginPlay 1회 lookup 후 보관 패턴 추천.
	static AActor* FindFirstActorByTag(const UWorld* World, const FName& Tag);
	static TArray<AActor*> FindActorsByTag(const UWorld* World, const FName& Tag);

	// --- 파티클 스폰 (UE UGameplayStatics::SpawnEmitterAtLocation 대응) ---
	// AParticleSystemActor 를 월드에 스폰해 Template 을 세팅·활성화하고 그 PSC 를 반환한다.
	// 수명은 호출자 책임 — finite 시스템은 완료 시 스스로 비활성화(bActive=false)되며,
	// 정리가 필요하면 호출자/Notify 가 PSC->GetOwner() 를 DestroyActor 한다.
	// (PSC tick / OnSystemFinished 콜백 중 destroy 는 use-after-free 위험이라 헬퍼는 auto-destroy 안 함.)
	static UParticleSystemComponent* SpawnEmitterAtLocation(UWorld* World, UParticleSystem* Template,
		const FVector& Location, const FRotator& Rotation = FRotator(), bool bActivate = true);
	// 경로 오버로드 — FParticleSystemManager 로 Template 로드 후 위 함수 호출.
	static UParticleSystemComponent* SpawnEmitterAtLocation(UWorld* World, const FString& TemplatePath,
		const FVector& Location, const FRotator& Rotation = FRotator(), bool bActivate = true);
};
