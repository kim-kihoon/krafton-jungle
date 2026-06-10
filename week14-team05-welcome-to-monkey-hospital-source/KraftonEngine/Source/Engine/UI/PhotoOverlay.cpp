#include "UI/PhotoOverlay.h"

#include "Audio/AudioManager.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Primitive/PhotoPolaroidComponent.h"
#include "Component/Primitive/SkinnedMeshComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Math/Matrix.h"
#include "Math/Quat.h"
#include "Math/Rotator.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Object/Object.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "Platform/Paths.h"
#include "Render/Types/MinimalViewInfo.h"
#include "WICTextureLoader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>

namespace
{
	constexpr float PhotoEjectSeconds = 1.0f;
	constexpr float PhotoSpawnDelaySeconds = 0.25f;
	constexpr float PhotoFlashSeconds = 0.2f;
	constexpr float PhotoDevelopSeconds = 1.0f;
	constexpr float DefaultFrameAspectRatio = 1672.0f / 941.0f;
	constexpr const char* HeldCameraMeshPath = "Content/Data/camera/camera_StaticMesh.uasset";
	constexpr const char* HeldCameraMeshFileName = "camera_StaticMesh.uasset";
	constexpr const char* PhotoGhostReplacementTargetTagName = "PhotoGhostReplacementTarget";
	constexpr const char* PhotoGhostReplacementActorTagName = "PhotoGhostReplacementActor";
	constexpr const char* PhotoBoneTwistTargetTagName = "PhotoBoneTwistTarget";
	constexpr float PhotoBoneTwistMaxDegrees = 35.0f;
	constexpr float PhotoBoneTwistRootMaxDegrees = 8.0f;
	constexpr uint32 PhotoBoneTwistRandomModulus = 2147483647u;
	constexpr uint32 PhotoBoneTwistRandomMultiplier = 48271u;
	constexpr float HeldCameraPhotoForwardOffset = 0.06f;
	constexpr float HeldCameraPhotoRightOffset = 0.0f;
	constexpr float HeldCameraPhotoBaseUpOffset = 0.0f;
	constexpr float HeldCameraPhotoEjectUpDistance = 0.19f;
	constexpr const char* CameraShutterAudioKey = "CameraShutter";
	constexpr const char* PhotoOutAudioKey = "PhotoOut";
	constexpr float CameraPhotoAudioVolume = 0.5f;

	bool bCaptureRequested = false;
	bool bCaptureBlackoutRequested = false;
	bool bPhotoSpawnPending = false;
	float PhotoSpawnDelayRemaining = 0.0f;
	float FlashTime = PhotoFlashSeconds;
	float DisplayTime = 0.0f;
	float DevelopTime = 0.0f;
	uint32 CapturedWidth = 0;
	uint32 CapturedHeight = 0;
	ID3D11Texture2D* CapturedTexture = nullptr;
	ID3D11ShaderResourceView* CapturedSRV = nullptr;
	uint32 FrameWidth = 0;
	uint32 FrameHeight = 0;
	ID3D11ShaderResourceView* FrameSRV = nullptr;
	struct FCaptureActorVisibilityState
	{
		TWeakObjectPtr<AActor> Actor;
		bool bWasVisible = false;
	};
	TArray<FCaptureActorVisibilityState> CaptureActorVisibilityStates;
	struct FCaptureComponentVisibilityState
	{
		TWeakObjectPtr<UPrimitiveComponent> Component;
		bool bWasVisible = false;
	};
	TArray<FCaptureComponentVisibilityState> CaptureComponentVisibilityStates;
	struct FPhotoBoneTwistPoseState
	{
		TWeakObjectPtr<USkinnedMeshComponent> Mesh;
		TArray<FTransform> LocalPose;
	};
	TArray<FPhotoBoneTwistPoseState> PhotoBoneTwistPoseStates;
	uint32 PhotoBoneTwistRandomState = 0;
	TWeakObjectPtr<UWorld> PendingCaptureWorld;
	FName PendingCaptureExcludeActorTag = FName::None;
	TWeakObjectPtr<AActor> PhotoActor;
	TWeakObjectPtr<UPhotoPolaroidComponent> PhotoComponent;
	TWeakObjectPtr<UStaticMeshComponent> HeldCameraMeshComponent;
	bool bCaptureWorldStatePrepared = false;

