#include "GameFramework/Pawn/LuaCharacter.h"

#include "Component/Camera/CameraComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Component/Script/LuaScriptComponent.h"
#include "Component/Camera/SpringArmComponent.h"
#include "Component/Light/PointLightComponent.h"
#include "Component/Light/SpotLightComponent.h"
#include "Component/Particle/ParticleSystemComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"

#include <algorithm>
#include <cmath>

namespace
{
	const FName MuzzleSocketName("Muzzle");
	constexpr float PistolMuzzleFlashAttenuationRadius = 8.0f;
	constexpr float PistolMuzzleFlashIntensity = 8.0f;

	bool IsNearlyZero(float Value)
	{
		return std::abs(Value) <= 1.0e-4f;
	}

	bool IsNearlyZeroVector(const FVector& Value)
	{
		return IsNearlyZero(Value.X) && IsNearlyZero(Value.Y) && IsNearlyZero(Value.Z);
	}

	bool IsHeldCameraMesh(UStaticMeshComponent* Component)
	{
		return Component && Component->GetStaticMeshPath() == FString("Content/Data/camera/camera_StaticMesh.uasset");
	}

	void ConfigurePistolMuzzleFlashPointLight(ALuaCharacter* Character, UPointLightComponent* Light)
	{
		if (!Character || !Light)
		{
			return;
		}

		if (USkeletalMeshComponent* MeshComponent = Character->GetMesh())
		{
			if (MeshComponent->HasSocket(MuzzleSocketName))
			{
				Light->AttachToComponent(MeshComponent, MuzzleSocketName);
			}
			else
			{
				Light->AttachToComponent(MeshComponent);
			}
		}
		else if (UCameraComponent* CameraComponent = Character->GetCamera())
		{
			Light->AttachToComponent(CameraComponent);
		}
		else if (USpringArmComponent* SpringArmComponent = Character->GetSpringArm())
		{
			Light->AttachToComponent(SpringArmComponent);
		}

		Light->SetRelativeLocation(FVector::ZeroVector);
		Light->SetRelativeRotation(FVector::ZeroVector);
		Light->SetIntensity(PistolMuzzleFlashIntensity);
		Light->SetLightColor(FVector4(1.0f, 0.90f, 0.72f, 1.0f));
		Light->SetAttenuationRadius(PistolMuzzleFlashAttenuationRadius);
		Light->SetCastShadows(false);
		Light->SetVisible(true);
	}
}

void ALuaCharacter::ConfigureFirstPersonViewRig()
{
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	bUseControllerRotationYaw = true;

	if (!CapsuleComponent || !SpringArm)
	{
		return;
	}

	SpringArm->AttachToComponent(CapsuleComponent);

	const FVector SpringArmRelativeLocation = SpringArm->GetRelativeLocation();
	const FVector ArmOffset = FVector(-SpringArm->TargetArmLength, 0.0f, 0.0f);
	const FVector CameraRelativeLocation = Camera ? Camera->GetRelativeLocation() : FVector(0.0f, 0.0f, 0.0f);

	FVector EyeOffset = SpringArmRelativeLocation + SpringArm->TargetOffset + ArmOffset + SpringArm->SocketOffset + CameraRelativeLocation;
	if (IsNearlyZeroVector(EyeOffset) || EyeOffset.Z < 0.5f)
	{
		EyeOffset = FVector(0.0f, 0.0f, 0.75f);
	}

	SpringArm->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
	SpringArm->SetRelativeRotation(FVector(0.0f, 0.0f, 0.0f));
	SpringArm->TargetArmLength = 0.0f;
	SpringArm->TargetOffset = EyeOffset;
	SpringArm->SocketOffset = FVector(0.0f, 0.0f, 0.0f);
	SpringArm->bEnableCameraLag = false;
	SpringArm->bEnableCameraRotationLag = false;
	SpringArm->bDoCollisionTest = false;
	SpringArm->bUsePawnControlRotation = true;
	SpringArm->bInheritPitch = true;  
	SpringArm->bInheritYaw = true;
	SpringArm->bInheritRoll = false;
	SpringArm->ResetLagState();

	if (Camera)
	{
		Camera->AttachToComponent(SpringArm);
		Camera->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
		Camera->SetRelativeRotation(FVector(0.0f, 0.0f, 0.0f));
	}

	if (USkeletalMeshComponent* MeshComponent = Mesh.Get())
	{
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		FVector DesiredRelativeLocation = MeshComponent->GetRelativeLocation();
		if (MeshComponent->GetParent() != SpringArm && DesiredRelativeLocation.Z > 0.5f)
		{
			DesiredRelativeLocation = DesiredRelativeLocation - EyeOffset;
		}
		MeshComponent->AttachToComponent(SpringArm);
		MeshComponent->SetRelativeLocation(DesiredRelativeLocation);
	}

	for (UActorComponent* Component : GetComponents())
	{
		USpotLightComponent* SpotLight = Cast<USpotLightComponent>(Component);
		if (!SpotLight)
		{
			continue;
		}

		FVector DesiredRelativeLocation = SpotLight->GetRelativeLocation();
		if (SpotLight->GetParent() != SpringArm && DesiredRelativeLocation.Z > 0.5f)
		{
			DesiredRelativeLocation = DesiredRelativeLocation - EyeOffset;
		}
		SpotLight->AttachToComponent(SpringArm);
		SpotLight->SetRelativeLocation(DesiredRelativeLocation);
	}

	for (UActorComponent* Component : GetComponents())
	{
		UStaticMeshComponent* StaticMesh = Cast<UStaticMeshComponent>(Component);
		if (!IsHeldCameraMesh(StaticMesh))
		{
			continue;
		}

		FVector DesiredRelativeLocation = StaticMesh->GetRelativeLocation();
		if (StaticMesh->GetParent() != SpringArm && DesiredRelativeLocation.Z > 0.5f)
		{
			DesiredRelativeLocation = DesiredRelativeLocation - EyeOffset;
		}
		StaticMesh->AttachToComponent(SpringArm);
		StaticMesh->SetRelativeLocation(DesiredRelativeLocation);
	}
}

