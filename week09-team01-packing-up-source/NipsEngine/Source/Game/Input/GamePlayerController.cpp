#include "Game/Input/GamePlayerController.h"

#include "Audio/AudioSystem.h"
#include "Component/CameraComponent.h"
#include "Component/LuaScriptComponent.h"
#include "Component/Movement/CharacterMovementComponent.h"
#include "Component/Physics/PhysicsHandleComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Physics/RigidBodyComponent.h"
#include "Component/SceneComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/SubUVComponent.h"
#include "Core/Logger.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Runtime/SceneView.h"
#include "Engine/Geometry/Ray.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "Game/Systems/CleaningToolAnimator.h"
#include "Game/Systems/CleaningToolSystem.h"
#include "Game/Systems/GameContext.h"
#include "Game/Systems/ItemSystem.h"
#include "Game/UI/GameUISystem.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Math/Matrix.h"
#include "Math/Utils.h"
#include "Object/Object.h"
#include "Physics/JoltPhysicsSystem.h"
#include "Scripting/LuaScriptSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <random>
#include <windows.h>

namespace
{
	// 새 Action을 추가하려면:
	// 1. 여기에 이름 함수를 하나 더 만듭니다. 예: ActionInteract() -> "Interact"
	// 2. SetupDefaultInputMappings()에서 원하는 키를 AddActionMapping으로 연결합니다.
	// 3. OnKeyPressed/OnKeyReleased에서 IsActionKey로 처리합니다.
	const FName& ActionToggleInputCapture()
	{
		static const FName Name("ToggleInputCapture");
		return Name;
	}

	const FName& ActionTogglePause()
	{
		static const FName Name("TogglePause");
		return Name;
	}

	const FName& ActionPickup()
	{
		static const FName Name("Pickup");
		return Name;
	}

	const FName& ActionInspect()
	{
		static const FName Name("Inspect");
		return Name;
	}

	const FName& ActionJump()
	{
		static const FName Name("Jump");
		return Name;
	}

	const FString& MopToolId()
	{
		static const FString Id("mop");
		return Id;
	}

	const FString& MopCleaningLoopSoundPath()
	{
		static const FString Path("Asset/Audio/mopping-floor.wav");
		return Path;
	}

	const FString& SpongeToolId()
	{
		static const FString Id("sponge");
		return Id;
	}

	FString SpongeCleaningSoundPath(int Index)
	{
		const int ClampedIndex = std::clamp(Index, 0, 4);
		return "Asset/Audio/sponge-" + std::to_string(ClampedIndex + 1) + ".wav";
	}

	int RandomSpongeSoundIndex()
	{
		static std::mt19937 Generator(std::random_device{}());
		static std::uniform_int_distribution<int> Distribution(0, 4);
		return Distribution(Generator);
	}

	// 새 Axis를 추가하려면:
	// 1. 여기에 이름 함수를 하나 더 만듭니다. 예: AxisMoveForward() -> "MoveForward"
	// 2. SetupDefaultInputMappings()에서 키와 Scale을 AddAxisMapping으로 연결합니다.
	// 3. ApplyInputAxes()에서 GetAxisValue로 값을 읽어 사용합니다.
	const FName& AxisMoveForward()
	{
		static const FName Name("MoveForward");
		return Name;
	}

	const FName& AxisMoveRight()
	{
		static const FName Name("MoveRight");
		return Name;
	}

	const FName& AxisLookYaw()
	{
		static const FName Name("LookYaw");
		return Name;
	}

	const FName& AxisLookPitch()
	{
		static const FName Name("LookPitch");
		return Name;
	}

	FVector ToForward(float PitchDegrees, float YawDegrees)
	{
		const float PitchRad = MathUtil::DegreesToRadians(PitchDegrees);
		const float YawRad = MathUtil::DegreesToRadians(YawDegrees);

		return FVector(std::cos(PitchRad) * std::cos(YawRad), std::cos(PitchRad) * std::sin(YawRad), std::sin(PitchRad)).GetSafeNormal();
	}

	FVector ToRight(float YawDegrees)
	{
		const float YawRad = MathUtil::DegreesToRadians(YawDegrees);
		return FVector(-std::sin(YawRad), std::cos(YawRad), 0.0f).GetSafeNormal();
	}

	FVector ToWorldOffset(const FVector& CameraForward, const FVector& CameraRight, const FVector& CameraUp, const FVector& CameraLocalOffset)
	{
		return CameraForward.GetSafeNormal() * CameraLocalOffset.X
			+ CameraRight.GetSafeNormal() * CameraLocalOffset.Y
			+ CameraUp.GetSafeNormal() * CameraLocalOffset.Z;
	}

	FVector ToWorldOffset(const FViewportCamera* Camera, const FVector& CameraLocalOffset)
	{
		if (!Camera)
		{
			return FVector::ZeroVector;
		}

		return ToWorldOffset(Camera->GetForwardVector(), Camera->GetRightVector(), Camera->GetUpVector(), CameraLocalOffset);
	}

	FQuat BuildCameraLocalHandleRotation(const FVector& CameraForward, const FVector& CameraRight, const FVector& CameraUp, const FVector& HandleCameraLocalDirection)
	{
		const FVector Forward = CameraForward.GetSafeNormal();
		const FVector HandleWorldDirection = ToWorldOffset(Forward, CameraRight, CameraUp, HandleCameraLocalDirection).GetSafeNormal();
		if (HandleWorldDirection.IsNearlyZero() || Forward.IsNearlyZero())
		{
			return FQuat::Identity;
		}

		FVector YAxis = FVector::CrossProduct(Forward, HandleWorldDirection).GetSafeNormal();
		if (YAxis.IsNearlyZero())
		{
			YAxis = CameraRight.GetSafeNormal();
		}

		const FVector ZAxis = FVector::CrossProduct(HandleWorldDirection, YAxis).GetSafeNormal();
		FMatrix RotationMatrix = FMatrix::Identity;
		RotationMatrix.SetAxes(HandleWorldDirection, YAxis, ZAxis);

		FQuat Rotation(RotationMatrix);
		Rotation.Normalize();
		return Rotation;
	}

	FQuat BuildCameraLocalHandleRotation(const FViewportCamera* Camera, const FVector& HandleCameraLocalDirection)
	{
		if (!Camera)
		{
			return FQuat::Identity;
		}

		return BuildCameraLocalHandleRotation(Camera->GetForwardVector(), Camera->GetRightVector(), Camera->GetUpVector(), HandleCameraLocalDirection);
	}