	std::filesystem::path ToProjectPath(const FString& Path)
	{
		std::filesystem::path Result(FPaths::ToWide(Path));
		if (Result.is_relative())
		{
			Result = std::filesystem::path(FPaths::RootDir()) / Result;
		}
		return Result;
	}

	float Clamp01(float Value)
	{
		return (std::max)(0.0f, (std::min)(1.0f, Value));
	}

	float EaseOutCubic(float Alpha)
	{
		const float InvAlpha = 1.0f - Clamp01(Alpha);
		return 1.0f - InvAlpha * InvAlpha * InvAlpha;
	}

	uint32 MakePhotoBoneTwistSeed()
	{
		using Clock = std::chrono::high_resolution_clock;
		const int64 Now = static_cast<int64>(Clock::now().time_since_epoch().count());
		const uint32 Seed = static_cast<uint32>(Now ^ (Now >> 32));
		return Seed == 0 ? 1u : Seed;
	}

	float DrawPhotoBoneTwistRandomUnit()
	{
		if (PhotoBoneTwistRandomState == 0)
		{
			PhotoBoneTwistRandomState = MakePhotoBoneTwistSeed();
		}

		PhotoBoneTwistRandomState = static_cast<uint32>(
			(static_cast<uint64>(PhotoBoneTwistRandomState) * PhotoBoneTwistRandomMultiplier) %
			PhotoBoneTwistRandomModulus);
		if (PhotoBoneTwistRandomState == 0)
		{
			PhotoBoneTwistRandomState = 1u;
		}

		return static_cast<float>(PhotoBoneTwistRandomState) / static_cast<float>(PhotoBoneTwistRandomModulus);
	}

	float DrawPhotoBoneTwistDegrees(float MaxAbsDegrees)
	{
		return (DrawPhotoBoneTwistRandomUnit() * 2.0f - 1.0f) * MaxAbsDegrees;
	}

	FQuat MakePhotoBoneTwistDelta(int32 BoneIndex)
	{
		const float MaxDegrees = BoneIndex == 0 ? PhotoBoneTwistRootMaxDegrees : PhotoBoneTwistMaxDegrees;
		const FRotator DeltaRotator(
			DrawPhotoBoneTwistDegrees(MaxDegrees),
			DrawPhotoBoneTwistDegrees(MaxDegrees),
			DrawPhotoBoneTwistDegrees(MaxDegrees));
		return DeltaRotator.ToQuaternion();
	}

	void PlayCameraShutterAudio()
	{
		FAudioManager::Get().PlayAudio(CameraShutterAudioKey, CameraPhotoAudioVolume);
	}

	void PlayPhotoOutAudio()
	{
		FAudioManager::Get().PlayAudio(PhotoOutAudioKey, CameraPhotoAudioVolume, 2.0f);
	}

	bool IsHeldCameraMesh(UStaticMeshComponent* Component)
	{
		if (!Component)
		{
			return false;
		}

		const FString& StaticMeshPath = Component->GetStaticMeshPath();
		return
			StaticMeshPath == HeldCameraMeshPath ||
			StaticMeshPath.find(HeldCameraMeshPath) != FString::npos ||
			StaticMeshPath.find(HeldCameraMeshFileName) != FString::npos;
	}

	bool IsPhotoBoneTwistTarget(AActor* Actor)
	{
		return Actor && Actor->HasTag(FName(PhotoBoneTwistTargetTagName));
	}

	bool IsPhotoGhostReplacementTarget(AActor* Actor)
	{
		return Actor && Actor->HasTag(FName(PhotoGhostReplacementTargetTagName));
	}

	bool IsPhotoGhostReplacementActor(AActor* Actor)
	{
		return Actor && Actor->HasTag(FName(PhotoGhostReplacementActorTagName));
	}