void ALuaCharacter::InitDefaultComponents(const FString& SkeletalMeshFileName, const FString& ScriptFile)
{
	Super::InitDefaultComponents(SkeletalMeshFileName);

	// 1인칭 view pivot. 위치는 눈높이에 고정하고 control rotation은 회전으로만 적용한다.
	SpringArm = AddComponent<USpringArmComponent>();
	SpringArm->AttachToComponent(CapsuleComponent);
	SpringArm->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
	SpringArm->TargetArmLength       = 0.0f;
	SpringArm->SocketOffset          = FVector(0.0f, 0.0f, 0.0f);
	SpringArm->TargetOffset          = FVector(0.0f, 0.0f, 0.75f);
	SpringArm->bEnableCameraLag      = false;
	SpringArm->bEnableCameraRotationLag = false;

	// mouse look 이 capsule rotation 안 건드리고 카메라만 회전 — UE ThirdPerson 패턴.
	// ACharacter::Tick 이 APawn::ControlRotation 누적 → SpringArm 이 이걸 inherit.
	SpringArm->bUsePawnControlRotation = true;
	SpringArm->bInheritPitch           = true;
	SpringArm->bInheritYaw             = true;
	SpringArm->bInheritRoll            = false;

	Camera = AddComponent<UCameraComponent>();
	Camera->AttachToComponent(SpringArm);

	LuaScriptComponent = AddComponent<ULuaScriptComponent>();
	if (!ScriptFile.empty())
	{
		LuaScriptComponent->SetScriptFile(ScriptFile);
	}

	ConfigureFirstPersonViewRig();
}

void ALuaCharacter::PostDuplicate()
{
	Super::PostDuplicate();
	RefreshLuaCharacterComponentReferences();
	ConfigureFirstPersonViewRig();
}

void ALuaCharacter::OnPostLoad(FArchive& Ar)
{
	Super::OnPostLoad(Ar);
	RefreshLuaCharacterComponentReferences();
}

void ALuaCharacter::RefreshLuaCharacterComponentReferences()
{
	LuaScriptComponent = GetComponentByClass<ULuaScriptComponent>();
	SpringArm          = GetComponentByClass<USpringArmComponent>();
	Camera             = GetComponentByClass<UCameraComponent>();
}

void ALuaCharacter::BeginPlay()
{
	RefreshLuaCharacterComponentReferences();
	ConfigureFirstPersonViewRig();

	Super::BeginPlay();
	EnsurePistolMuzzleFlashLight();
	if (PistolMuzzleFlashParticle)
	{
		PistolMuzzleFlashParticle->SetCastShadow(false);
		PistolMuzzleFlashParticle->Deactivate();
	}
	SetPistolMuzzleFlashVisible(false);
}

void ALuaCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (PistolMuzzleFlashRemaining <= 0.0f)
	{
		return;
	}

	PistolMuzzleFlashRemaining -= DeltaTime;
	if (PistolMuzzleFlashRemaining <= 0.0f)
	{
		PistolMuzzleFlashRemaining = 0.0f;
		SetPistolMuzzleFlashVisible(false);
	}
}

void ALuaCharacter::PlayPistolFireEffect()
{
	EnsurePistolMuzzleFlashLight();
	if (PistolMuzzleFlashLight)
	{
		ConfigurePistolMuzzleFlashPointLight(this, PistolMuzzleFlashLight);
	}
	if (PistolMuzzleFlashParticle)
	{
		PistolMuzzleFlashParticle->SetCastShadow(false);
		PistolMuzzleFlashParticle->Activate(true);
	}
	PistolMuzzleFlashRemaining = PistolMuzzleFlashDuration;
	SetPistolMuzzleFlashVisible(true);
}

void ALuaCharacter::EnsurePistolMuzzleFlashLight()
{
	if (!PistolMuzzleFlashLight)
	{
		PistolMuzzleFlashLight = AddComponent<UPointLightComponent>();
	}

	if (!PistolMuzzleFlashLight)
	{
		return;
	}

	if (!bPistolMuzzleFlashLightInitialized)
	{
		ConfigurePistolMuzzleFlashPointLight(this, PistolMuzzleFlashLight);
		PistolMuzzleFlashConfiguredIntensity = PistolMuzzleFlashLight->GetIntensity();
		bPistolMuzzleFlashLightInitialized = true;
	}

	PistolMuzzleFlashLight->SetIntensity(0.0f);
}

void ALuaCharacter::SetPistolMuzzleFlashVisible(bool bVisible)
{
	if (PistolMuzzleFlashLight)
	{
		PistolMuzzleFlashLight->SetIntensity(bVisible ? PistolMuzzleFlashConfiguredIntensity : 0.0f);
	}
}
