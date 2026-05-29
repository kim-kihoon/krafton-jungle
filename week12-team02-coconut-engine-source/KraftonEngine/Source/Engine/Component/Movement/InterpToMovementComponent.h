#pragma once
#include "MovementComponent.h"
#include "Math/Vector.h"
#include "InterpToMovementComponent.generated.h"

UENUM()
enum class EInterpBehaviour {
	OneShot,
	OneShotReverse,
	Loop,
	PingPong,
};

UCLASS()
class UInterpToMovementComponent : public UMovementComponent {
public:
	GENERATED_BODY(UInterpToMovementComponent)
	UInterpToMovementComponent() = default;

	// Overrides
	void				BeginPlay() override;
	void				TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

	// Control Point Management
	void				AddControlPoint(FVector InControlPoint);
	void				RemoveControlPoint(uint32 Index);
	TArray<FVector>& GetControlPoints() { return ControlPoints; }
	FVector& GetControlPoint(uint32 Index);
	void				SetControlPoint(uint32 Index, FVector InPoint);

	// Interpolation Duration
	float				GetInterpDuration() const { return Duration; }
	void				SetInterpDuration(float InDuration);

	// Interpolation behaviour
	EInterpBehaviour	GetInterpolationBehaviour() const { return InterpBehaviour; }
	void				SetInterpolationBehaviour(EInterpBehaviour InBehaviour);
	bool				IsFacingTargetDir() const { return bFaceTargetDir; }
	void				ShouldFaceTargetDir(bool InBool) { bFaceTargetDir = InBool; }

	// Misc
	void				Initiate();
	bool				IsAutoActivating() const { return bAutoActivate; }
	void				ShouldAutoActivate(bool bActivate) { bAutoActivate = bActivate; }
	void				Reset();
	void				ResetAndHalt();

private:
	// Used to lerp back and forth when Behaviour is set to PingPong
	void				Ping();
	void				Pong();

	// Determines what to do after reeaching the target destination inside the chain
	void				DestinationReached();

	// Determines what to do after a chain of interpolation has been ended
	void				EndOfChain();

	// Returns the ratio of the distance towards the next control point over the total distance
	void				SetNextDistRatio();

	// Determines the rotation speed proportional to distance ratio
	void				SetRotationSpeed();

	// Tick - Lerp updates
	void				UpdateLerp(float DeltaTime);

	// Rotate (interpolated) towards target direction if flagged.
	// Call before updating PointID
	void				FaceTargetDir(float DeltaTime);

private:
	UPROPERTY(Edit, Category="Movement", DisplayName="Interpolation Behaviour")
	EInterpBehaviour	InterpBehaviour = EInterpBehaviour::OneShot;

	UPROPERTY(Edit, Category="Movement", DisplayName="Control Points")
	TArray<FVector>		ControlPoints;

	UPROPERTY(Edit, Category="Movement", DisplayName = "Interp Duration", Min = 0.1, Max = 2048.0, Speed = 0.1)
	float				Duration = 5.0f;		// Does not store an "array" of duration

	UPROPERTY(Edit, Category="Movement", DisplayName = "Orient to Target Direction")
	bool				bFaceTargetDir = true;

	uint32				CurrentPointID = 0;
	uint32				NextPointID = 0;
	float				RotateDuration = 0.f;
	float				Elapsed = 0.f;
	float				TotalDistance = 0;
	float				NextDistRatio = 0;
	bool				bisLerping = false;
	bool				bPing = true;
	float				TargetPitch = 0.f;
	float				TargetYaw = 0.f;
};