	bool IsActorVisibilityTracked(AActor* Actor)
	{
		for (const FCaptureActorVisibilityState& State : CaptureActorVisibilityStates)
		{
			if (State.Actor.Get() == Actor)
			{
				return true;
			}
		}
		return false;
	}

	void SetActorVisibilityForCapture(AActor* Actor, bool bVisible)
	{
		if (!Actor)
		{
			return;
		}

		if (!IsActorVisibilityTracked(Actor))
		{
			FCaptureActorVisibilityState State;
			State.Actor = TWeakObjectPtr<AActor>(Actor);
			State.bWasVisible = Actor->IsVisible();
			CaptureActorVisibilityStates.push_back(State);
		}

		Actor->SetVisible(bVisible);
	}

	bool IsComponentVisibilityTracked(UPrimitiveComponent* Component)
	{
		for (const FCaptureComponentVisibilityState& State : CaptureComponentVisibilityStates)
		{
			if (State.Component.Get() == Component)
			{
				return true;
			}
		}
		return false;
	}

	void SetComponentVisibilityForCapture(UPrimitiveComponent* Component, bool bVisible)
	{
		if (!Component)
		{
			return;
		}

		if (!IsComponentVisibilityTracked(Component))
		{
			FCaptureComponentVisibilityState State;
			State.Component = TWeakObjectPtr<UPrimitiveComponent>(Component);
			State.bWasVisible = Component->IsVisible();
			CaptureComponentVisibilityStates.push_back(State);
		}

		Component->SetVisibility(bVisible);
	}

	UStaticMeshComponent* FindHeldCameraMeshComponent(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}

		for (AActor* Actor : World->GetActors())
		{
			if (!Actor || Actor == PhotoActor.Get())
			{
				continue;
			}

			for (UActorComponent* ActorComponent : Actor->GetComponents())
			{
				UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(ActorComponent);
				if (IsHeldCameraMesh(StaticMeshComponent))
				{
					return StaticMeshComponent;
				}
			}
		}