	FString NormalizeToolMatchKey(FString Value)
	{
		std::replace(Value.begin(), Value.end(), '\\', '/');
		std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char C)
		{
			return static_cast<char>(std::tolower(C));
		});
		return Value;
	}

	FString GetAssetFileStemMatchKey(FString Value)
	{
		Value = NormalizeToolMatchKey(Value);
		const size_t SlashPos = Value.find_last_of('/');
		if (SlashPos != FString::npos)
		{
			Value = Value.substr(SlashPos + 1);
		}

		const size_t DotPos = Value.find_last_of('.');
		if (DotPos != FString::npos)
		{
			Value = Value.substr(0, DotPos);
		}
		return Value;
	}

	FString NormalizeLooseItemMatchKey(FString Value)
	{
		Value = NormalizeToolMatchKey(Value);
		Value.erase(std::remove_if(Value.begin(), Value.end(), [](char C)
		{
			return C == '_' || C == '-' || C == ' ';
		}), Value.end());
		return Value;
	}

	FString ResolveItemAliasFromMeshStem(const FString& MeshStem)
	{
		if (MeshStem == "frame" || MeshStem == "drawingpaper")
		{
			return "painting";
		}
		if (MeshStem == "cardboard_box" || MeshStem == "box")
		{
			return "moving_box";
		}
		if (MeshStem == "candybottle")
		{
			return "jar";
		}
		if (MeshStem == "rolleduppapers")
		{
			return "paper_roll";
		}
		if (MeshStem == "wooden_bucket_01_1k")
		{
			return "bucket";
		}

		return "";
	}

	bool IsItemInspectable(const FString& ItemId)
	{
		const FGameItemData* ItemData = FItemSystem::Get().FindItemData(ItemId);
		return ItemData != nullptr && ItemData->bCanInspect;
	}

	FString FindCleaningToolIdFromActor(const AActor* Actor, bool bLogResult = true)
	{
		if (!Actor)
		{
			return "";
		}

		const FString ScriptToolId = FLuaScriptSystem::Get().GetStringGameStateValue("CleaningTool:" + Actor->GetFName().ToString());
		if (!ScriptToolId.empty())
		{
			if (bLogResult)
			{
				UE_LOG("[CleaningTool] Actor=%s resolved by Lua toolId=%s", Actor->GetFName().ToString().c_str(), ScriptToolId.c_str());
			}
			return ScriptToolId;
		}

		const FString ActorName = NormalizeToolMatchKey(Actor->GetName());
		if (bLogResult)
		{
			UE_LOG("[CleaningTool] Actor=%s has no Lua tool id. Trying fallback match.", Actor->GetFName().ToString().c_str());
		}
		const TArray<FCleaningToolData>& ToolDataList = FCleaningToolSystem::Get().GetAllToolData();
		for (const FCleaningToolData& ToolData : ToolDataList)
		{
			if (ActorName == NormalizeToolMatchKey(ToolData.ToolId))
			{
				if (bLogResult)
				{
					UE_LOG("[CleaningTool] Actor=%s resolved by actor name toolId=%s", Actor->GetFName().ToString().c_str(), ToolData.ToolId.c_str());
				}
				return ToolData.ToolId;
			}
		}

		if (bLogResult)
		{
			UE_LOG("[CleaningTool] Actor=%s is not a registered cleaning tool.", Actor->GetFName().ToString().c_str());
		}
		return "";
	}

	FString FindItemIdFromActor(const AActor* Actor)
	{
		if (!Actor)
		{
			return "";
		}

		const FString ScriptItemId = FLuaScriptSystem::Get().GetStringGameStateValue("Item:" + Actor->GetFName().ToString());
		if (!ScriptItemId.empty())
		{
			return ScriptItemId;
		}

		const FString ActorName = NormalizeToolMatchKey(Actor->GetName());
		const TArray<FGameItemData>& Items = FItemSystem::Get().GetAllItemData();
		for (const FGameItemData& ItemData : Items)
		{
			if (ActorName == NormalizeToolMatchKey(ItemData.ItemId))
			{
				return ItemData.ItemId;
			}
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Component);
			if (MeshComponent == nullptr || MeshComponent->GetStaticMesh() == nullptr)
			{
				continue;
			}

			const FString MeshName = GetAssetFileStemMatchKey(MeshComponent->GetStaticMesh()->GetAssetPathFileName());
			const FString AliasItemId = ResolveItemAliasFromMeshStem(MeshName);
			if (!AliasItemId.empty() && FItemSystem::Get().FindItemData(AliasItemId) != nullptr)
			{
				return AliasItemId;
			}

			const FString LooseMeshName = NormalizeLooseItemMatchKey(MeshName);
			for (const FGameItemData& ItemData : Items)
			{
				if (MeshName == NormalizeToolMatchKey(ItemData.ItemId) ||
					LooseMeshName == NormalizeLooseItemMatchKey(ItemData.ItemId))
				{
					return ItemData.ItemId;
				}
			}
		}

		return "";
	}

	bool TryGetDecisionBoxTypeFromActor(const AActor* Actor, EItemDecisionBoxType& OutBoxType)
	{
		if (Actor == nullptr)
		{
			return false;
		}

		const FString ActorName = NormalizeToolMatchKey(Actor->GetName());
		if (ActorName.find("keepbox") != FString::npos || ActorName.find("keep_box") != FString::npos)
		{
			OutBoxType = EItemDecisionBoxType::KeepBox;
			return true;
		}

		if (ActorName.find("discardbox") != FString::npos || ActorName.find("discard_box") != FString::npos)
		{
			OutBoxType = EItemDecisionBoxType::DiscardBox;
			return true;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			const ULuaScriptComponent* ScriptComponent = Cast<ULuaScriptComponent>(Component);
			if (ScriptComponent == nullptr)
			{
				continue;
			}

			const FString ScriptPath = NormalizeToolMatchKey(ScriptComponent->GetScriptPath());
			if (ScriptPath.find("keepbox.lua") != FString::npos)
			{
				OutBoxType = EItemDecisionBoxType::KeepBox;
				return true;
			}

			if (ScriptPath.find("discardbox.lua") != FString::npos)
			{
				OutBoxType = EItemDecisionBoxType::DiscardBox;
				return true;
			}
		}

		return false;
	}

FVector GetCleaningToolAnimationDirection(const FCleaningToolData& ToolData)
{
	if (!ToolData.HandleCameraLocalDirection.IsNearlyZero())
	{
		return ToolData.HandleCameraLocalDirection;
	}

	return FVector(1.0f, 0.0f, 0.0f);
}

FQuat BuildCameraLocalHandleRotation(const FVector& HandleCameraLocalDirection)
{
	return BuildCameraLocalHandleRotation(
		FVector(1.0f, 0.0f, 0.0f),
		FVector(0.0f, 1.0f, 0.0f),
		FVector(0.0f, 0.0f, 1.0f),
		HandleCameraLocalDirection);
}

FQuat BuildCleaningToolLocalRotation(const FVector& RotationDegrees)
{
	if (RotationDegrees.IsNearlyZero())
	{
		return FQuat::Identity;
	}

	FQuat Rotation = FQuat(FVector(1.0f, 0.0f, 0.0f), MathUtil::DegreesToRadians(RotationDegrees.X))
		* FQuat(FVector(0.0f, 1.0f, 0.0f), MathUtil::DegreesToRadians(RotationDegrees.Y))
		* FQuat(FVector(0.0f, 0.0f, 1.0f), MathUtil::DegreesToRadians(RotationDegrees.Z));
	Rotation.Normalize();
	return Rotation;
}

FVector ApplyCleaningToolLocalRotationToStrokeOffset(const FCleaningToolData& ToolData, const FVector& CameraLocalOffset)
{
	if (ToolData.ViewModelLocalRotationDegrees.IsNearlyZero())
	{
		return CameraLocalOffset;
	}

	const FVector StrokeLocalOffset = CameraLocalOffset - ToolData.HoldCameraLocalOffset;
	if (StrokeLocalOffset.IsNearlyZero())
	{
		return CameraLocalOffset;
	}

	const FVector LocalXAxis = GetCleaningToolAnimationDirection(ToolData);
	if (LocalXAxis.IsNearlyZero())
	{
		return CameraLocalOffset;
	}

	const FQuat LocalToolRotation = BuildCameraLocalHandleRotation(LocalXAxis);
	const FQuat LocalAdjustment = BuildCleaningToolLocalRotation(ToolData.ViewModelLocalRotationDegrees);
	FQuat StrokeRotation = LocalToolRotation.Inverse() * LocalAdjustment * LocalToolRotation;
	StrokeRotation.Normalize();
	return ToolData.HoldCameraLocalOffset + (StrokeRotation * StrokeLocalOffset);
}

FVector BuildCameraScreenUp(const FVector& CameraForward, const FVector& CameraRight)
{
	FVector ScreenUp = FVector::CrossProduct(CameraForward.GetSafeNormal(), CameraRight.GetSafeNormal()).GetSafeNormal();
	if (ScreenUp.IsNearlyZero())
	{
		return FVector(0.0f, 0.0f, 1.0f);
	}

	return ScreenUp;
}

