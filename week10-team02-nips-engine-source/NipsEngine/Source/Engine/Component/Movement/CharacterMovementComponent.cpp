#include "Component/Movement/CharacterMovementComponent.h"

#include "Audio/AudioSystem.h"
#include "Component/Physics/RigidBodyComponent.h"
#include "Component/SceneComponent.h"
#include "Core/Logger.h"
#include "GameFramework/AActor.h"
#include "Object/ObjectFactory.h"
#include "Physics/JoltPhysicsSystem.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float GravityAcceleration = 9.8f;
	constexpr float DefaultJumpSpeed = 5.5f;
	constexpr float DefaultFootstepVolume = 0.8f;
	constexpr float DefaultFootstepStepDistance = 1.70f;
	constexpr float DefaultFootstepMinSpeed = 0.35f;
	constexpr float JumpSoundVolume = 3.0f;
	constexpr float LandingSoundVolume = 10.0f;

	const FString& GetFootstepPath(int32 Index)
	{
		static const FString Paths[] = {
			"Asset/Audio/Linoleum_Mono_01.WAV",
			"Asset/Audio/Linoleum_Mono_02.WAV",
			"Asset/Audio/Linoleum_Mono_03.WAV",
			"Asset/Audio/Linoleum_Mono_04.WAV",
			"Asset/Audio/Linoleum_Mono_05.WAV",
		};
		constexpr int32 Count = static_cast<int32>(sizeof(Paths) / sizeof(Paths[0]));
		return Paths[Index % Count];
	}

	const FString& GetJumpSoundPath()
	{
		static const FString Path = "Asset/Audio/Linoleum_Mono_02.WAV";
		return Path;
	}

	const FString& GetLandingSoundPath()
	{
		static const FString Path = "Asset/Audio/Linoleum_Mono_01.WAV";
		return Path;
	}
}

DEFINE_CLASS(UCharacterMovementComponent, UMovementComponent)
REGISTER_FACTORY(UCharacterMovementComponent)

UCharacterMovementComponent::UCharacterMovementComponent()
{
	bCanEverTick = true;
	Velocity = FVector::ZeroVector;
}

void UCharacterMovementComponent::BeginPlay()
{
	UMovementComponent::BeginPlay();
	RefreshUpdatedReferences();
	ClampEditableValues();
}

void UCharacterMovementComponent::Serialize(FArchive& Ar)
{
	UMovementComponent::Serialize(Ar);
	uint32 RigidBodyUUID = RigidBody ? RigidBody->GetUUID() : 0;
	Ar << "RigidBodyUUID" << RigidBodyUUID;
	Ar << "MaxWalkSpeed" << MaxWalkSpeed;
	Ar << "JumpSpeed" << JumpSpeed;
	Ar << "Acceleration" << Acceleration;
	Ar << "BrakingDeceleration" << BrakingDeceleration;
	Ar << "GravityScale" << GravityScale;
	Ar << "MaxFallSpeed" << MaxFallSpeed;
	Ar << "GroundStickVelocity" << GroundStickVelocity;
	Ar << "GroundProbeDistance" << GroundProbeDistance;
	Ar << "EnableFootsteps" << bEnableFootsteps;
	Ar << "FootstepVolume" << FootstepVolume;
	Ar << "FootstepStepDistance" << FootstepStepDistance;
	Ar << "FootstepMinSpeed" << FootstepMinSpeed;
	Ar << "SprintFootstepStepDistanceScale" << SprintFootstepStepDistanceScale;
	Ar << "SprintFootstepVolumeScale" << SprintFootstepVolumeScale;

	if (Ar.IsLoading())
	{
		const bool bLooksLikeLegacyFootstepDefaults =
			!bEnableFootsteps &&
			FootstepVolume <= 0.0f &&
			FootstepStepDistance <= 0.1001f &&
			FootstepMinSpeed <= 0.0f;
		if (bLooksLikeLegacyFootstepDefaults)
		{
			bEnableFootsteps = true;
			FootstepVolume = DefaultFootstepVolume;
			FootstepStepDistance = DefaultFootstepStepDistance;
			FootstepMinSpeed = DefaultFootstepMinSpeed;
		}
		if (JumpSpeed <= 0.0f)
		{
			JumpSpeed = DefaultJumpSpeed;
		}
		ClampEditableValues();
	}
}

void UCharacterMovementComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UMovementComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Velocity", EPropertyType::Vec3, &Velocity });
	OutProps.push_back({ "Max Walk Speed", EPropertyType::Float, &MaxWalkSpeed, 0.0f, 20.0f, 0.1f });
	OutProps.push_back({ "Jump Speed", EPropertyType::Float, &JumpSpeed, 0.0f, 30.0f, 0.1f });
	OutProps.push_back({ "Acceleration", EPropertyType::Float, &Acceleration, 0.0f, 100.0f, 0.5f });
	OutProps.push_back({ "Braking Deceleration", EPropertyType::Float, &BrakingDeceleration, 0.0f, 100.0f, 0.5f });
	OutProps.push_back({ "Gravity Scale", EPropertyType::Float, &GravityScale, 0.0f, 10.0f, 0.05f });
	OutProps.push_back({ "Max Fall Speed", EPropertyType::Float, &MaxFallSpeed, 0.0f, 100.0f, 0.5f });
	OutProps.push_back({ "Ground Stick Velocity", EPropertyType::Float, &GroundStickVelocity, -10.0f, 0.0f, 0.05f });
	OutProps.push_back({ "Ground Probe Distance", EPropertyType::Float, &GroundProbeDistance, 0.0f, 1.0f, 0.01f });
	OutProps.push_back({ "Enable Footsteps", EPropertyType::Bool, &bEnableFootsteps });
	OutProps.push_back({ "Footstep Volume", EPropertyType::Float, &FootstepVolume, 0.0f, 2.0f, 0.05f });
	OutProps.push_back({ "Footstep Step Distance", EPropertyType::Float, &FootstepStepDistance, 0.1f, 5.0f, 0.05f });
	OutProps.push_back({ "Footstep Min Speed", EPropertyType::Float, &FootstepMinSpeed, 0.0f, 5.0f, 0.05f });
	OutProps.push_back({ "Sprint Footstep Step Distance Scale", EPropertyType::Float, &SprintFootstepStepDistanceScale, 1.0f, 4.0f, 0.05f });
	OutProps.push_back({ "Sprint Footstep Volume Scale", EPropertyType::Float, &SprintFootstepVolumeScale, 1.0f, 2.0f, 0.05f });
}

void UCharacterMovementComponent::TickComponent(float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		ConsumeInputVector();
		return;
	}

	RefreshUpdatedReferences();
	if (UpdatedComponent == nullptr)
	{
		ConsumeInputVector();
		return;
	}

	ClampEditableValues();

	const bool bWasGrounded = bGrounded;
	FVector Input = ConsumeInputVector();
	Input.Z = 0.0f;
	if (Input.SizeSquared() > 1.0f)
	{
		Input = Input.GetSafeNormal();
	}

	const FVector DesiredHorizontalVelocity = Input * MaxWalkSpeed * SpeedMultiplier;
	const float HorizontalStep = (Input.IsNearlyZero() ? BrakingDeceleration : Acceleration) * DeltaTime;
	Velocity.X = MoveToward(Velocity.X, DesiredHorizontalVelocity.X, HorizontalStep);
	Velocity.Y = MoveToward(Velocity.Y, DesiredHorizontalVelocity.Y, HorizontalStep);

	if (bGrounded && Velocity.Z <= 0.0f)
	{
		Velocity.Z = 0.0f;
	}
	else
	{
		Velocity.Z -= GravityAcceleration * GravityScale * DeltaTime;
		Velocity.Z = std::max(Velocity.Z, -MaxFallSpeed);
	}

	const FVector StartLocation = UpdatedComponent->GetWorldLocation();
	FVector TargetLocation = StartLocation + Velocity * DeltaTime;
	const FVector RequestedDelta = TargetLocation - StartLocation;

	bool bMovedByCharacter = false;
	if (RigidBody != nullptr && FJoltPhysicsSystem::Get().IsInitialized())
	{
		FVector ResolvedLocation = TargetLocation;
		FVector ResolvedVelocity = Velocity;
		bool bResolvedGrounded = false;
		bMovedByCharacter = FJoltPhysicsSystem::Get().MoveCharacter(
			RigidBody,
			Velocity,
			DeltaTime,
			GroundProbeDistance,
			ResolvedLocation,
			ResolvedVelocity,
			bResolvedGrounded);
		if (bMovedByCharacter)
		{
			TargetLocation = ResolvedLocation;
			Velocity = ResolvedVelocity;
			bGrounded = bResolvedGrounded;
		}
	}

	UpdatedComponent->SetWorldLocation(TargetLocation);

	const FVector ActualDelta = TargetLocation - StartLocation;
	if (!bMovedByCharacter)
	{
		const bool bHitGround = RequestedDelta.Z < -0.0001f && ActualDelta.Z > RequestedDelta.Z + 0.001f;
		bGrounded = bHitGround;
	}
	if (bGrounded && Velocity.Z < 0.0f)
	{
		Velocity.Z = 0.0f;
	}

	if (RequestedDelta.X != 0.0f && std::fabs(ActualDelta.X) < std::fabs(RequestedDelta.X) * 0.5f)
	{
		Velocity.X = 0.0f;
	}
	if (RequestedDelta.Y != 0.0f && std::fabs(ActualDelta.Y) < std::fabs(RequestedDelta.Y) * 0.5f)
	{
		Velocity.Y = 0.0f;
	}

	if (!bWasGrounded && bGrounded)
	{
		FootstepAccumulatedDistance = 0.0f;
		PlayLandingSound();
	}

	UpdateFootsteps(DeltaTime, ActualDelta);
}