		return nullptr;
	}

	UStaticMeshComponent* GetHeldCameraMeshComponent(UWorld* World)
	{
		UStaticMeshComponent* Component = HeldCameraMeshComponent.Get();
		if (Component && Component->GetOwner() && Component->GetOwner()->GetWorld() == World && IsHeldCameraMesh(Component))
		{
			return Component;
		}

		Component = FindHeldCameraMeshComponent(World);
		HeldCameraMeshComponent = Component;
		return Component;
	}

	void DestroyPhotoActor()
	{
		if (AActor* Actor = PhotoActor.Get())
		{
			if (UWorld* World = Actor->GetWorld())
			{
				World->DestroyActor(Actor);
			}
		}
		PhotoComponent.Reset();
		PhotoActor.Reset();
	}

	void SpawnPhotoActor(UWorld* World)
	{
		DestroyPhotoActor();
		if (!World || !CapturedSRV || !FrameSRV)
		{
			return;
		}

		AActor* Actor = World->SpawnActor<AActor>();
		if (!Actor)
		{
			return;
		}
		Actor->SetFName(FName("RuntimePolaroidPhoto"));
		Actor->AddTag(FName("Fake"));
		Actor->bNeedsTick = false;

		UPhotoPolaroidComponent* Component = Actor->AddComponent<UPhotoPolaroidComponent>();
		if (!Component)
		{
			World->DestroyActor(Actor);
			return;
		}

		Component->SetCastShadow(false);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetTextures(CapturedSRV, FrameSRV);
		Component->SetDisplayTime(0.0f);
		Component->SetDevelopTime(0.0f);
		Actor->SetRootComponent(Component);

		PhotoActor = Actor;
		PhotoComponent = Component;
	}

	void UpdatePhotoActorTransform()
	{
		UPhotoPolaroidComponent* Component = PhotoComponent.Get();
		AActor* Actor = PhotoActor.Get();
		UWorld* World = Actor ? Actor->GetWorld() : PendingCaptureWorld.Get();
		if (!Component || !Actor || !World)
		{
			return;
		}

		FMinimalViewInfo POV;
		if (!World->GetActivePOV(POV))
		{
			return;
		}

		const float EjectAlpha = Clamp01(DisplayTime / PhotoEjectSeconds);
		const float EjectEase = EaseOutCubic(EjectAlpha);
		FVector Forward = POV.Rotation.GetForwardVector();
		FVector Right = POV.Rotation.GetRightVector();
		FVector Up = POV.Rotation.GetUpVector();
		FVector Location =
			POV.Location +
			Forward * 0.225f +
			Up * (-0.22f + 0.22f * EjectEase);

		if (UStaticMeshComponent* HeldCameraMesh = GetHeldCameraMeshComponent(World))
		{
			Forward = HeldCameraMesh->GetForwardVector();
			Right = HeldCameraMesh->GetRightVector();
			Up = HeldCameraMesh->GetUpVector();
			Location =
				HeldCameraMesh->GetWorldLocation() +
				Forward * HeldCameraPhotoForwardOffset +
				Right * HeldCameraPhotoRightOffset +
				Up * (HeldCameraPhotoBaseUpOffset + HeldCameraPhotoEjectUpDistance * EjectEase);
		}

		FMatrix PhotoRotationMatrix;
		PhotoRotationMatrix.SetAxes(Forward, Right, Up);
		Component->SetWorldRotation(FQuat::FromMatrix(PhotoRotationMatrix));

		Component->SetWorldLocation(Location);
		Component->SetRelativeScale(FVector(0.1f, 0.1f, 0.1f));
		Component->SetDisplayTime(DisplayTime);
		Component->SetDevelopTime(DevelopTime);
	}

	void StartCapturedPhotoEject()
	{
		bPhotoSpawnPending = false;
		PhotoSpawnDelayRemaining = 0.0f;
		DisplayTime = 0.0f;
		DevelopTime = 0.0f;
		SpawnPhotoActor(PendingCaptureWorld.Get());
		UpdatePhotoActorTransform();
		PlayPhotoOutAudio();
	}

	void ResetPhotoForNewCapture()
	{
		bPhotoSpawnPending = false;
		PhotoSpawnDelayRemaining = 0.0f;
		DisplayTime = 0.0f;
		DevelopTime = 0.0f;
		DestroyPhotoActor();
	}

	void HideHeldCameraForCapture(UWorld* World)
	{
		UStaticMeshComponent* HeldCamera = GetHeldCameraMeshComponent(World);
		if (!HeldCamera || !HeldCamera->IsVisible())
		{
			return;
		}

		SetComponentVisibilityForCapture(HeldCamera, false);
	}

	void PreparePhotoGhostReplacementForCapture(UWorld* World)
	{
		if (!World)
		{
			return;
		}

		for (AActor* Actor : World->GetActors())
		{
			if (!Actor)
			{
				continue;
			}

			if (IsPhotoGhostReplacementTarget(Actor) && Actor->IsVisible())
			{
				SetActorVisibilityForCapture(Actor, false);
			}

			if (IsPhotoGhostReplacementActor(Actor))
			{
				SetActorVisibilityForCapture(Actor, true);
			}
		}
	}

	void PreparePhotoBoneTwistForCapture(UWorld* World)
	{
		if (!World)
		{
			return;
		}

		for (AActor* Actor : World->GetActors())
		{
			if (!Actor || !Actor->IsVisible() || !IsPhotoBoneTwistTarget(Actor))
			{
				continue;
			}

			for (UActorComponent* ActorComponent : Actor->GetComponents())
			{
				USkinnedMeshComponent* MeshComponent = Cast<USkinnedMeshComponent>(ActorComponent);
				if (!MeshComponent || !MeshComponent->IsVisible())
				{
					continue;
				}

				USkeletalMesh* SkeletalMesh = MeshComponent->GetSkeletalMesh();
				FSkeletalMesh* SkeletalMeshAsset = SkeletalMesh ? SkeletalMesh->GetSkeletalMeshAsset() : nullptr;
				if (!SkeletalMeshAsset || SkeletalMeshAsset->Bones.empty())
				{
					continue;
				}

				FPhotoBoneTwistPoseState PoseState;
				PoseState.Mesh = TWeakObjectPtr<USkinnedMeshComponent>(MeshComponent);
				PoseState.LocalPose.reserve(SkeletalMeshAsset->Bones.size());

				const int32 BoneCount = static_cast<int32>(SkeletalMeshAsset->Bones.size());
				for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
				{
					PoseState.LocalPose.push_back(MeshComponent->GetBoneLocalTransformByIndex(BoneIndex));
				}

				for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
				{
					FTransform TwistedTransform = PoseState.LocalPose[BoneIndex];
					TwistedTransform.Rotation = (TwistedTransform.Rotation * MakePhotoBoneTwistDelta(BoneIndex)).GetNormalized();
					MeshComponent->SetBoneLocalTransformByIndex(BoneIndex, TwistedTransform);
				}

				PhotoBoneTwistPoseStates.push_back(PoseState);
			}
		}
	}
}