FQuat BuildCameraBasisRotation(const FVector& CameraForward, const FVector& CameraRight, const FVector& CameraScreenUp)
{
	FMatrix CameraBasis = FMatrix::Identity;
	CameraBasis.SetAxes(
		CameraForward.GetSafeNormal(),
		CameraRight.GetSafeNormal(),
		CameraScreenUp.GetSafeNormal());

	FQuat CameraRotation(CameraBasis);
	CameraRotation.Normalize();
	return CameraRotation;
}

FQuat BuildViewModelWorldRotation(
	const FVector& CameraForward,
	const FVector& CameraRight,
	const FVector& HandleCameraLocalDirection,
	const FVector& LocalRotationDegrees)
{
	const FVector CameraScreenUp = BuildCameraScreenUp(CameraForward, CameraRight);
	FQuat LocalToolRotation = BuildCameraLocalHandleRotation(HandleCameraLocalDirection);
	FQuat LocalRotation = BuildCleaningToolLocalRotation(LocalRotationDegrees);
	FQuat CameraRotation = BuildCameraBasisRotation(CameraForward, CameraRight, CameraScreenUp);
	FQuat Result = LocalRotation * LocalToolRotation * CameraRotation;
	Result.Normalize();
	return Result;
}

bool IsLiveObjectPointer(const UObject* Object)
{
	if (Object == nullptr)
	{
		return false;
	}

	for (const UObject* LiveObject : GUObjectArray)
	{
		if (LiveObject == Object)
		{
			return true;
		}
	}

	return false;
}

UStaticMeshComponent* FindFirstVisibleStaticMesh(AActor* Actor)
{
	if (Actor == nullptr)
	{
		return nullptr;
	}

	for (UActorComponent* Component : Actor->GetComponents())
	{
		UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Component);
		if (MeshComponent != nullptr && MeshComponent->HasValidMesh())
		{
			return MeshComponent;
		}
	}

	return nullptr;
}

bool IsSprintKeyHeld()
{
	return InputSystem::Get().GetKey(VK_SHIFT);
}
} // namespace

FGamePlayerController::FGamePlayerController()
{
	SetupDefaultInputMappings();
}

FGamePlayerController::~FGamePlayerController()
{
	StopCleaningLoopSound();
	DestroyPhysicsHandle();
}

void FGamePlayerController::Tick(float DeltaTime)
{
	IBaseGameController::Tick(DeltaTime);
	RefreshPawnComponents();
	CaptureInitialRigidBodyRotations();
	SyncFreeCameraAngles();
	ApplyInputAxes();
	UpdateHoveredPickableActor();
	FCleaningToolAnimator::Get().Tick(DeltaTime);
	UpdateCleaningUseSound();
	if (UPhysicsHandleComponent* Handle = GetPhysicsHandle())
	{
		FVector CameraLocation;
		FVector CameraForward;
		FVector CameraRight;
		FVector CameraUp;
		if (!GetActiveCameraBasis(CameraLocation, CameraForward, CameraRight, CameraUp))
		{
			return;
		}

		const FString& CurrentToolId = GGameContext::Get().GetCurrentToolId();
		const FCleaningToolData* ToolData = CurrentToolId.empty() ? nullptr : FCleaningToolSystem::Get().FindToolData(CurrentToolId);
		const FVector HoldCameraLocalOffset = FCleaningToolAnimator::Get().GetHoldCameraLocalOffset();
		const FVector AnimatedCameraLocalOffset = HoldCameraLocalOffset + FCleaningToolAnimator::Get().GetCameraLocalOffset();
		const FVector PhysicsCameraLocalOffset = ToolData ? HoldCameraLocalOffset : AnimatedCameraLocalOffset;
		const FVector ToolOffset = ToWorldOffset(CameraForward, CameraRight, CameraUp, PhysicsCameraLocalOffset);
		Handle->TickHandle(DeltaTime, CameraLocation, CameraForward, ToolOffset, nullptr, ToolData != nullptr);
	}
}

void FGamePlayerController::LateTick(float DeltaTime)
{
	(void)DeltaTime;

	UPhysicsHandleComponent* Handle = GetPhysicsHandle();
	if (Handle == nullptr || !Handle->IsHolding())
	{
		return;
	}

	const FString& CurrentToolId = GGameContext::Get().GetCurrentToolId();
	if (CurrentToolId.empty())
	{
		return;
	}

	const FCleaningToolData* ToolData = FCleaningToolSystem::Get().FindToolData(CurrentToolId);
	if (ToolData == nullptr)
	{
		return;
	}

	FVector CameraLocation;
	FVector CameraForward;
	FVector CameraRight;
	FVector CameraUp;
	if (!GetActiveCameraBasis(CameraLocation, CameraForward, CameraRight, CameraUp))
	{
		return;
	}

	const FVector AnimatedCameraLocalOffset =
		FCleaningToolAnimator::Get().GetHoldCameraLocalOffset() + FCleaningToolAnimator::Get().GetCameraLocalOffset();
	UpdateCleaningToolViewModel(*ToolData, AnimatedCameraLocalOffset, CameraLocation, CameraForward, CameraRight);
}

void FGamePlayerController::OnMouseMove(float DeltaX, float DeltaY)
{
	RotateActiveCamera(DeltaX, DeltaY);
}

void FGamePlayerController::OnMouseMoveAbsolute(float X, float Y)
{
	(void)X;
	(void)Y;
}

void FGamePlayerController::OnLeftMouseClick(float X, float Y)
{
	(void)X;
	(void)Y;
	TryBeginCleaningUse();
}

void FGamePlayerController::OnLeftMouseDrag(float X, float Y)
{
	(void)X;
	(void)Y;
	TryBeginCleaningUse();
}

void FGamePlayerController::OnLeftMouseDragEnd(float X, float Y)
{
	(void)X;
	(void)Y;
	EndCleaningUse();
}

void FGamePlayerController::OnLeftMouseButtonUp(float X, float Y)
{
	(void)X;
	(void)Y;
	EndCleaningUse();
}

void FGamePlayerController::OnRightMouseClick(float DeltaX, float DeltaY)
{
	(void)DeltaX;
	(void)DeltaY;
}

void FGamePlayerController::OnRightMouseDrag(float DeltaX, float DeltaY)
{
	RotateActiveCamera(DeltaX, DeltaY);
}

void FGamePlayerController::OnMiddleMouseDrag(float DeltaX, float DeltaY)
{
	(void)DeltaX;
	(void)DeltaY;
}

void FGamePlayerController::OnKeyPressed(int VK)
{
	if (InputMapping.IsActionKey(ActionToggleInputCapture(), VK) && !Camera && OnRequestToggleInputCapture)
	{
		OnRequestToggleInputCapture();
	}

	if (InputMapping.IsActionKey(ActionPickup(), VK))
	{
		TogglePickup();
	}

	if (InputMapping.IsActionKey(ActionInspect(), VK))
	{
		TryInspectHoveredItem();
	}

	if (InputMapping.IsActionKey(ActionJump(), VK))
	{
		if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
		{
			MoveComp->Jump();
		}
	}
}

void FGamePlayerController::OnKeyDown(int VK)
{
	// 이동/회전처럼 누르고 있는 동안 계속 적용되는 입력은 Axis에서 처리합니다.
	// 그래서 개별 키 반복 이벤트인 OnKeyDown에서는 직접 움직이지 않습니다.
	(void)VK;
}

void FGamePlayerController::OnKeyReleased(int VK)
{
	if (InputMapping.IsActionKey(ActionTogglePause(), VK) && OnRequestTogglePause)
	{
		OnRequestTogglePause();
	}
}

