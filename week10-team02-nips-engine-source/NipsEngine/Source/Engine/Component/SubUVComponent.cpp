#include "SubUVComponent.h"

#include <cmath>
#include <cstring>
#include "Engine/Viewport/ViewportCamera.h"
#include "Core/ResourceManager.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Component/CameraComponent.h"
#include "Math/Utils.h"

DEFINE_CLASS(USubUVComponent, UPrimitiveComponent)
REGISTER_FACTORY(USubUVComponent)

USubUVComponent::USubUVComponent()
{
	SetVisibility(true);
}

void USubUVComponent::PostDuplicate(UObject* Original)
{
    UPrimitiveComponent::PostDuplicate(Original);

    const USubUVComponent* Orig = Cast<USubUVComponent>(Original);
    bIsBillboard = Orig->bIsBillboard;
    ParticleName = Orig->ParticleName;
    CachedParticle = Orig->CachedParticle;
    FrameIndex = Orig->FrameIndex;
    Width = Orig->Width;
    Height = Orig->Height;
    PlayRate = Orig->PlayRate;
    TimeAccumulator = Orig->TimeAccumulator;
    bLoop = Orig->bLoop;
    bIsExecute = Orig->bIsExecute;
    TintColor = Orig->TintColor;
}

void USubUVComponent::Serialize(FArchive& Ar)
{
	UPrimitiveComponent::Serialize(Ar);
	Ar << "Particle" << ParticleName;
	Ar << "Width" << Width;
	Ar << "Height" << Height;
	Ar << "PlayRate" << PlayRate;
	Ar << "bLoop" << bLoop;

	if (Ar.IsLoading())
	{
		SetParticle(ParticleName);
	}
}

bool USubUVComponent::TryGetActiveCamera(const FViewportCamera*& OutCamera) const
{
	OutCamera = nullptr;

	if (GetOwner() == nullptr || GetOwner()->GetFocusedWorld() == nullptr)
	{
		return false;
	}

	OutCamera = GetOwner()->GetFocusedWorld()->GetActiveCamera();
	return OutCamera != nullptr;
}

FMatrix USubUVComponent::MakeBillboardWorldMatrix(
	const FVector& WorldLocation,
	const FVector& WorldScale,
	const FVector& CameraForward,
	const FVector& CameraRight,
	const FVector& CameraUp)
{
	FVector Forward = CameraForward.GetSafeNormal();
	FVector Right = (-CameraRight).GetSafeNormal();
	FVector Up = CameraUp.GetSafeNormal();

	if (Forward.IsNearlyZero())
	{
		Forward = FVector(-1.0f, 0.0f, 0.0f);
	}

	if (Right.IsNearlyZero() || Up.IsNearlyZero())
	{
		FVector FallbackUp = FVector::UpVector;
		if (std::abs(FVector::DotProduct(Forward, FallbackUp)) > 0.99f)
		{
			FallbackUp = FVector::RightVector;
		}

		Right = FVector::CrossProduct(FallbackUp, Forward).GetSafeNormal();
		Up = FVector::CrossProduct(Forward, Right).GetSafeNormal();
	}

	FMatrix BillboardMatrix = FMatrix::Identity;
	BillboardMatrix.SetAxes(
		Forward * WorldScale.X,
		Right * WorldScale.Y,
		Up * WorldScale.Z,
		WorldLocation);
	return BillboardMatrix;
}

void USubUVComponent::SetParticle(const FName& InParticleName)
{
	ParticleName = InParticleName;
	CachedParticle = FResourceManager::Get().FindParticle(InParticleName);
}

void USubUVComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UPrimitiveComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Particle", EPropertyType::Name, &ParticleName });
	OutProps.push_back({ "Width", EPropertyType::Float, &Width, 0.1f, 100.0f, 0.1f });
	OutProps.push_back({ "Height", EPropertyType::Float, &Height, 0.1f, 100.0f, 0.1f });
	OutProps.push_back({ "Play Rate", EPropertyType::Float, &PlayRate, 1.0f, 120.0f, 1.0f });
	OutProps.push_back({ "bLoop", EPropertyType::Bool, &bLoop });
}

void USubUVComponent::PostEditProperty(const char* PropertyName)
{
	if (strcmp(PropertyName, "Particle") == 0)
	{
		SetParticle(ParticleName);
	}
}