void FPhotoOverlay::RequestCapture()
{
	PlayCameraShutterAudio();
	RestoreHiddenActors();
	ResetPhotoForNewCapture();
	FlashTime = 0.0f;
	PendingCaptureWorld.Reset();
	PendingCaptureExcludeActorTag = FName::None;
	bCaptureBlackoutRequested = false;
	bCaptureWorldStatePrepared = false;
	bCaptureRequested = true;
}

void FPhotoOverlay::RequestCapture(UWorld* World, const FName& ExcludeActorTag, bool bBlackout)
{
	PlayCameraShutterAudio();
	RestoreHiddenActors();
	ResetPhotoForNewCapture();
	FlashTime = 0.0f;
	PendingCaptureWorld = World;
	PendingCaptureExcludeActorTag = ExcludeActorTag;
	bCaptureBlackoutRequested = bBlackout;
	bCaptureWorldStatePrepared = false;
	bCaptureRequested = true;
}

void FPhotoOverlay::PreparePendingCaptureWorldState(UWorld* World)
{
	if (!bCaptureRequested || bCaptureWorldStatePrepared)
	{
		return;
	}

	UWorld* CaptureWorld = PendingCaptureWorld.Get();
	if (!CaptureWorld)
	{
		CaptureWorld = World;
	}

	if (!CaptureWorld)
	{
		return;
	}

	if (PendingCaptureExcludeActorTag.IsValid() && PendingCaptureExcludeActorTag != FName::None)
	{
		for (AActor* Actor : CaptureWorld->GetActors())
		{
			if (!Actor || !Actor->IsVisible() || !Actor->HasTag(PendingCaptureExcludeActorTag) || IsPhotoGhostReplacementTarget(Actor))
			{
				continue;
			}

			SetActorVisibilityForCapture(Actor, false);
		}
	}

	PreparePhotoGhostReplacementForCapture(CaptureWorld);
	PreparePhotoBoneTwistForCapture(CaptureWorld);
	HideHeldCameraForCapture(CaptureWorld);
	bCaptureWorldStatePrepared = true;
}

void FPhotoOverlay::CapturePendingFromViewport(ID3D11Texture2D* SourceTexture)
{
	if (!bCaptureRequested)
	{
		return;
	}
	if (!SourceTexture)
	{
		bCaptureRequested = false;
		bCaptureBlackoutRequested = false;
		bCaptureWorldStatePrepared = false;
		bPhotoSpawnPending = false;
		RestoreHiddenActors();
		return;
	}

	bCaptureRequested = false;
	if (!EnsureResources(SourceTexture))
	{
		bCaptureBlackoutRequested = false;
		bCaptureWorldStatePrepared = false;
		bPhotoSpawnPending = false;
		RestoreHiddenActors();
		return;
	}

	ID3D11Device* Device = nullptr;
	SourceTexture->GetDevice(&Device);
	if (!Device)
	{
		bCaptureBlackoutRequested = false;
		bCaptureWorldStatePrepared = false;
		bPhotoSpawnPending = false;
		RestoreHiddenActors();
		return;
	}

	ID3D11DeviceContext* Context = nullptr;
	Device->GetImmediateContext(&Context);
	EnsureFrameResource(Device);

	if (!Context)
	{
		Device->Release();
		bCaptureBlackoutRequested = false;
		bCaptureWorldStatePrepared = false;
		bPhotoSpawnPending = false;
		RestoreHiddenActors();
		return;
	}

	bool bWroteCapturedTexture = false;
	if (bCaptureBlackoutRequested)
	{
		ID3D11RenderTargetView* BlackoutRTV = nullptr;
		if (SUCCEEDED(Device->CreateRenderTargetView(CapturedTexture, nullptr, &BlackoutRTV)) && BlackoutRTV)
		{
			const float Black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			Context->ClearRenderTargetView(BlackoutRTV, Black);
			BlackoutRTV->Release();
			bWroteCapturedTexture = true;
		}
	}
	if (!bWroteCapturedTexture)
	{
		Context->CopyResource(CapturedTexture, SourceTexture);
	}

	bCaptureBlackoutRequested = false;
	bCaptureWorldStatePrepared = false;
	Device->Release();
	Context->Release();
	RestoreHiddenActors();
	bPhotoSpawnPending = true;
	PhotoSpawnDelayRemaining = PhotoSpawnDelaySeconds;
}