void FGamePlayerController::OnWheelScrolled(float Notch)
{
	if (IsRuntimeWorld())
	{
		return;
	}

	MoveActiveCamera(ToForward(Camera ? Camera->GetPitchDegrees() : FreeCameraPitch,
							   Camera ? Camera->GetYawDegrees() : FreeCameraYaw),
					 Notch * MoveSpeed * 0.25f);
}

void FGamePlayerController::SetWorld(UWorld* InWorld)
{
	if (World == InWorld)
	{
		return;
	}

	EndCleaningToolViewModel();
	StopCleaningLoopSound();
	World = InWorld;
	Player = nullptr;
	Camera = nullptr;
	CharacterMovement = nullptr;
	HoveredPickableActor = nullptr;
	HoveredDecisionBoxActor = nullptr;
	InitialRigidBodyRotations.clear();
	bInitialRigidBodyRotationsCaptured = false;
	DestroyPhysicsHandle();
}

void FGamePlayerController::SetPlayer(AActor* InPlayer)
{
	if (Player == InPlayer)
	{
		return;
	}

	EndCleaningToolViewModel();
	StopCleaningLoopSound();
	DestroyPhysicsHandle();
	CharacterMovement = nullptr;
	Player = InPlayer;
	RefreshPawnComponents();
}

void FGamePlayerController::SetCamera(UCameraComponent* InCamera)
{
	Camera = InCamera;
	if (Camera)
	{
		Camera->OnResize(static_cast<int32>(ViewportWidth), static_cast<int32>(ViewportHeight));
	}
}

void FGamePlayerController::SetFreeCamera(FViewportCamera* InCamera)
{
	FreeCamera = InCamera;
	bFreeCameraInitialized = false;
	SyncFreeCameraAngles();
}

AActor* FGamePlayerController::GetHeldNonCleaningToolActor() const
{
	if (PhysicsHandle == nullptr || !PhysicsHandle->IsHolding())
	{
		return nullptr;
	}

	if (!GGameContext::Get().GetCurrentToolId().empty())
	{
		return nullptr;
	}

	URigidBodyComponent* HeldBody = PhysicsHandle->GetHeldBody();
	return HeldBody ? HeldBody->GetOwner() : nullptr;
}

void FGamePlayerController::InitializeFreeCameraFromSnapshot(const FCameraSnapshot& Snapshot)
{
	if (!FreeCamera)
	{
		return;
	}

	FreeCamera->ClearCustomLookDir();
	FreeCamera->SetLocation(Snapshot.Location);
	FreeCamera->SetRotation(Snapshot.Rotation);
	FreeCamera->SetProjectionType(Snapshot.ProjectionType);
	FreeCamera->OnResize(Snapshot.Width, Snapshot.Height);
	FreeCamera->SetFOV(Snapshot.FOV);
	FreeCamera->SetNearPlane(Snapshot.NearPlane);
	FreeCamera->SetFarPlane(Snapshot.FarPlane);
	FreeCamera->SetOrthoHeight(Snapshot.OrthoHeight);

	bFreeCameraInitialized = false;
	SyncFreeCameraAngles();
}

void FGamePlayerController::BuildSceneView(FSceneView& OutView, const FViewportRect& ViewRect, EViewMode ViewMode) const
{
	if (Camera)
	{
		OutView.ViewMatrix = Camera->GetViewMatrix();
		OutView.ProjectionMatrix = Camera->GetProjectionMatrix();
		OutView.ViewProjectionMatrix = OutView.ViewMatrix * OutView.ProjectionMatrix;

		OutView.CameraPosition = Camera->GetWorldLocation();
		OutView.CameraForward = Camera->GetForwardVector();
		OutView.CameraRight = Camera->GetRightVector();
		OutView.CameraUp = Camera->GetUpVector();

		OutView.NearPlane = Camera->GetNearPlane();
		OutView.FarPlane = Camera->GetFarPlane();
		OutView.bOrthographic = Camera->IsOrthogonal();
		OutView.CameraOrthoHeight = Camera->GetOrthoWidth();
		OutView.CameraFrustum.UpdateFromCamera(OutView.ViewProjectionMatrix);
	}
	else if (FreeCamera)
	{
		OutView.ViewMatrix = FreeCamera->GetViewMatrix();
		OutView.ProjectionMatrix = FreeCamera->GetProjectionMatrix();
		OutView.ViewProjectionMatrix = OutView.ViewMatrix * OutView.ProjectionMatrix;

		OutView.CameraPosition = FreeCamera->GetLocation();
		OutView.CameraForward = FreeCamera->GetForwardVector();
		OutView.CameraRight = FreeCamera->GetRightVector();
		OutView.CameraUp = FreeCamera->GetUpVector();

		OutView.NearPlane = FreeCamera->GetNearPlane();
		OutView.FarPlane = FreeCamera->GetFarPlane();
		OutView.bOrthographic = FreeCamera->IsOrthographic();
		OutView.CameraOrthoHeight = FreeCamera->GetOrthoHeight();
		OutView.CameraFrustum = FreeCamera->GetFrustum();
	}

	OutView.ViewRect = ViewRect;
	OutView.ViewMode = ViewMode;
}

void FGamePlayerController::SetupDefaultInputMappings()
{
	InputMapping.Clear();

	// Action Mapping: 키 입력을 "한 번 발생하는 명령" 이름에 연결합니다.
	// 예: InputMapping.AddActionMapping(ActionInteract(), 'E');
	InputMapping.AddActionMapping(ActionToggleInputCapture(), VK_F4);
	InputMapping.AddActionMapping(ActionTogglePause(), 'P');
	InputMapping.AddActionMapping(ActionPickup(), 'E');
	InputMapping.AddActionMapping(ActionInspect(), 'Q');
	InputMapping.AddActionMapping(ActionJump(), VK_SPACE);

	// Axis Mapping: 여러 키를 하나의 연속 값으로 합칩니다.
	// 예를 들어 W는 MoveForward에 +1, S는 -1을 더합니다.
	// 키를 바꾸려면 문자 키나 VK_* 값을 바꾸고, 방향을 바꾸려면 Scale의 부호를 바꾸면 됩니다.
	// 예: 위쪽 화살표도 전진에 쓰려면 AddAxisMapping(AxisMoveForward(), VK_UP, 1.0f)를 추가합니다.
	InputMapping.AddAxisMapping(AxisMoveForward(), 'W', 1.0f);
	InputMapping.AddAxisMapping(AxisMoveForward(), 'S', -1.0f);
	InputMapping.AddAxisMapping(AxisMoveRight(), 'D', 1.0f);
	InputMapping.AddAxisMapping(AxisMoveRight(), 'A', -1.0f);

	InputMapping.AddAxisMapping(AxisLookYaw(), VK_RIGHT, 1.0f);
	InputMapping.AddAxisMapping(AxisLookYaw(), VK_LEFT, -1.0f);
	InputMapping.AddAxisMapping(AxisLookPitch(), VK_DOWN, 1.0f);
	InputMapping.AddAxisMapping(AxisLookPitch(), VK_UP, -1.0f);
}