void UCharacterMovementComponent::RefreshUpdatedReferences()
{
	if (Owner == nullptr)
	{
		UpdatedComponent = nullptr;
		RigidBody = nullptr;
		return;
	}

	if (UpdatedComponent == nullptr)
	{
		UpdatedComponent = Owner->GetRootComponent();
	}

	if (RigidBody == nullptr)
	{
		for (UActorComponent* Component : Owner->GetComponents())
		{
			if (URigidBodyComponent* Body = Cast<URigidBodyComponent>(Component))
			{
				RigidBody = Body;
				break;
			}
		}
	}
}

void UCharacterMovementComponent::Jump()
{
	if (!bGrounded)
	{
		return;
	}

	Velocity.Z = std::max(Velocity.Z, JumpSpeed);
	bGrounded = false;
	FootstepAccumulatedDistance = 0.0f;
	PlayJumpSound();
}

void UCharacterMovementComponent::SetSpeedMultiplier(float InSpeedMultiplier)
{
	SpeedMultiplier = std::max(0.0f, InSpeedMultiplier);
}

void UCharacterMovementComponent::ClampEditableValues()
{
	MaxWalkSpeed = std::max(0.0f, MaxWalkSpeed);
	JumpSpeed = std::max(0.0f, JumpSpeed);
	SpeedMultiplier = std::max(0.0f, SpeedMultiplier);
	Acceleration = std::max(0.0f, Acceleration);
	BrakingDeceleration = std::max(0.0f, BrakingDeceleration);
	GravityScale = std::max(0.0f, GravityScale);
	MaxFallSpeed = std::max(0.0f, MaxFallSpeed);
	GroundStickVelocity = std::min(0.0f, GroundStickVelocity);
	GroundProbeDistance = std::max(0.08f, GroundProbeDistance);
	FootstepVolume = std::clamp(FootstepVolume, 0.0f, 2.0f);
	FootstepStepDistance = std::max(0.1f, FootstepStepDistance);
	FootstepMinSpeed = std::max(0.0f, FootstepMinSpeed);
	SprintFootstepStepDistanceScale = std::max(1.0f, SprintFootstepStepDistanceScale);
	SprintFootstepVolumeScale = std::max(1.0f, SprintFootstepVolumeScale);
}

float UCharacterMovementComponent::MoveToward(float Current, float Target, float MaxDelta) const
{
	if (std::fabs(Target - Current) <= MaxDelta)
	{
		return Target;
	}

	return Current + (Target > Current ? MaxDelta : -MaxDelta);
}

void UCharacterMovementComponent::UpdateFootsteps(float DeltaTime, const FVector& ActualDelta)
{
	if (!bEnableFootsteps || DeltaTime <= 0.0f || !bGrounded)
	{
		FootstepAccumulatedDistance = 0.0f;
		return;
	}

	const FVector HorizontalDelta(ActualDelta.X, ActualDelta.Y, 0.0f);
	const float HorizontalDistance = HorizontalDelta.Size();
	const float HorizontalSpeed = HorizontalDistance / DeltaTime;
	const bool bNotFallingFast = Velocity.Z > -0.5f;

	if (!bNotFallingFast || HorizontalSpeed < FootstepMinSpeed)
	{
		FootstepAccumulatedDistance = 0.0f;
		return;
	}

	const bool bSprinting = SpeedMultiplier > 1.05f;
	const float CurrentStepDistance = FootstepStepDistance * (bSprinting ? SprintFootstepStepDistanceScale : 1.0f);
	const float CurrentVolumeScale = bSprinting ? SprintFootstepVolumeScale : 1.0f;

	FootstepAccumulatedDistance += HorizontalDistance;
	while (FootstepAccumulatedDistance >= CurrentStepDistance)
	{
		FootstepAccumulatedDistance -= CurrentStepDistance;
		PlayFootstep(CurrentVolumeScale);
	}
}

void UCharacterMovementComponent::PlayFootstep(float VolumeScale)
{
	const FString& FootstepPath = GetFootstepPath(FootstepIndex);
	FAudioPlayParams Params;
	Params.bSpatial = false;
	Params.bLoop = false;
	Params.bAffectedByAudioZones = false;
	Params.Bus = EAudioBus::SFX;
	Params.Volume = std::clamp(FootstepVolume * VolumeScale, 0.0f, 2.0f);
	FAudioSystem::Get().Play(FootstepPath, Params);
	++FootstepIndex;
}

void UCharacterMovementComponent::PlayJumpSound()
{
	FAudioPlayParams Params;
	Params.bSpatial = false;
	Params.bLoop = false;
	Params.bAffectedByAudioZones = false;
	Params.bAllowVolumeBoost = true;
	Params.Bus = EAudioBus::SFX;
	Params.Volume = JumpSoundVolume;
	FAudioSystem::Get().Play(GetJumpSoundPath(), Params);
}

void UCharacterMovementComponent::PlayLandingSound()
{
	FAudioPlayParams Params;
	Params.bSpatial = false;
	Params.bLoop = false;
	Params.bAffectedByAudioZones = false;
	Params.bAllowVolumeBoost = true;
	Params.Bus = EAudioBus::SFX;
	Params.Volume = LandingSoundVolume;
	FAudioSystem::Get().Play(GetLandingSoundPath(), Params);
}