bool FPhotoOverlay::ShouldSuppressViewportUIForCapture()
{
	return bCaptureRequested;
}

void FPhotoOverlay::Tick(float DeltaTime)
{
	if (FlashTime < PhotoFlashSeconds)
	{
		FlashTime += DeltaTime;
	}

	if (bPhotoSpawnPending)
	{
		PhotoSpawnDelayRemaining -= DeltaTime;
		if (PhotoSpawnDelayRemaining <= 0.0f)
		{
			StartCapturedPhotoEject();
		}
	}

	if (PhotoActor.Get() && PhotoComponent.Get())
	{
		DisplayTime += DeltaTime;
		DevelopTime = DisplayTime > PhotoEjectSeconds ? DisplayTime - PhotoEjectSeconds : 0.0f;
		UpdatePhotoActorTransform();
	}
}

bool FPhotoOverlay::IsVisible()
{
	return PhotoActor.Get() && CapturedSRV;
}

bool FPhotoOverlay::IsFlashVisible()
{
	return FlashTime < PhotoFlashSeconds;
}

ID3D11ShaderResourceView* FPhotoOverlay::GetSRV()
{
	return IsVisible() ? CapturedSRV : nullptr;
}

ID3D11ShaderResourceView* FPhotoOverlay::GetFrameSRV()
{
	return FrameSRV;
}

float FPhotoOverlay::GetDisplayTime()
{
	return DisplayTime;
}

float FPhotoOverlay::GetFlashTime()
{
	return FlashTime;
}

float FPhotoOverlay::GetDevelopTime()
{
	return DevelopTime;
}

float FPhotoOverlay::GetDevelopSeconds()
{
	return PhotoDevelopSeconds;
}

float FPhotoOverlay::GetEjectSeconds()
{
	return PhotoEjectSeconds;
}

bool FPhotoOverlay::IsCaptureInProgress()
{
	if (bCaptureRequested || bPhotoSpawnPending)
	{
		return true;
	}

	return PhotoActor.Get() && PhotoComponent.Get() && DevelopTime < PhotoDevelopSeconds;
}

float FPhotoOverlay::GetCaptureAspectRatio()
{
	return CapturedHeight > 0 ? static_cast<float>(CapturedWidth) / static_cast<float>(CapturedHeight) : 16.0f / 9.0f;
}

float FPhotoOverlay::GetFrameAspectRatio()
{
	return FrameHeight > 0 ? static_cast<float>(FrameWidth) / static_cast<float>(FrameHeight) : DefaultFrameAspectRatio;
}

