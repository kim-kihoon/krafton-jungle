#pragma once

#include "GameFramework/AActor.h"
#include "Object/FName.h"
#include "Math/Vector.h"

#include "Object/Ptr/WeakObjectPtr.h"
#include "Source/Engine/GameFramework/Actor/TriggerVolumeBase.generated.h"

class UBoxComponent;
class UPrimitiveComponent;
class APawn;
struct FHitResult;

// ============================================================
// ATriggerVolumeBase - Possessed Pawn occupancy -> GameMode/derived trigger events
//
// Runtime intent:
//   1) BoxComponent is configured as overlap-only trigger volume
//   2) Begin/EndOverlap are treated as occupancy signals, not final truth
//   3) The base actor tracks which possessed pawns are actually inside
//   4) Derived classes consume real enter/exit transitions instead of raw overlap counts
// ============================================================
UCLASS()
class ATriggerVolumeBase : public AActor
{
public:
	GENERATED_BODY()
	ATriggerVolumeBase() = default;
	~ATriggerVolumeBase() override = default;

	void BeginPlay() override;

	void InitDefaultComponents(const FVector& Extent = FVector(1.0f, 1.0f, 1.0f));
	void PostDuplicate() override;

	virtual void OnPossessedPawnEntered(APawn* Pawn) {}
	virtual void OnPossessedPawnExited(APawn* Pawn) {}

	UFUNCTION(Pure, Category="Trigger")
	UBoxComponent* GetTriggerBox() const { return TriggerBox; }

	// 게임모드가 트리거 종류를 구분할 때 사용 (씬에 직렬화).
	UFUNCTION(Pure, Category="Trigger")
	FName GetTriggerTag() const { return TriggerTag; }
	UFUNCTION(Callable, Category="Trigger")
	void SetTriggerTag(const FName& InTag) { TriggerTag = InTag; }
	int32 GetOccupyingPawnCount() const { return static_cast<int32>(OccupyingPawns.size()); }

protected:
	APawn* ResolvePossessedPawn(AActor* OtherActor) const;
	bool IsPawnStillInsideTrigger(APawn* Pawn) const;
	bool AddOccupyingPawn(APawn* Pawn);
	bool RemoveOccupyingPawn(APawn* Pawn);

	void HandleBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	void HandleEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	TWeakObjectPtr<UBoxComponent> TriggerBox;

	UPROPERTY(Edit, Save, Category="Trigger", DisplayName="TriggerTag")
	FName TriggerTag;

	TSet<TWeakObjectPtr<APawn>> OccupyingPawns;
};