void FGamePlayerController::ApplyInputAxes()
{
	if (!IsInputEnabled())
	{
		return;
	}

	const float MoveForwardValue = InputMapping.GetAxisValue(AxisMoveForward());
	const float MoveRightValue = InputMapping.GetAxisValue(AxisMoveRight());
	const bool bSprintHeld = IsSprintKeyHeld();

	// GetAxisValue가 매핑된 키들을 보고 MoveForwardValue 같은 의미 값만 돌려줍니다.
	// Axis 값은 -1~+1 범위의 값으로 사용합니다.
	// 실제 월드 방향은 현재 카메라의 forward/right/up 벡터로 변환합니다.
	const float Yaw = Camera ? Camera->GetYawDegrees() : FreeCameraYaw;
	const float Pitch = Camera ? Camera->GetPitchDegrees() : FreeCameraPitch;
	const FVector Forward = IsRuntimeWorld() ? ToForward(0.0f, Yaw) : ToForward(Pitch, Yaw);
	const FVector Right = ToRight(Yaw);

	const FVector Direction = Forward * MoveForwardValue + Right * MoveRightValue;
	if (!Direction.IsNearlyZero())
	{
		if (IsRuntimeWorld())
		{
			if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
			{
				MoveComp->SetActive(true);
				MoveComp->SetComponentTickEnabled(true);
				MoveComp->SetSpeedMultiplier(bSprintHeld ? 2.0f : 1.0f);
				MoveComp->AddInputVector(Direction.GetSafeNormal());
			}
			else
			{
				MoveActiveCamera(Direction.GetSafeNormal(), MoveSpeed * (bSprintHeld ? 2.0f : 1.0f) * DeltaTime);
			}
		}
		else
		{
			MoveActiveCamera(Direction.GetSafeNormal(), MoveSpeed * (bSprintHeld ? 2.0f : 1.0f) * DeltaTime);
		}
	}
	else if (IsRuntimeWorld())
	{
		if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
		{
			MoveComp->SetSpeedMultiplier(1.0f);
		}
	}

	if (!MathUtil::IsNearlyZero(RotateSensitivity))
	{
		// 화살표 키도 Axis로 읽어서 매 프레임 일정한 속도로 회전시킵니다.
		const float LookYawValue = InputMapping.GetAxisValue(AxisLookYaw());
		const float LookPitchValue = InputMapping.GetAxisValue(AxisLookPitch());
		const float RotateScale = 60.0f * DeltaTime / RotateSensitivity;
		if (!MathUtil::IsNearlyZero(LookYawValue) || !MathUtil::IsNearlyZero(LookPitchValue))
		{
			RotateActiveCamera(LookYawValue * RotateScale, LookPitchValue * RotateScale);
		}
	}
}

bool FGamePlayerController::TryBeginCleaningUse()
{
	if (!IsInputEnabled())
	{
		UE_LOG("[CleaningTool] BeginUse blocked: input disabled.");
		return false;
	}

	if (bIsCleaningUseHeld)
	{
		return true;
	}

	if (!PhysicsHandle || !PhysicsHandle->IsHolding())
	{
		UE_LOG("[CleaningTool] BeginUse blocked: no held physics object.");
		return false;
	}

	const FString& CurrentToolId = GGameContext::Get().GetCurrentToolId();
	if (CurrentToolId.empty())
	{
		UE_LOG("[CleaningTool] BeginUse blocked: current tool id is empty.");
		return false;
	}

	const FCleaningToolData* ToolData = FCleaningToolSystem::Get().FindToolData(CurrentToolId);
	if (!ToolData)
	{
		UE_LOG("[CleaningTool] BeginUse blocked: tool data not found for toolId=%s.", CurrentToolId.c_str());
		return false;
	}

	bIsCleaningUseHeld = true;
	if (URigidBodyComponent* HeldBody = PhysicsHandle->GetHeldBody())
	{
		HeldBody->SetAngularVelocity(FVector::ZeroVector);
	}
	FCleaningToolAnimator::Get().BeginUse(*ToolData);
	StartCleaningLoopSound(*ToolData);
	for (USubUVComponent* SubUV : CleaningToolSubUVs)
	{
		if (IsLiveObjectPointer(SubUV))
		{
			SubUV->Play();
			SubUV->SetVisibility(true);
		}
	}
	UE_LOG("[CleaningTool] BeginUse started: toolId=%s amplitude=%.3f speed=%.3f.",
		   CurrentToolId.c_str(),
		   ToolData->UseBobAmplitude,
		   ToolData->UseBobSpeed);
	return true;
}

void FGamePlayerController::EndCleaningUse()
{
	if (!bIsCleaningUseHeld)
	{
		return;
	}

	bIsCleaningUseHeld = false;
	FCleaningToolAnimator::Get().EndUse();
	StopCleaningLoopSound();
	for (USubUVComponent* SubUV : CleaningToolSubUVs)
	{
		if (IsLiveObjectPointer(SubUV))
		{
			SubUV->SetVisibility(false);
		}
	}
}

void FGamePlayerController::StartCleaningLoopSound(const FCleaningToolData& ToolData)
{
	LastSpongeUseStrokeCycle = -1;

	if (CleaningOneShotSoundHandle.IsValid())
	{
		FAudioSystem::Get().Stop(CleaningOneShotSoundHandle);
		CleaningOneShotSoundHandle = {};
	}

	if (ToolData.ToolId == SpongeToolId())
	{
		LastSpongeUseStrokeCycle = FCleaningToolAnimator::Get().GetUseStrokeCycleIndex();
		PlayNextSpongeCleaningSound();
		return;
	}

	if (ToolData.ToolId != MopToolId())
	{
		return;
	}

	if (CleaningLoopSoundHandle.IsValid())
	{
		FAudioSystem::Get().Stop(CleaningLoopSoundHandle);
		CleaningLoopSoundHandle = {};
	}

	CleaningLoopSoundHandle = FAudioSystem::Get().Play2D(MopCleaningLoopSoundPath(), 1.0f, true);
}

void FGamePlayerController::StopCleaningLoopSound()
{
	if (CleaningLoopSoundHandle.IsValid())
	{
		FAudioSystem::Get().Stop(CleaningLoopSoundHandle);
		CleaningLoopSoundHandle = {};
	}

	if (CleaningOneShotSoundHandle.IsValid())
	{
		FAudioSystem::Get().Stop(CleaningOneShotSoundHandle);
		CleaningOneShotSoundHandle = {};
	}

	LastSpongeUseStrokeCycle = -1;
}

void FGamePlayerController::UpdateCleaningUseSound()
{
	if (!bIsCleaningUseHeld || !FCleaningToolAnimator::Get().IsUsing())
	{
		return;
	}

	const FString& CurrentToolId = GGameContext::Get().GetCurrentToolId();
	if (CurrentToolId != SpongeToolId())
	{
		return;
	}

	const int CurrentStrokeCycle = FCleaningToolAnimator::Get().GetUseStrokeCycleIndex();
	if (LastSpongeUseStrokeCycle < 0)
	{
		LastSpongeUseStrokeCycle = CurrentStrokeCycle;
		return;
	}

	if (CurrentStrokeCycle == LastSpongeUseStrokeCycle)
	{
		return;
	}

	LastSpongeUseStrokeCycle = CurrentStrokeCycle;
	PlayNextSpongeCleaningSound();
}

void FGamePlayerController::PlayNextSpongeCleaningSound()
{
	const FString SoundPath = SpongeCleaningSoundPath(RandomSpongeSoundIndex());

	FAudioPlayParams Params;
	Params.bSpatial = false;
	Params.bLoop = false;
	Params.bAffectedByAudioZones = false;
	Params.Bus = EAudioBus::SFX;
	Params.Volume = 1.0f;
	CleaningOneShotSoundHandle = FAudioSystem::Get().Play(SoundPath, Params);
}