void FPhotoOverlay::RestoreHiddenActors()
{
	for (FPhotoBoneTwistPoseState& State : PhotoBoneTwistPoseStates)
	{
		if (USkinnedMeshComponent* Mesh = State.Mesh.Get())
		{
			const int32 BoneCount = static_cast<int32>(State.LocalPose.size());
			for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
			{
				Mesh->SetBoneLocalTransformByIndex(BoneIndex, State.LocalPose[BoneIndex]);
			}
		}
	}
	PhotoBoneTwistPoseStates.clear();

	for (FCaptureComponentVisibilityState& State : CaptureComponentVisibilityStates)
	{
		if (UPrimitiveComponent* Component = State.Component.Get())
		{
			Component->SetVisibility(State.bWasVisible);
		}
	}
	CaptureComponentVisibilityStates.clear();

	for (FCaptureActorVisibilityState& State : CaptureActorVisibilityStates)
	{
		if (AActor* Actor = State.Actor.Get())
		{
			Actor->SetVisible(State.bWasVisible);
		}
	}
	CaptureActorVisibilityStates.clear();
}

bool FPhotoOverlay::EnsureResources(ID3D11Texture2D* SourceTexture)
{
	D3D11_TEXTURE2D_DESC SourceDesc = {};
	SourceTexture->GetDesc(&SourceDesc);
	if (SourceDesc.Width == 0 || SourceDesc.Height == 0)
	{
		return false;
	}

	if (CapturedTexture && CapturedSRV && CapturedWidth == SourceDesc.Width && CapturedHeight == SourceDesc.Height)
	{
		return true;
	}

	ReleaseResources();

	ID3D11Device* Device = nullptr;
	SourceTexture->GetDevice(&Device);
	if (!Device)
	{
		return false;
	}

	D3D11_TEXTURE2D_DESC CaptureDesc = SourceDesc;
	CaptureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	CaptureDesc.CPUAccessFlags = 0;
	CaptureDesc.MiscFlags = 0;
	CaptureDesc.Usage = D3D11_USAGE_DEFAULT;

	HRESULT HR = Device->CreateTexture2D(&CaptureDesc, nullptr, &CapturedTexture);
	if (FAILED(HR) || !CapturedTexture)
	{
		Device->Release();
		return false;
	}
	CapturedTexture->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("PhotoOverlayTexture")), "PhotoOverlayTexture");

	HR = Device->CreateShaderResourceView(CapturedTexture, nullptr, &CapturedSRV);
	Device->Release();
	if (FAILED(HR) || !CapturedSRV)
	{
		ReleaseResources();
		return false;
	}
	CapturedSRV->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("PhotoOverlaySRV")), "PhotoOverlaySRV");

	CapturedWidth = SourceDesc.Width;
	CapturedHeight = SourceDesc.Height;
	return true;
}

bool FPhotoOverlay::EnsureFrameResource(ID3D11Device* Device)
{
	if (FrameSRV)
	{
		return true;
	}
	if (!Device)
	{
		return false;
	}

	const std::filesystem::path FramePath = ToProjectPath("Content/Texture/polaroid.png");
	ID3D11Resource* Resource = nullptr;
	ID3D11ShaderResourceView* SRV = nullptr;
	const HRESULT HR = DirectX::CreateWICTextureFromFileEx(
		Device,
		FramePath.c_str(),
		0,
		D3D11_USAGE_DEFAULT,
		D3D11_BIND_SHADER_RESOURCE,
		0,
		0,
		DirectX::WIC_LOADER_IGNORE_SRGB,
		&Resource,
		&SRV);

	if (FAILED(HR) || !SRV)
	{
		if (Resource)
		{
			Resource->Release();
		}
		return false;
	}

	if (Resource)
	{
		ID3D11Texture2D* Texture2D = nullptr;
		if (SUCCEEDED(Resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&Texture2D))) && Texture2D)
		{
			D3D11_TEXTURE2D_DESC Desc = {};
			Texture2D->GetDesc(&Desc);
			FrameWidth = Desc.Width;
			FrameHeight = Desc.Height;
			Texture2D->Release();
		}
		Resource->Release();
	}

	FrameSRV = SRV;
	FrameSRV->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("PhotoOverlayFrameSRV")), "PhotoOverlayFrameSRV");
	return true;
}

void FPhotoOverlay::ReleaseResources()
{
	if (CapturedSRV)
	{
		CapturedSRV->Release();
		CapturedSRV = nullptr;
	}
	if (CapturedTexture)
	{
		CapturedTexture->Release();
		CapturedTexture = nullptr;
	}
	CapturedWidth = 0;
	CapturedHeight = 0;
}
