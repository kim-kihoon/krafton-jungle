#pragma once

#include "Component/ActorComponent.h"
#include "Math/Vector.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/Component/Input/ActionComponent.generated.h"
class USceneComponent;

UCLASS()
class UActionComponent : public UActorComponent
{
public:
	GENERATED_BODY()
	UActionComponent() = default;
	~UActionComponent() override = default;

	void BeginPlay() override;
	void EndPlay() override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

	UFUNCTION(Callable, Category="Action")
	void HitStop(float Duration, float TimeDilation);
	UFUNCTION(Callable, Category="Action")
	void LocalHitStop(float Duration);
	UFUNCTION(Callable, Category="Action")
	void HitSquash(const FVector& SquashedScale, float SquashInDuration, float RecoverDuration);
	UFUNCTION(Callable, Category="Action")
	void Knockback(const FVector& Direction, float Distance, float Duration);
	UFUNCTION(Callable, Category="Action")
	void Slomo(float Duration, float TimeDilation);

	UFUNCTION(Callable, Category="Action")
	void StopHitStop();
	UFUNCTION(Callable, Category="Action")
	void StopLocalHitStop();
	UFUNCTION(Callable, Category="Action")
	void StopHitSquash();
	UFUNCTION(Callable, Category="Action")
	void StopKnockback();
	UFUNCTION(Callable, Category="Action")
	void StopSlomo();
	UFUNCTION(Callable, Category="Action")
	void StopAllActions();

private:
	float GetRawDeltaTime(float FallbackDeltaTime) const;
	USceneComponent* GetTargetSceneComponent() const;
	void UpdateTimeDilationRegistration();
	void RegisterTimeDilationComponent();
	void UnregisterTimeDilationComponent();
	static void RefreshGlobalTimeDilation();

	struct FTimedDilationAction
	{
		bool bActive = false;
		float Duration = 0.0f;
		float RemainingTime = 0.0f;
		float TimeDilation = 1.0f;
	};

	struct FHitSquashAction
	{
		bool bActive = false;
		float SquashInDuration = 0.0f;
		float RecoverDuration = 0.0f;
		float ElapsedTime = 0.0f;
		FVector StartScale = FVector::OneVector;
		FVector SquashedScale = FVector::OneVector;
	};

	struct FLocalHitStopAction
	{
		bool bActive = false;
		float Duration = 0.0f;
		float RemainingTime = 0.0f;
		bool bActorTickWasEnabled = true;
		TArray<TPair<TWeakObjectPtr<UActorComponent>, bool>> ComponentTickStates;
	};

	struct FKnockbackAction
	{
		bool bActive = false;
		float Duration = 0.0f;
		float RemainingTime = 0.0f;
		FVector RemainingOffset = FVector::ZeroVector;
	};

	bool HasActiveTimeDilationAction(const FTimedDilationAction& Action) const;
	bool HasActiveTimeDilation() const;

	FTimedDilationAction HitStopAction;
	FTimedDilationAction SlomoAction;
	FHitSquashAction HitSquashAction;
	FLocalHitStopAction LocalHitStopAction;
	FKnockbackAction KnockbackAction;

	static TArray<TWeakObjectPtr<UActionComponent>> TimeDilationComponents;
	static bool bHasCapturedGlobalBaseTimeDilation;
	static float GlobalBaseTimeDilation;
};