void FGamePlayerController::TogglePickup()
{
	if (!World || !IsInputEnabled())
	{
		return;
	}

	UPhysicsHandleComponent* Handle = GetPhysicsHandle();
	if (!Handle)
	{
		return;
	}

	if (Handle->IsHolding())
	{
		if (TryPlaceHeldItemInHoveredDecisionBox())
		{
			return;
		}

		UE_LOG("[CleaningTool] Releasing held object. currentToolId=%s", GGameContext::Get().GetCurrentToolId().c_str());
		EndCleaningUse();
		EndCleaningToolViewModel();
		FCleaningToolAnimator::Get().Reset();
		GGameContext::Get().SetCurrentTool("");
		Handle->Release();
		Handle->ResetHoldDistance();
		GGameContext::Get().ClearHeldObject();
		return;
	}

	FVector CameraLocation;
	FVector CameraForward;
	if (!GetActiveCameraFrame(CameraLocation, CameraForward))
	{
		return;
	}

    if (Handle->TryGrab(World, CameraLocation, CameraForward))
    {
        ResetHeldBodyRotationToInitial();
        bool bSelectedHeldTool = false;
        if (URigidBodyComponent* HeldBody = Handle->GetHeldBody())
        {
            if (AActor* HeldActor = HeldBody->GetOwner())
            {
                NotifyPickedUp(HeldActor);

                const FString HeldToolId = FindCleaningToolIdFromActor(HeldActor);
                const FString HeldItemId = FindItemIdFromActor(HeldActor);
                GGameContext::Get().SetHeldObject(HeldActor, HeldItemId, HeldToolId);
                bSelectedHeldTool = !HeldToolId.empty() && FCleaningToolSystem::Get().SelectTool(HeldToolId);
                if (bSelectedHeldTool)
                {
                    if (const FCleaningToolData* ToolData = FCleaningToolSystem::Get().FindToolData(HeldToolId))
                    {
                        Handle->SetHoldDistance(ToolData->HoldDistance, false);
                        Handle->SetHeldMovementCollisionSuppressed(true);
                        FCleaningToolAnimator::Get().SetActiveTool(*ToolData);
                        BeginCleaningToolViewModel(*ToolData);
                    }
                }
                UE_LOG("[CleaningTool] Picked actor=%s resolvedToolId=%s selected=%d",
                       HeldActor->GetFName().ToString().c_str(),
                       HeldToolId.c_str(),
                       bSelectedHeldTool ? 1 : 0);
            }
        }

		if (!bSelectedHeldTool)
		{
			UE_LOG("[CleaningTool] Picked object is not a cleaning tool. Clearing current tool.");
			EndCleaningToolViewModel();
			GGameContext::Get().SetCurrentTool("");
			FCleaningToolAnimator::Get().Reset();
			Handle->ResetHoldDistance();
		}
	}
}

bool FGamePlayerController::TryPlaceHeldItemInHoveredDecisionBox()
{
	UPhysicsHandleComponent* Handle = GetPhysicsHandle();
	if (World == nullptr || Handle == nullptr || !Handle->IsHolding())
	{
		return false;
	}

	if (!GGameContext::Get().GetCurrentToolId().empty())
	{
		return false;
	}

	URigidBodyComponent* HeldBody = Handle->GetHeldBody();
	AActor* HeldActor = HeldBody ? HeldBody->GetOwner() : nullptr;
	if (HeldActor == nullptr)
	{
		return false;
	}

	const FString ItemId = FindItemIdFromActor(HeldActor);
	if (ItemId.empty())
	{
		return false;
	}

	EItemDecisionBoxType BoxType = EItemDecisionBoxType::KeepBox;
	AActor* DecisionBoxActor = FindHoveredDecisionBoxActor(BoxType);
	if (DecisionBoxActor == nullptr)
	{
		return false;
	}

	const bool bPlacedInDecisionBox = FItemSystem::Get().PlaceItemInDecisionBox(ItemId, BoxType);
	const bool bAllowRepeatedResolvedItemPlacement =
		!bPlacedInDecisionBox
		&& (GGameContext::Get().HasKeptItem(ItemId) || GGameContext::Get().HasDiscardedItem(ItemId));
	if (!bPlacedInDecisionBox && !bAllowRepeatedResolvedItemPlacement)
	{
		return false;
	}

	if (const FGameItemData* ItemData = FItemSystem::Get().FindItemData(ItemId))
	{
		GameUISystem::Get().SetCurrentItem(ItemData->DisplayName.c_str(), FItemSystem::Get().GetDescriptionForCurrentState(ItemId).c_str());
	}
	GameUISystem::Get().SetItemCount(static_cast<int>(GGameContext::Get().GetResolvedItemCount()));
	GameUISystem::Get().HideItemInspect();

	EndCleaningUse();
	EndCleaningToolViewModel();
	FCleaningToolAnimator::Get().Reset();
	GGameContext::Get().SetCurrentTool("");
	Handle->Release();
	Handle->ResetHoldDistance();
	GGameContext::Get().ClearHeldObject();

	World->DeactivateActor(HeldActor);
	HoveredPickableActor = nullptr;
	HoveredDecisionBoxActor = nullptr;
	GameUISystem::Get().SetInteractionHint(EInteractionHintType::None);
	if (BoxType == EItemDecisionBoxType::DiscardBox && ItemId == "crumpled_paper")
	{
		GGameContext::Get().MarkTrashDiscardedForMission();
	}
	GGameContext::Get().RefreshCleanProgressFromDecals();
	GameUISystem::Get().SetProgress(GGameContext::Get().GetCleanProgress());

	UE_LOG("[ItemDecisionBox] Placed item=%s into box=%s actor=%s",
		   ItemId.c_str(),
		   BoxType == EItemDecisionBoxType::KeepBox ? "KeepBox" : "DiscardBox",
		   DecisionBoxActor->GetFName().ToString().c_str());
	return true;
}

void FGamePlayerController::TryInspectHoveredItem()
{
    if (!World || !IsInputEnabled())
    {
        return;
    }

    UPhysicsHandleComponent* Handle = GetPhysicsHandle();
    if (Handle == nullptr || !Handle->IsHolding())
    {
        return;
    }

    URigidBodyComponent* HeldBody = Handle->GetHeldBody();
    AActor* HeldActor = HeldBody ? HeldBody->GetOwner() : nullptr;
    if (HeldActor == nullptr || !FindCleaningToolIdFromActor(HeldActor, false).empty())
    {
        return;
    }

    const FString ItemId = FindItemIdFromActor(HeldActor);
    if (ItemId.empty())
    {
        return;
    }

    if (!FItemSystem::Get().InspectItem(ItemId))
    {
        return;
    }

    const FGameItemData* ItemData = FItemSystem::Get().FindItemData(ItemId);
    if (ItemData == nullptr)
    {
        return;
    }

    const FString Description = FItemSystem::Get().GetDescriptionForCurrentState(ItemId);
    GameUISystem::Get().ShowItemInspect(
        ItemData->DisplayName.c_str(),
        Description.c_str(),
        ItemData->IconPath.c_str());
}

void FGamePlayerController::NotifyPickedUp(AActor* PickedActor)
{
    if (PickedActor == nullptr)
    {
        return;
    }

    for (UActorComponent* Component : PickedActor->GetComponents())
    {
        ULuaScriptComponent* LuaComponent = Cast<ULuaScriptComponent>(Component);
        if (LuaComponent == nullptr)
        {
            continue;
        }

        LuaComponent->HandlePickedUp(Player);
    }
}

UPhysicsHandleComponent* FGamePlayerController::GetPhysicsHandle()
{
	RefreshPawnComponents();
	return PhysicsHandle;
}

UCharacterMovementComponent* FGamePlayerController::GetCharacterMovement()
{
	RefreshPawnComponents();
	return CharacterMovement;
}

void FGamePlayerController::DestroyPhysicsHandle()
{
	EndCleaningToolViewModel();
	StopCleaningLoopSound();
	if (PhysicsHandle)
	{
		PhysicsHandle->Release();
		PhysicsHandle = nullptr;
	}
	GGameContext::Get().ClearHeldObject();
	HoveredPickableActor = nullptr;
	HoveredDecisionBoxActor = nullptr;
}