void USubUVComponent::UpdateWorldAABB() const
{
	WorldAABB.Reset();

	const FViewportCamera* Camera = nullptr;

	if (TryGetActiveCamera(Camera) && Camera != nullptr)
	{
		CachedWorldMatrix = MakeBillboardWorldMatrix(GetWorldLocation(),
			GetWorldScale(),
			Camera->GetEffectiveForward(),
			Camera->GetEffectiveRight(),
			Camera->GetEffectiveUp());
	}
	else
	{
		CachedWorldMatrix = MakeBillboardWorldMatrix(GetWorldLocation(),
			GetWorldScale(),
			FVector(1.0f, 0.0f, 0.0f),
			FVector(0.0f, 1.0f, 0.0f),
			FVector(0.0f, 0.0f, 1.0f));
	}

	FVector LExt = { 0.01f, Width * 0.5f, Height * 0.5f };

	float NewEx = std::abs(CachedWorldMatrix.M[0][0]) * LExt.X +
		std::abs(CachedWorldMatrix.M[1][0]) * LExt.Y +
		std::abs(CachedWorldMatrix.M[2][0]) * LExt.Z;

	float NewEy = std::abs(CachedWorldMatrix.M[0][1]) * LExt.X +
		std::abs(CachedWorldMatrix.M[1][1]) * LExt.Y +
		std::abs(CachedWorldMatrix.M[2][1]) * LExt.Z;

	float NewEz = std::abs(CachedWorldMatrix.M[0][2]) * LExt.X +
		std::abs(CachedWorldMatrix.M[1][2]) * LExt.Y +
		std::abs(CachedWorldMatrix.M[2][2]) * LExt.Z;

	FVector WorldCenter = GetWorldLocation();
	const FVector Min = WorldCenter - FVector(NewEx, NewEy, NewEz);
	const FVector Max = WorldCenter + FVector(NewEx, NewEy, NewEz);

	WorldAABB.Expand(Min);
	WorldAABB.Expand(Max);
}

bool USubUVComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
{
	FMatrix BillboardWorldMatrix = GetWorldMatrix();
	const FViewportCamera* ActiveCamera = nullptr;
	if (TryGetActiveCamera(ActiveCamera))
	{
		BillboardWorldMatrix = MakeBillboardWorldMatrix(
			GetWorldLocation(),
			GetWorldScale(),
			ActiveCamera->GetEffectiveForward(),
			ActiveCamera->GetEffectiveRight(),
			ActiveCamera->GetEffectiveUp());
	}

	const FMatrix InvWorld = BillboardWorldMatrix.GetInverse();

	FRay LocalRay;
	LocalRay.Origin = InvWorld.TransformPosition(Ray.Origin);
	LocalRay.Direction = InvWorld.TransformVector(Ray.Direction);
	LocalRay.Direction.NormalizeSafe();

	if (std::abs(LocalRay.Direction.X) < MathUtil::Epsilon)
	{
		return false;
	}

	const float T = -LocalRay.Origin.X / LocalRay.Direction.X;
	if (T < 0.0f)
	{
		return false;
	}

	const FVector HitLocal = LocalRay.Origin + LocalRay.Direction * T;
	const float HalfW = Width * 0.5f;
	const float HalfH = Height * 0.5f;

	if (HitLocal.Y < -HalfW || HitLocal.Y > HalfW || HitLocal.Z < -HalfH || HitLocal.Z > HalfH)
	{
		return false;
	}

	const FVector HitWorld = BillboardWorldMatrix.TransformPosition(HitLocal);

	OutHitResult.bHit = true;
	OutHitResult.HitComponent = this;
	OutHitResult.Distance = FVector::Distance(Ray.Origin, HitWorld);
	OutHitResult.Location = HitWorld;
	OutHitResult.Normal = BillboardWorldMatrix.GetForwardVector();
	OutHitResult.FaceIndex = 0;
	return true;
}

void USubUVComponent::TickComponent(float DeltaTime)
{
	UpdateWorldAABB();

	if (!CachedParticle) return;
	if (!bLoop && bIsExecute) return;

	const uint32 TotalFrames = CachedParticle->Columns * CachedParticle->Rows;
	if (TotalFrames == 0) return;

	TimeAccumulator += DeltaTime;
	const float FrameDuration = 1.0f / PlayRate;
	while (TimeAccumulator >= FrameDuration)
	{
		TimeAccumulator -= FrameDuration;

		if (bLoop)
		{
			bIsExecute = false;
			FrameIndex = (FrameIndex + 1) % TotalFrames;
		}
		else
		{
			if (FrameIndex < TotalFrames - 1)
			{
				FrameIndex++;
			}
			else
			{
				bIsExecute = true;
				TimeAccumulator = 0.0f;
				break;
			}
		}
	}
}
