#include "Component/Camera/CameraComponent.h"
#include "Math/Rotator.h"
#include "Object/Reflection/ObjectFactory.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/Camera/PlayerCameraManager.h"
#include "Render/Types/MinimalViewInfo.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Object/GarbageCollection.h"
#include "Texture/Texture2D.h"
#include <cmath>
#include <cstring>

void UCameraComponent::BeginPlay()
{
	Super::BeginPlay();

	// E.2/3: PC 가 BeginPlay 시점엔 아직 spawn 전 → PlayerCameraManager nullptr.
	// PC 의 BeginPlay 에서 World 의 모든 카메라 컴포넌트를 catch up 등록하므로 안전.
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APlayerCameraManager* CM = PC->GetPlayerCameraManager())
			{
				CM->RegisterCamera(this);
			}
		}
	}
}

void UCameraComponent::EndPlay()
{
	Super::EndPlay();
	if (UWorld* World = GetWorldEvenIfPendingKill())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APlayerCameraManager* CM = PC->GetPlayerCameraManager())
			{
				CM->UnregisterCamera(this);
			}
		}
	}
}

void UCameraComponent::PostEditProperty(const char* PropertyName)
{
	Super::PostEditProperty(PropertyName);

	if (std::strcmp(PropertyName, "PostProcessMaterialPath") == 0 ||
		std::strcmp(PropertyName, "Post Process Material") == 0)
	{
		SetPostProcessMaterialByPath(PostProcessMaterialPath.ToString());
	}
}

void UCameraComponent::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(PostProcessMaterial, "UCameraComponent::PostProcessMaterial");
}

void UCameraComponent::LookAt(const FVector& Target)
{
	FVector Position = GetWorldLocation();
	FVector Diff = (Target - Position).Normalized();

	constexpr float Rad2Deg = 180.0f / 3.14159265358979f;

	FRotator LookRotation = GetRelativeRotation();
	LookRotation.Pitch = -asinf(Diff.Z) * Rad2Deg;

	if (fabsf(Diff.Z) < 0.999f) {
		LookRotation.Yaw = atan2f(Diff.Y, Diff.X) * Rad2Deg;
	}

	SetRelativeRotation(LookRotation);
}

void UCameraComponent::OnResize(int32 Width, int32 Height)
{
	CameraState.AspectRatio = static_cast<float>(Width) / static_cast<float>(Height);
}

void UCameraComponent::SetCameraState(const FCameraState& NewState)
{
	CameraState = NewState;
}

void UCameraComponent::SetPostProcessMaterial(UMaterial* InMaterial)
{
	PostProcessMaterial = InMaterial;
	PostProcessMaterialPath = InMaterial ? InMaterial->GetAssetPathFileName() : FString("None");
}

UMaterial* UCameraComponent::GetPostProcessMaterial() const
{
	if (!IsValid(PostProcessMaterial) && !PostProcessMaterialPath.IsNull())
	{
		PostProcessMaterial = FMaterialManager::Get().GetOrCreateMaterial(PostProcessMaterialPath.ToString());
	}
	return PostProcessMaterial;
}

bool UCameraComponent::SetPostProcessMaterialByPath(const FString& MaterialPath)
{
	if (MaterialPath.empty() || MaterialPath == "None")
	{
		ClearPostProcessMaterial();
		return true;
	}

	UMaterial* LoadedMaterial = FMaterialManager::Get().GetOrCreateMaterial(MaterialPath);
	if (!LoadedMaterial)
	{
		return false;
	}

	SetPostProcessMaterial(LoadedMaterial);
	return true;
}

void UCameraComponent::ClearPostProcessMaterial()
{
	PostProcessMaterial = nullptr;
	PostProcessMaterialPath = "None";
}

bool UCameraComponent::SetPostProcessScalarParameter(const FString& ParamName, float Value)
{
	return IsValid(PostProcessMaterial) && PostProcessMaterial->SetScalarParameter(ParamName, Value);
}

bool UCameraComponent::SetPostProcessVectorParameter(const FString& ParamName, const FVector4& Value)
{
	return IsValid(PostProcessMaterial) && PostProcessMaterial->SetVector4Parameter(ParamName, Value);
}

bool UCameraComponent::SetPostProcessTextureParameter(const FString& ParamName, UTexture2D* Texture)
{
	return IsValid(PostProcessMaterial) && PostProcessMaterial->SetTextureParameter(ParamName, Texture);
}

void UCameraComponent::SetViewBob(float RollDegrees, float PitchDegrees, float LocalOffsetZ)
{
	ViewBobRollDegrees = RollDegrees;
	ViewBobPitchDegrees = PitchDegrees;
	ViewBobLocalOffsetZ = LocalOffsetZ;
}

void UCameraComponent::ClearViewBob()
{
	ViewBobRollDegrees = 0.0f;
	ViewBobPitchDegrees = 0.0f;
	ViewBobLocalOffsetZ = 0.0f;
}

void UCameraComponent::GetCameraView(float /*DeltaTime*/, FMinimalViewInfo& OutPOV) const
{
	UpdateWorldMatrix();
	OutPOV.Location    = GetWorldLocation();
	OutPOV.Rotation    = GetWorldMatrix().ToRotator();

	if (std::fabs(ViewBobRollDegrees) > 1.0e-6f || std::fabs(ViewBobPitchDegrees) > 1.0e-6f || std::fabs(ViewBobLocalOffsetZ) > 1.0e-6f)
	{
		OutPOV.Rotation.Roll += ViewBobRollDegrees;
		OutPOV.Rotation.Pitch += ViewBobPitchDegrees;
		OutPOV.Rotation = OutPOV.Rotation.GetNormalized();
		OutPOV.Location = OutPOV.Location + OutPOV.Rotation.ToMatrix().TransformVector(FVector(0.0f, 0.0f, ViewBobLocalOffsetZ));
	}

	OutPOV.FOV         = CameraState.FOV;
	OutPOV.AspectRatio = CameraState.AspectRatio;
	OutPOV.OrthoWidth  = CameraState.OrthoWidth;
	OutPOV.NearClip    = CameraState.NearZ;
	OutPOV.FarClip     = CameraState.FarZ;
	OutPOV.bIsOrtho    = CameraState.bIsOrthogonal;
}