void FGamePlayerController::RefreshPawnComponents()
{
	Camera = nullptr;
	PhysicsHandle = nullptr;
	CharacterMovement = nullptr;
	if (Player == nullptr)
	{
		return;
	}

	for (UActorComponent* Component : Player->GetComponents())
	{
		if (Camera == nullptr)
		{
			Camera = Cast<UCameraComponent>(Component);
			if (Camera != nullptr)
			{
				Camera->OnResize(static_cast<int32>(ViewportWidth), static_cast<int32>(ViewportHeight));
			}
		}

		if (PhysicsHandle == nullptr)
		{
			PhysicsHandle = Cast<UPhysicsHandleComponent>(Component);
		}

		if (CharacterMovement == nullptr)
		{
			CharacterMovement = Cast<UCharacterMovementComponent>(Component);
		}

		if (Camera != nullptr && PhysicsHandle != nullptr && CharacterMovement != nullptr)
		{
			break;
		}
	}
}

void FGamePlayerController::UpdateHoveredPickableActor()
{
	HoveredPickableActor = nullptr;
	HoveredDecisionBoxActor = nullptr;
	GameUISystem::Get().SetInteractionHint(EInteractionHintType::None);

	if (World == nullptr || !IsInputEnabled())
	{
		return;
	}

	UPhysicsHandleComponent* Handle = GetPhysicsHandle();
	if (Handle == nullptr)
	{
		return;
	}

	if (Handle->IsHolding())
	{
		EInteractionHintType HintType = EInteractionHintType::Drop;
		if (URigidBodyComponent* HeldBody = Handle->GetHeldBody())
		{
			if (AActor* HeldActor = HeldBody->GetOwner())
			{
				const FString HeldItemId = FindItemIdFromActor(HeldActor);
				if (GGameContext::Get().GetCurrentToolId().empty() && !HeldItemId.empty())
				{
					EItemDecisionBoxType BoxType = EItemDecisionBoxType::KeepBox;
					HoveredDecisionBoxActor = FindHoveredDecisionBoxActor(BoxType);
					if (HoveredDecisionBoxActor != nullptr)
					{
						HoveredDecisionBoxType = BoxType;
						HintType = BoxType == EItemDecisionBoxType::KeepBox
							? EInteractionHintType::Keep
							: EInteractionHintType::Discard;
					}
					else
					{
						HintType = IsItemInspectable(HeldItemId) ? EInteractionHintType::DropWithInspect : EInteractionHintType::Drop;
					}
				}
			}
		}
		GameUISystem::Get().SetInteractionHint(HintType);
		return;
	}

	FVector CameraLocation;
	FVector CameraForward;
	if (!GetActiveCameraFrame(CameraLocation, CameraForward))
	{
		return;
	}

	if (URigidBodyComponent* Body = Handle->FindPickableBody(World, CameraLocation, CameraForward))
	{
		HoveredPickableActor = Body->GetOwner();
		GameUISystem::Get().SetInteractionHint(EInteractionHintType::Pickup);
	}
}

AActor* FGamePlayerController::FindHoveredDecisionBoxActor(EItemDecisionBoxType& OutBoxType) const
{
	if (World == nullptr)
	{
		return nullptr;
	}

	FVector CameraLocation;
	FVector CameraForward;
	if (!GetActiveCameraFrame(CameraLocation, CameraForward))
	{
		return nullptr;
	}

	const AActor* IgnoredActor = nullptr;
	if (PhysicsHandle != nullptr && PhysicsHandle->IsHolding())
	{
		if (URigidBodyComponent* HeldBody = PhysicsHandle->GetHeldBody())
		{
			IgnoredActor = HeldBody->GetOwner();
		}
	}

	FHitResult Hit;
	constexpr float DecisionBoxTraceDistance = 6.0f;
	if (!World->LineTraceSingle(FRay(CameraLocation, CameraForward), DecisionBoxTraceDistance, Hit, IgnoredActor) || !Hit.IsValid())
	{
		return nullptr;
	}

	AActor* HitActor = Hit.HitComponent ? Hit.HitComponent->GetOwner() : nullptr;
	if (!TryGetDecisionBoxTypeFromActor(HitActor, OutBoxType))
	{
		return nullptr;
	}

	return HitActor;
}

bool FGamePlayerController::GetActiveCameraFrame(FVector& OutLocation, FVector& OutForward) const
{
	FVector Right;
	FVector Up;
	return GetActiveCameraBasis(OutLocation, OutForward, Right, Up);
}

bool FGamePlayerController::GetActiveCameraBasis(FVector& OutLocation, FVector& OutForward, FVector& OutRight, FVector& OutUp) const
{
	if (Camera != nullptr)
	{
		OutLocation = Camera->GetWorldLocation();
		OutForward = Camera->GetForwardVector();
		OutRight = Camera->GetRightVector();
		OutUp = Camera->GetUpVector();
		return !OutForward.IsNearlyZero();
	}

	if (FreeCamera != nullptr)
	{
		OutLocation = FreeCamera->GetLocation();
		OutForward = FreeCamera->GetForwardVector();
		OutRight = FreeCamera->GetRightVector();
		OutUp = FreeCamera->GetUpVector();
		return !OutForward.IsNearlyZero();
	}

	return false;
}

void FGamePlayerController::CaptureInitialRigidBodyRotations()
{
	if (bInitialRigidBodyRotationsCaptured || !IsRuntimeWorld())
	{
		return;
	}

	InitialRigidBodyRotations.clear();
	for (AActor* Actor : World->GetActors())
	{
		if (Actor == nullptr)
		{
			continue;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			URigidBodyComponent* Body = Cast<URigidBodyComponent>(Component);
			if (Body == nullptr)
			{
				continue;
			}

			if (USceneComponent* UpdatedComponent = Body->GetUpdatedComponent())
			{
				InitialRigidBodyRotations[Body] = UpdatedComponent->GetRelativeRotation();
			}
		}
	}

	bInitialRigidBodyRotationsCaptured = true;
}

void FGamePlayerController::ResetHeldBodyRotationToInitial()
{
	if (!IsRuntimeWorld() || PhysicsHandle == nullptr || !PhysicsHandle->IsHolding())
	{
		return;
	}

	URigidBodyComponent* Body = PhysicsHandle->GetHeldBody();
	if (Body == nullptr)
	{
		return;
	}

	USceneComponent* UpdatedComponent = Body->GetUpdatedComponent();
	if (UpdatedComponent == nullptr)
	{
		return;
	}

	auto It = InitialRigidBodyRotations.find(Body);
	if (It == InitialRigidBodyRotations.end())
	{
		It = InitialRigidBodyRotations.emplace(Body, UpdatedComponent->GetRelativeRotation()).first;
	}

	UpdatedComponent->SetRelativeRotation(It->second);
	Body->SetAngularVelocity(FVector::ZeroVector);
	FJoltPhysicsSystem::Get().SetBodyTransformFromComponent(Body);
}

bool FGamePlayerController::BeginCleaningToolViewModel(const FCleaningToolData& ToolData)
{
	UPhysicsHandleComponent* Handle = GetPhysicsHandle();
	if (Handle == nullptr || !Handle->IsHolding() || Player == nullptr || Camera == nullptr)
	{
		return false;
	}

	URigidBodyComponent* HeldBody = Handle->GetHeldBody();
	AActor* HeldActor = HeldBody ? HeldBody->GetOwner() : nullptr;
	if (!IsLiveObjectPointer(HeldActor))
	{
		return false;
	}

	UStaticMeshComponent* SourceMeshComponent = FindFirstVisibleStaticMesh(HeldActor);
	if (SourceMeshComponent == nullptr || SourceMeshComponent->GetStaticMesh() == nullptr)
	{
		return false;
	}

	if (CleaningToolViewModel != nullptr && !IsLiveObjectPointer(CleaningToolViewModel))
	{
		CleaningToolViewModel = nullptr;
	}

	if (CleaningToolViewModel == nullptr)
	{
		CleaningToolViewModel = Player->AddComponent<UStaticMeshComponent>();
		CleaningToolViewModel->SetTransient(true);
		CleaningToolViewModel->SetSceneQueryEnabled(false);
		CleaningToolViewModel->SetEnableCull(false);
		CleaningToolViewModel->SetBlockComponent(false);
	CleaningToolViewModel->SetGenerateOverlapEvents(false);
	}

	CleaningToolViewModel->SetParent(nullptr);
	CleaningToolViewModel->SetStaticMesh(SourceMeshComponent->GetStaticMesh());
	CleaningToolViewModel->SetRelativeScale(SourceMeshComponent->GetWorldScale());
	CleaningToolViewModel->SetVisibility(true);
	FVector CameraLocation;
	FVector CameraForward;
	FVector CameraRight;
	FVector CameraUp;
	if (GetActiveCameraBasis(CameraLocation, CameraForward, CameraRight, CameraUp))
	{
		UpdateCleaningToolViewModel(
			ToolData,
			FCleaningToolAnimator::Get().GetHoldCameraLocalOffset() + FCleaningToolAnimator::Get().GetCameraLocalOffset(),
			CameraLocation,
			CameraForward,
			CameraRight);
	}

	HiddenCleaningToolPrimitives.clear();
	HiddenCleaningToolPrimitiveVisibility.clear();
	for (UPrimitiveComponent* Primitive : HeldActor->GetPrimitiveComponents())
	{
		if (Cast<UStaticMeshComponent>(Primitive) == nullptr)
		{
			continue;
		}

		HiddenCleaningToolPrimitives.push_back(Primitive);
		HiddenCleaningToolPrimitiveVisibility.push_back(Primitive->IsVisible());
		Primitive->SetVisibility(false);
	}

	CleaningToolSubUVs.clear();
	CleaningToolSubUVOriginalParents.clear();
	for (UActorComponent* Comp : HeldActor->GetComponents())
	{
		USubUVComponent* SubUV = Cast<USubUVComponent>(Comp);
		if (!SubUV)
		{
			continue;
		}
		CleaningToolSubUVOriginalParents.push_back(SubUV->GetParent());
		SubUV->SetParent(CleaningToolViewModel);
		SubUV->SetVisibility(false);
		CleaningToolSubUVs.push_back(SubUV);
	}

	return true;
}

void FGamePlayerController::EndCleaningToolViewModel()
{
	for (size_t i = 0; i < CleaningToolSubUVs.size(); ++i)
	{
		USubUVComponent* SubUV = CleaningToolSubUVs[i];
		if (!IsLiveObjectPointer(SubUV))
		{
			continue;
		}
		SubUV->SetVisibility(false);
		USceneComponent* OrigParent = i < CleaningToolSubUVOriginalParents.size() ? CleaningToolSubUVOriginalParents[i] : nullptr;
		SubUV->SetParent(IsLiveObjectPointer(OrigParent) ? OrigParent : nullptr);
	}
	CleaningToolSubUVs.clear();
	CleaningToolSubUVOriginalParents.clear();

	for (size_t Index = 0; Index < HiddenCleaningToolPrimitives.size(); ++Index)
	{
		UPrimitiveComponent* Primitive = HiddenCleaningToolPrimitives[Index];
		if (!IsLiveObjectPointer(Primitive))
		{
			continue;
		}

		const bool bWasVisible = Index < HiddenCleaningToolPrimitiveVisibility.size()
									 ? HiddenCleaningToolPrimitiveVisibility[Index]
									 : true;
		Primitive->SetVisibility(bWasVisible);
	}
	HiddenCleaningToolPrimitives.clear();
	HiddenCleaningToolPrimitiveVisibility.clear();

	if (CleaningToolViewModel != nullptr && IsLiveObjectPointer(CleaningToolViewModel))
	{
		CleaningToolViewModel->SetParent(nullptr);
		CleaningToolViewModel->SetVisibility(false);
		AActor* ViewModelOwner = CleaningToolViewModel->GetOwner();
		if (IsLiveObjectPointer(ViewModelOwner))
		{
			ViewModelOwner->RemoveComponent(CleaningToolViewModel);
		}
	}

	CleaningToolViewModel = nullptr;
}

void FGamePlayerController::UpdateCleaningToolViewModel(
	const FCleaningToolData& ToolData,
	const FVector& CameraLocalOffset,
	const FVector& CameraLocation,
	const FVector& CameraForward,
	const FVector& CameraRight)
{
	if (!IsLiveObjectPointer(CleaningToolViewModel) || !IsLiveObjectPointer(Camera))
	{
		CleaningToolViewModel = nullptr;
		return;
	}

	const FVector TiltedCameraLocalOffset = ApplyCleaningToolLocalRotationToStrokeOffset(ToolData, CameraLocalOffset);
	const FVector LocalOffset = FVector(ToolData.HoldDistance, 0.0f, 0.0f) + TiltedCameraLocalOffset;
	if (CleaningToolViewModel->GetParent() != nullptr)
	{
		CleaningToolViewModel->SetParent(nullptr);
	}

	const FVector CameraScreenUp = BuildCameraScreenUp(CameraForward, CameraRight);
	CleaningToolViewModel->SetWorldLocation(CameraLocation + ToWorldOffset(CameraForward, CameraRight, CameraScreenUp, LocalOffset));
	CleaningToolViewModel->SetRelativeRotationQuat(BuildViewModelWorldRotation(
		CameraForward,
		CameraRight,
		GetCleaningToolAnimationDirection(ToolData),
		ToolData.ViewModelLocalRotationDegrees));
}

bool FGamePlayerController::IsRuntimeWorld() const
{
	if (World == nullptr)
	{
		return false;
	}

	const EWorldType WorldType = World->GetWorldType();
	return WorldType == EWorldType::PIE || WorldType == EWorldType::Game;
}

void FGamePlayerController::RotateActiveCamera(float DeltaX, float DeltaY)
{
	if (Camera)
	{
		Camera->AddYawInput(DeltaX * RotateSensitivity);
		Camera->AddPitchInput(-DeltaY * RotateSensitivity);
		return;
	}

	if (!FreeCamera)
	{
		return;
	}

	SyncFreeCameraAngles();
	FreeCameraYaw += DeltaX * RotateSensitivity;
	FreeCameraPitch -= DeltaY * RotateSensitivity;
	FreeCameraPitch = MathUtil::Clamp(FreeCameraPitch, -89.0f, 89.0f);
	UpdateFreeCameraRotation();
}

void FGamePlayerController::MoveActiveCamera(const FVector& Direction, float Scale)
{
	if (Player)
	{
		Player->AddActorWorldOffset(Direction * Scale);
		return;
	}

	if (Camera)
	{
		Camera->SetWorldLocation(Camera->GetWorldLocation() + Direction * Scale);
		return;
	}

	if (FreeCamera)
	{
		FreeCamera->SetLocation(FreeCamera->GetLocation() + Direction * Scale);
	}
}

void FGamePlayerController::SyncFreeCameraAngles()
{
	if (!FreeCamera || bFreeCameraInitialized)
		return;

	const FVector Forward = FreeCamera->GetForwardVector().GetSafeNormal();
	FreeCameraPitch = MathUtil::RadiansToDegrees(std::asin(MathUtil::Clamp(Forward.Z, -1.0f, 1.0f)));
	FreeCameraYaw = MathUtil::RadiansToDegrees(std::atan2(Forward.Y, Forward.X));
	bFreeCameraInitialized = true;
}

void FGamePlayerController::UpdateFreeCameraRotation()
{
	if (!FreeCamera)
		return;

	const FVector Forward = ToForward(FreeCameraPitch, FreeCameraYaw);
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
	if (Right.IsNearlyZero())
		return;

	const FVector Up = FVector::CrossProduct(Forward, Right).GetSafeNormal();

	FMatrix RotationMatrix = FMatrix::Identity;
	RotationMatrix.SetAxes(Forward, Right, Up);

	FQuat Rotation(RotationMatrix);
	Rotation.Normalize();
	FreeCamera->SetRotation(Rotation);
}
