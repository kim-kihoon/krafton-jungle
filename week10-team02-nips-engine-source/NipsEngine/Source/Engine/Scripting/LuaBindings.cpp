#include "Scripting/LuaBindings.h"

#if WITH_LUA
#include "GameFramework/AActor.h"
#include "Component/CameraComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/DecalComponent.h"
#include "Component/PostProcessComponent.h"
#include "Component/KnockbackComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/Physics/RigidBodyComponent.h"
#include "Engine/Camera/PlayerCameraManager.h"
#include "Engine/Camera/Modifier/LuaCameraModifier.h"
#include "Engine/Camera/Modifier/CameraShakeModifier.h"
#include "Engine/Runtime/Engine.h"
#include "Game/GameEngine.h"
#include "Game/Viewport/GameViewportClient.h"
#include "Math/Vector.h"
#include "Math/Vector2.h"
#include "Math/Vector4.h"
#include "Math/Quat.h"
#include "Math/Matrix.h"
#include "Math/Color.h"
#include "Object/Object.h"
#include "Core/CollisionTypes.h"
#include "Core/Logger.h"
#include "Audio/AudioSystem.h"
#include "Engine/Input/InputRouter.h"
#include "Game/UI/GameUISystem.h"
#include "Game/UI/DialoguePanel.h"
#include "Game/Systems/GameContext.h"
#include "Game/Systems/TimeDilationSystem.h"
#include "Game/Systems/CleaningToolSystem.h"
#include "Game/Systems/ItemSystem.h"
#include "GameFramework/World.h"
#include "Scripting/LuaScriptSystem.h"

#include <cmath>

namespace
{
	FGameViewportClient* GetLuaGameViewport()
	{
		UGameEngine* GameEngine = Cast<UGameEngine>(GEngine);
		return GameEngine ? GameEngine->GetGameViewport() : nullptr;
	}

	APlayerCameraManager* GetLuaPlayerCameraManager()
	{
		FGameViewportClient* Viewport = GetLuaGameViewport();
		return Viewport ? &Viewport->GetPlayerCameraManager() : nullptr;
	}

	FQuat MakeLookAtRotation(const FVector& Location, const FVector& Target)
	{
		FVector Forward = (Target - Location).GetSafeNormal();
		if (Forward.IsNearlyZero())
		{
			return FQuat::Identity;
		}

		FVector Up = FVector::UpVector;
		if (std::abs(FVector::DotProduct(Forward, Up)) > 0.98f)
		{
			Up = FVector::RightVector;
		}

		const FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
		const FVector CorrectedUp = FVector::CrossProduct(Forward, Right).GetSafeNormal();

		FMatrix RotationMatrix = FMatrix::Identity;
		RotationMatrix.SetAxes(Forward, Right, CorrectedUp);

		FQuat Rotation(RotationMatrix);
		Rotation.Normalize();
		return Rotation;
	}

	AActor* FindRegisteredItemActor(UWorld* World, const FString& ItemId)
	{
		if (World == nullptr || ItemId.empty())
		{
			return nullptr;
		}

		for (AActor* Actor : World->GetActors())
		{
			if (Actor == nullptr)
			{
				continue;
			}

			const FString RegisteredItemId = FLuaScriptSystem::Get().GetStringGameStateValue("Item:" + Actor->GetFName().ToString());
			if (RegisteredItemId == ItemId)
			{
				return Actor;
			}
		}

		return nullptr;
	}

	URigidBodyComponent* FindFirstRigidBody(AActor* Actor)
	{
		if (Actor == nullptr)
		{
			return nullptr;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (URigidBodyComponent* Body = Cast<URigidBodyComponent>(Component))
			{
				return Body;
			}
		}

		return nullptr;
	}

	UKnockbackComponent* FindFirstKnockback(AActor* Actor)
	{
		if (Actor == nullptr)
		{
			return nullptr;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (UKnockbackComponent* Knockback = Cast<UKnockbackComponent>(Component))
			{
				return Knockback;
			}
		}

		return nullptr;
	}

	const char* ToLuaHeldObjectType(EGameHeldObjectType Type)
	{
		switch (Type)
		{
		case EGameHeldObjectType::Object:
			return "Object";
		case EGameHeldObjectType::Item:
			return "Item";
		case EGameHeldObjectType::CleaningTool:
			return "CleaningTool";
		default:
			return "None";
		}
	}
}

void RegisterLuaBindings(sol::state& Lua)
{
	Lua.set_function("Log", [](const FString& Message)
	{
		UE_LOG("[Lua] %s", Message.c_str());
	});

	Lua.set_function("PlaySound", [](const FString& SoundPath)
	{
		return static_cast<int32>(FAudioSystem::Get().Play2D(SoundPath).Id);
	});

	Lua.set_function("PlaySoundAt", [](const FString& SoundPath, const FVector& Location)
	{
		return static_cast<int32>(FAudioSystem::Get().PlayAtLocation(SoundPath, Location).Id);
	});

	Lua.set_function("StopSound", [](int32 HandleId)
	{
		FAudioSystem::Get().Stop(FAudioHandle{ static_cast<uint32>(HandleId) });
	});

	Lua.new_usertype<FVector>(
		"FVector",
		sol::constructors<FVector(), FVector(float, float, float)>(),
		"x", &FVector::X,
		"y", &FVector::Y,
		"z", &FVector::Z,
		"X", &FVector::X,  
		"Y", &FVector::Y,
		"Z", &FVector::Z
	);

	Lua.new_usertype<FQuat>(
		"FQuat",
		sol::constructors<FQuat(), FQuat(float, float, float, float)>(),
		"x", &FQuat::X,
		"y", &FQuat::Y,
		"z", &FQuat::Z,
		"w", &FQuat::W,
		"X", &FQuat::X,
		"Y", &FQuat::Y,
		"Z", &FQuat::Z,
		"W", &FQuat::W,
		"Normalize", &FQuat::Normalize,
		"GetForwardVector", &FQuat::GetForwardVector,
		"GetRightVector", &FQuat::GetRightVector,
		"GetUpVector", &FQuat::GetUpVector
	);

	Lua.new_usertype<FVector4>(
		"FVector4",
		sol::constructors<FVector4(), FVector4(float, float, float, float)>(),
		"x", &FVector4::X,
		"y", &FVector4::Y,
		"z", &FVector4::Z,
		"w", &FVector4::W,
		"X", &FVector4::X,
		"Y", &FVector4::Y,
		"Z", &FVector4::Z,
		"W", &FVector4::W
	);

	Lua.new_usertype<FColor>(
		"FColor",
		sol::constructors<FColor(), FColor(float, float, float, float)>(),
		"r", &FColor::r,
		"g", &FColor::g,
		"b", &FColor::b,
		"a", &FColor::a,
		"R", &FColor::R,
		"G", &FColor::G,
		"B", &FColor::B,
		"A", &FColor::A
	);

	Lua.new_usertype<FCameraViewInfo>(
		"FCameraViewInfo",
		sol::constructors<FCameraViewInfo()>(),
		"Location", &FCameraViewInfo::Location,
		"Rotation", &FCameraViewInfo::Rotation,
		"FOV", &FCameraViewInfo::FOV,
		"AspectRatio", &FCameraViewInfo::AspectRatio,
		"NearPlane", &FCameraViewInfo::NearPlane,
		"FarPlane", &FCameraViewInfo::FarPlane,
		"OrthoWidth", &FCameraViewInfo::OrthoWidth,
		"OrthoHeight", &FCameraViewInfo::OrthoHeight,
		"bOrthographic", &FCameraViewInfo::bOrthographic,
		"GetForwardVector", &FCameraViewInfo::GetForwardVector,
		"GetRightVector", &FCameraViewInfo::GetRightVector,
		"GetUpVector", &FCameraViewInfo::GetUpVector
	);

	Lua.set_function("MakeCameraView", [](const FVector& Location, const FVector& Target, sol::optional<float> FOV)
	{
		FCameraViewInfo View;
		View.Location = Location;
		View.Rotation = MakeLookAtRotation(Location, Target);
		if (FOV)
		{
			View.FOV = FOV.value();
		}
		return View;
	});

	Lua.set_function("SetCameraViewLookAt", [](FCameraViewInfo& View, const FVector& Location, const FVector& Target)
	{
		View.Location = Location;
		View.Rotation = MakeLookAtRotation(Location, Target);
	});

	Lua.new_usertype<UCameraComponent>(
		"UCameraComponent",
		"AddPitchInput", &UCameraComponent::AddPitchInput,
		"AddYawInput", &UCameraComponent::AddYawInput,
		"GetPitchDegrees", &UCameraComponent::GetPitchDegrees,
		"GetYawDegrees", &UCameraComponent::GetYawDegrees
	);

	Lua.new_usertype<FPostProcessSettings>(
		"FPostProcessSettings",
		"Gamma", &FPostProcessSettings::Gamma,
		"VignetteIntensity", &FPostProcessSettings::VignetteIntensity,
		"VignetteRadius", &FPostProcessSettings::VignetteRadius,
		"VignetteSoftness", &FPostProcessSettings::VignetteSoftness
	);

	Lua.new_usertype<FCameraOverlaySettings>(
		"FCameraOverlaySettings",
		"FadeColor", &FCameraOverlaySettings::FadeColor,
		"FadeAlpha", sol::property(
			[](const FCameraOverlaySettings& Settings) { return Settings.FadeColor.W; },
			[](FCameraOverlaySettings& Settings, float Alpha) { Settings.FadeColor.W = Alpha; }),
		"LetterBoxRatio", &FCameraOverlaySettings::LetterBoxRatio,
		"LetterboxRatio", &FCameraOverlaySettings::LetterBoxRatio
	);

	Lua.new_usertype<UObject>(
		"UObject",
		"GetUUID", &UObject::GetUUID,
		"GetName", [](UObject& Object) { return Object.GetFName().ToString(); }
	);

	Lua.new_usertype<AActor>(
		"AActor",
		sol::base_classes, sol::bases<UObject>(),
		"GetActorLocation", &AActor::GetActorLocation,
		"SetActorLocation", &AActor::SetActorLocation,
		"AddActorWorldOffset", &AActor::AddActorWorldOffset,
		"GetActorRotation", &AActor::GetActorRotation,
		"SetActorRotation", &AActor::SetActorRotation,
		"GetActorScale", &AActor::GetActorScale,
		"SetActorScale", &AActor::SetActorScale,
		"IsVisible", &AActor::IsVisible,
		"SetVisible", &AActor::SetVisible,
		"GetName", [](AActor& Actor) { return Actor.GetFName().ToString(); },
		"GetUUID", &AActor::GetUUID
	);

	Lua.set_function("GetCameraComponent", [](AActor* Actor) -> UCameraComponent*
	{
		if (!Actor)
		{
			return nullptr;
		}

		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (UCameraComponent* Camera = Cast<UCameraComponent>(Comp))
			{
				return Camera;
			}
		}

		return nullptr;
	});

	Lua.set_function("SetActorCollisionEnabled", [](AActor* Actor, bool bEnabled)
	{
		if (!Actor)
		{
			return false;
		}

		bool bChangedAny = false;
		for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
		{
			if (!Primitive)
			{
				continue;
			}

			Primitive->SetSceneQueryEnabled(bEnabled);
			bChangedAny = true;
		}

		return bChangedAny;
	});

	// -------------------------------------------------------
	// GameUI
	// -------------------------------------------------------
	Lua.set_function("SetUIState", [](const std::string& StateName)
	{
		if      (StateName == "None")      GameUISystem::Get().SetState(EGameUIState::None);
		else if (StateName == "StartMenu") GameUISystem::Get().SetState(EGameUIState::StartMenu);
		else if (StateName == "Prologue")  GameUISystem::Get().SetState(EGameUIState::Prologue);
		else if (StateName == "InGame")    GameUISystem::Get().SetState(EGameUIState::InGame);
		else if (StateName == "Ending")    GameUISystem::Get().SetState(EGameUIState::Ending);
	});

	Lua.set_function("GetUIState", []()
	{
		EGameUIState State = GameUISystem::Get().GetState();
		if      (State == EGameUIState::None)      return "None";
		else if (State == EGameUIState::StartMenu) return "StartMenu";
		else if (State == EGameUIState::Prologue)  return "Prologue";
		else if (State == EGameUIState::InGame)    return "InGame";
		else if (State == EGameUIState::Ending)    return "Ending";
		return "Unknown";
	});

	Lua.set_function("SetProgress", [](float Progress)
	{
		GameUISystem::Get().SetProgress(Progress);
	});

	Lua.set_function("SetCurrentItem", [](const std::string& Name, const std::string& Desc)
	{
		GameUISystem::Get().SetCurrentItem(Name.c_str(), Desc.c_str());
	});

	Lua.set_function("SetItemCount", [](int Count)
	{
		GameUISystem::Get().SetItemCount(Count);
	});

	Lua.set_function("SetElapsedTime", [](float Seconds)
	{
		GameUISystem::Get().SetElapsedTime(Seconds);
	});

	Lua.set_function("SetInteractionHint", [](const std::string& HintName)
	{
		if      (HintName == "None")    GameUISystem::Get().SetInteractionHint(EInteractionHintType::None);
		else if (HintName == "Pickup")  GameUISystem::Get().SetInteractionHint(EInteractionHintType::Pickup);
		else if (HintName == "Drop")    GameUISystem::Get().SetInteractionHint(EInteractionHintType::Drop);
		else if (HintName == "Throw")   GameUISystem::Get().SetInteractionHint(EInteractionHintType::Throw);
		else if (HintName == "Keep")    GameUISystem::Get().SetInteractionHint(EInteractionHintType::Keep);
		else if (HintName == "Discard") GameUISystem::Get().SetInteractionHint(EInteractionHintType::Discard);
		else if (HintName == "Wash")    GameUISystem::Get().SetInteractionHint(EInteractionHintType::Wash);
	});

	Lua.set_function("SetPauseMenuOpen", [](bool bOpen)
	{
		GameUISystem::Get().SetPauseMenuOpen(bOpen);
	});

	Lua.set_function("IsPauseMenuOpen", []()
	{
		return GameUISystem::Get().IsPauseMenuOpen();
	});

	// -------------------------------------------------------
	// Dialogue
	// -------------------------------------------------------
	Lua.set_function("ShowDialogue", [](const std::string& Speaker, const std::string& Text)
	{
		GameUISystem::Get().ShowDialogue(Speaker.c_str(), Text.c_str());
	});

	Lua.set_function("QueueDialogue", [](const std::string& Speaker, const std::string& Text)
	{
		GameUISystem::Get().QueueDialogue(Speaker.c_str(), Text.c_str());
	});

	Lua.set_function("HideDialogue", []()
	{
		GameUISystem::Get().HideDialogue();
	});

	Lua.set_function("IsDialogueActive", []()
	{
		return GameUISystem::Get().IsDialogueActive();
	});

	Lua.set_function("IsDialogueTextComplete", []()
	{
		return DialoguePanel::IsTextComplete();
	});

	Lua.set_function("AdvanceDialogue", []()
	{
		return DialoguePanel::AdvanceOrSkip();
	});

	Lua.set_function("GetEndingType", []()
	{
		EEndingType Type = GameUISystem::Get().GetEndingType();
		if      (Type == EEndingType::Good)   return "Good";
		else if (Type == EEndingType::Normal) return "Normal";
		else if (Type == EEndingType::Bad)    return "Bad";
		return "None";
	});

	// 아이템 상호작용
	Lua.set_function("PlaceItemInKeepBox", [](const std::string& ItemId)
	{
		return FItemSystem::Get().PlaceItemInDecisionBox(ItemId, EItemDecisionBoxType::KeepBox);
	});

	Lua.set_function("PlaceItemInDiscardBox", [](const std::string& ItemId)
	{
		return FItemSystem::Get().PlaceItemInDecisionBox(ItemId, EItemDecisionBoxType::DiscardBox);
	});

	Lua.set_function("ClassifyItem", [](const std::string& ItemId, const std::string& Disposition)
	{
		if (Disposition == "Kept")
		{
			return FItemSystem::Get().ClassifyItem(ItemId, EGameItemDisposition::Kept);
		}

		if (Disposition == "Discarded")
		{
			return FItemSystem::Get().ClassifyItem(ItemId, EGameItemDisposition::Discarded);
		}

		return false;
	});

	Lua.set_function("GetItemDisplayName", [](const std::string& ItemId)
	{
		const FGameItemData* ItemData = FItemSystem::Get().FindItemData(ItemId);
		return ItemData ? ItemData->DisplayName : FString();
	});

	Lua.set_function("GetItemDescription", [](const std::string& ItemId)
	{
		return FItemSystem::Get().GetDescriptionForCurrentState(ItemId);
	});

	Lua.set_function("GetResolvedItemCount", []()
	{
		return static_cast<int32>(GGameContext::Get().GetResolvedItemCount());
	});

	Lua.set_function("IsHoldingObject", []()
	{
		return GGameContext::Get().IsHoldingObject();
	});

	Lua.set_function("GetHeldActorName", []()
	{
		return GGameContext::Get().GetHeldObjectInfo().ActorName;
	});

	Lua.set_function("GetHeldItemId", []()
	{
		return GGameContext::Get().GetHeldObjectInfo().ItemId;
	});

	Lua.set_function("GetHeldToolId", []()
	{
		return GGameContext::Get().GetHeldObjectInfo().ToolId;
	});

	Lua.set_function("GetHeldObjectType", []()
	{
		return FString(ToLuaHeldObjectType(GGameContext::Get().GetHeldObjectInfo().ObjectType));
	});

	Lua.set_function("DeactivateActor", [](AActor* Actor)
	{
		if (!Actor)
		{
			return false;
		}

		UWorld* World = Actor->GetFocusedWorld();
		if (!World)
		{
			return false;
		}

		World->DeactivateActor(Actor);
		return true;
	});

	Lua.set_function("RegisterItemActor", [](AActor* Actor, const std::string& ItemId)
	{
		if (!Actor || ItemId.empty())
		{
			return false;
		}

		const FString ActorName = Actor->GetFName().ToString();
		const FString Key = "Item:" + ActorName;
		return FLuaScriptSystem::Get().SetStringGameStateValue(Key, ItemId);
	});

	Lua.set_function("GetRegisteredItemId", [](AActor* Actor)
	{
		if (!Actor)
		{
			return FString();
		}

		return FLuaScriptSystem::Get().GetStringGameStateValue("Item:" + Actor->GetFName().ToString());
	});

	Lua.set_function("HasRigidBodyComponent", [](AActor* Actor)
	{
		return FindFirstRigidBody(Actor) != nullptr;
	});

	Lua.set_function("DropRegisteredItemFromActor", [](const std::string& ItemId, AActor* SourceActor, sol::optional<FVector> Offset)
	{
		if (ItemId.empty() || SourceActor == nullptr)
		{
			return false;
		}

		UWorld* World = SourceActor->GetFocusedWorld();
		if (World == nullptr)
		{
			return false;
		}

		AActor* DropActor = FindRegisteredItemActor(World, ItemId);
		if (DropActor == nullptr)
		{
			return false;
		}

		if (!DropActor->IsActive())
		{
			return false;
		}

		const FVector DropOffset = Offset.value_or(FVector(0.0f, 0.0f, 0.45f));
		const FVector DropLocation = SourceActor->GetActorLocation() + DropOffset;
		DropActor->SetVisible(true);
		if (URigidBodyComponent* DropBody = FindFirstRigidBody(DropActor))
		{
			DropBody->SetPhysicsLocation(DropLocation);
			DropBody->SetVelocity(FVector::ZeroVector);
			DropBody->SetAngularVelocity(FVector::ZeroVector);
		}
		else
		{
			DropActor->SetActorLocation(DropLocation);
		}

		return true;
	});

	Lua.set_function("SelectCleaningTool", [](const std::string& ToolId)
	{
		return FCleaningToolSystem::Get().SelectTool(ToolId);
	});

	Lua.set_function("GetCurrentCleaningToolId", []()
	{
		return GGameContext::Get().GetCurrentToolId();
	});

	Lua.set_function("GetCurrentCleaningToolRadius", []()
	{
		const FString& ToolId = GGameContext::Get().GetCurrentToolId();
		const FCleaningToolData* ToolData = ToolId.empty() ? nullptr : FCleaningToolSystem::Get().FindToolData(ToolId);
		return ToolData ? ToolData->CleaningRadius : 0.0f;
	});

	Lua.set_function("GetCurrentCleaningToolPower", []()
	{
		const FString& ToolId = GGameContext::Get().GetCurrentToolId();
		const FCleaningToolData* ToolData = ToolId.empty() ? nullptr : FCleaningToolSystem::Get().FindToolData(ToolId);
		return ToolData ? ToolData->CleaningPower : 0.0f;
	});

	Lua.set_function("RegisterCleaningToolActor", [](AActor* Actor, const std::string& ToolId)
	{
		if (!Actor || ToolId.empty())
		{
			return false;
		}

		const FString ActorName = Actor->GetFName().ToString();
		const FString Key = "CleaningTool:" + ActorName;
		return FLuaScriptSystem::Get().SetStringGameStateValue(Key, ToolId);
	});

	Lua.set_function("TriggerHitStop", [](float Duration, sol::optional<float> Dilation)
	{
		FTimeDilationSystem::Get().TriggerHitStop(Duration, Dilation.value_or(0.0f));
	});

	Lua.set_function("TriggerKnockback", [](AActor* Actor, const FVector& Direction, float Strength, float Duration)
	{
		if (UKnockbackComponent* Knockback = FindFirstKnockback(Actor))
		{
			Knockback->TriggerKnockback(Direction, Strength, Duration);
			return true;
		}

		return false;
	});

	Lua.set_function("IsKnockbackActive", [](AActor* Actor)
	{
		if (UKnockbackComponent* Knockback = FindFirstKnockback(Actor))
		{
			return Knockback->IsKnockbackActive();
		}

		return false;
	});

	Lua.set_function("IsKnockbackControlLocked", [](AActor* Actor)
	{
		if (UKnockbackComponent* Knockback = FindFirstKnockback(Actor))
		{
			return Knockback->IsControlLocked();
		}

		return false;
	});

	Lua.set_function("StartSlomo", [](float TargetDilation, float HoldTime, sol::optional<float> BlendInTime, sol::optional<float> BlendOutTime)
	{
		FTimeDilationSystem::Get().StartSlomo(
			TargetDilation,
			HoldTime,
			BlendInTime.value_or(0.0f),
			BlendOutTime.value_or(0.0f));
	});

	Lua.set_function("StopSlomo", []()
	{
		FTimeDilationSystem::Get().StopSlomo();
	});

	Lua.set_function("GetGlobalTimeDilation", []()
	{
		return FTimeDilationSystem::Get().GetGlobalTimeDilation();
	});

	// 키 입력 (Windows Virtual Key Code)
	// 자주 쓰는 상수를 Lua 전역으로 노출
	Lua.set("KEY_SPACE",  0x20);
	Lua.set("KEY_ESCAPE", 0x1B);
	Lua.set("KEY_P",      0x50);
	Lua.set("KEY_R",      0x52);
	Lua.set("KEY_TAB",    0x09);
	Lua.set("KEY_ENTER",  0x0D);

	// 마우스 입력
	Lua.set("KEY_LEFT_MOUSE", 0x01);
	Lua.set("KEY_RIGHT_MOUSE", 0x02);

	Lua.set_function("GetKeyDown", [](int VK)
	{
		if (GGameContext::Get().IsCinematicInputBlocked()) return false;
		if (GameUISystem::Get().WantsMouseCursor()) return false;
		return FInputRouter::GetKeyDown(VK);
	});

	Lua.set_function("GetKey", [](int VK)
	{
		if (GGameContext::Get().IsCinematicInputBlocked()) return false;
		if (GameUISystem::Get().WantsMouseCursor()) return false;
		return FInputRouter::GetKey(VK);
	});

	Lua.set_function("GetKeyUp", [](int VK)
	{
		if (GGameContext::Get().IsCinematicInputBlocked()) return false;
		if (GameUISystem::Get().WantsMouseCursor()) return false;
		return FInputRouter::GetKeyUp(VK);
	});

	Lua.new_usertype<FHitResult>(
		"FHitResult",
		"HitComponent", &FHitResult::HitComponent,
		"Distance", &FHitResult::Distance,
		"Location", &FHitResult::Location,
		"Normal", &FHitResult::Normal,
		"FaceIndex", &FHitResult::FaceIndex,
		"bHit", &FHitResult::bHit,
		"IsValid", &FHitResult::IsValid,
		"GetHitActor", [](FHitResult& Hit) -> AActor*
		{
			return Hit.HitComponent ? Hit.HitComponent->GetOwner() : nullptr;
		},
		"GetDecalComponent", [](FHitResult& Hit) -> UDecalComponent*
		{
		if (!Hit.bHit || !Hit.HitComponent) return nullptr;

		// 1. 직접 맞은 게 데칼이면 바로 반환
		if (auto Decal = Cast<UDecalComponent>(Hit.HitComponent))
			return Decal;

		// 2. 맞은 컴포넌트의 액터를 가져옴
		AActor* Owner = Hit.HitComponent->GetOwner();
		if (!Owner) return nullptr;

		// 3. 액터가 가진 모든 컴포넌트를 순회하며 데칼을 찾음
		// 엔진 내부의 컴포넌트 리스트 접근 방식(예: Owner->GetComponents())에 따라 수정하세요.
		for (auto* Comp : Owner->GetComponents()) 
		{
			if (auto* DecalComp = Cast<UDecalComponent>(Comp))
			{
				return DecalComp;
			}
		}

		return nullptr;		}
	);

	Lua.new_usertype<UDecalComponent>(
		"UDecalComponent",
		"GetCleanPercentage", &UDecalComponent::GetCleanPercentage,
		"PaintAtWorldPos", [](UDecalComponent& Decal, const FVector& WorldPos, float WorldRadius, int Value)
		{
			FVector2 UV;
			if (!Decal.WorldPosToDecalUV(WorldPos, UV)) return;

			const FVector BaseSize = Decal.GetDecalSize();
			const FVector CurrentScale = Decal.GetRelativeScale();

			// WorldPosToDecalUV: Local.Y -> UV.X(U축) 이므로 가로 기준은 Y축
			const float RealWorldWidth = BaseSize.Y * CurrentScale.Y;

			if (RealWorldWidth > 0.001f)
			{
				float FinalUVRadius = WorldRadius / RealWorldWidth;
				Decal.PaintMask(UV, FinalUVRadius, static_cast<uint8>(Value));
			}
		}
	);

	Lua.new_usertype<APlayerCameraManager>(
		"APlayerCameraManager",
		"GetCameraView", [](APlayerCameraManager& Manager) { return Manager.GetCameraView(); },
		"SetManualCameraViewLookAt", [](APlayerCameraManager& Manager, const FVector& Location, const FVector& Target, sol::optional<float> FOV)
		{
			FCameraViewInfo View = Manager.GetCameraView();
			View.Location = Location;
			View.Rotation = MakeLookAtRotation(Location, Target);
			if (FOV)
			{
				View.FOV = FOV.value();
			}
			Manager.SetManualCameraView(View);
		},
		"ClearManualCameraView", &APlayerCameraManager::ClearManualCameraView,
		"StartCameraFade", [](APlayerCameraManager& Manager, const FVector& Color, float FromAlpha, float ToAlpha, float Duration, sol::optional<bool> bHold)
		{
			Manager.StartCameraFade(Color, FromAlpha, ToAlpha, Duration, bHold.value_or(false));
		},
		"SetManualCameraFade", &APlayerCameraManager::SetManualCameraFade,
		"StopCameraFade", &APlayerCameraManager::StopCameraFade,

		"StartLetterBox", &APlayerCameraManager::StartLetterBox,
		"SetLetterBox", &APlayerCameraManager::SetLetterBox,
		"ClearLetterBox", &APlayerCameraManager::ClearLetterBox,

		"StartCameraTransition", &APlayerCameraManager::StartCameraTransition,
		"StartCameraTransitionBezier", &APlayerCameraManager::StartCameraTransitionBezier,
		"StartCameraTransitionLookAtBezier", [](APlayerCameraManager& Manager, const FVector& ToLocation, const FVector& Target, float Duration, sol::optional<float> HandleDistance, sol::optional<float> UpOffset, sol::optional<float> SideOffset)
		{
			FCameraViewInfo FromView = Manager.GetCameraView();
			FCameraViewInfo ToView = FromView;
			ToView.Location = ToLocation;
			ToView.Rotation = MakeLookAtRotation(ToLocation, Target);

			const FVector Delta = ToView.Location - FromView.Location;
			const float Distance = Delta.Size();
			const FVector Direction = Distance > 0.001f ? Delta / Distance : FromView.GetForwardVector().GetSafeNormal();
			const float Handle = HandleDistance.value_or(Distance * 0.33f);
			const FVector Lift = FVector::UpVector * UpOffset.value_or(0.0f);
			const FVector MidPoint = (FromView.Location + ToView.Location) * 0.5f;
			FVector Outward = (MidPoint - Target).GetSafeNormal2D();
			if (Outward.IsNearlyZero())
			{
				Outward = FVector(-Direction.Y, Direction.X, 0.0f).GetSafeNormal2D();
			}

			const FVector Side = Outward * SideOffset.value_or(0.0f);
			const FVector ControlPointA = FromView.Location + Direction * Handle + Side + Lift;
			const FVector ControlPointB = ToView.Location - Direction * Handle + Side + Lift;

			Manager.StartCameraTransitionBezier(FromView, ToView, ControlPointA, ControlPointB, Duration);
		},
		"StopCameraTransition", &APlayerCameraManager::StopCameraTransition,
		"StopCameraShake",  &APlayerCameraManager::StopCameraShake,
		"IsCameraShaking",  &APlayerCameraManager::IsCameraShaking,
		"StartCameraShake", [](APlayerCameraManager& Manager, sol::table t)
		{
			FCameraShakeParams Params;

			auto ReadFloat = [&](const char* Key, float Default) -> float
			{
				sol::object v = t[Key];
				return (v.valid() && v.is<float>()) ? v.as<float>() : Default;
			};
			auto ReadBool = [&](const char* Key, bool Default) -> bool
			{
				sol::object v = t[Key];
				return (v.valid() && v.is<bool>()) ? v.as<bool>() : Default;
			};
			auto ReadArray = [&](const char* Key, float* Out, int Count)
			{
				sol::object v = t[Key];
				if (!v.valid() || !v.is<sol::table>()) return;
				sol::table arr = v.as<sol::table>();
				for (int i = 0; i < Count; ++i)
				{
					sol::object elem = arr[i + 1];
					if (elem.valid() && elem.is<float>()) Out[i] = elem.as<float>();
				}
			};

			Params.Duration = ReadFloat("Duration", Params.Duration);
			Params.bLoop    = ReadBool("bLoop", false);
			ReadArray("RotAmplitude", Params.RotAmplitude, 3);
			ReadArray("RotFrequency", Params.RotFrequency, 3);
			ReadArray("LocAmplitude", Params.LocAmplitude, 3);
			ReadArray("LocFrequency", Params.LocFrequency, 3);
			ReadArray("RotBezierCP",  Params.RotBezierCP,  6);
			ReadArray("LocBezierCP",  Params.LocBezierCP,  6);
			ReadArray("FOVBezierCP",  Params.FOVBezierCP,  6);
			Params.FOVAmplitude = ReadFloat("FOVAmplitude", Params.FOVAmplitude);
			Params.FOVFrequency = ReadFloat("FOVFrequency", Params.FOVFrequency);

			Manager.StartCameraShake(Params);
		}
	);

	Lua.set_function("GetPlayerCameraManager", []() -> APlayerCameraManager*
	{
		return GetLuaPlayerCameraManager();
	});

	Lua.set_function("AddLuaCameraModifier", [](const FString& ScriptPath)
	{
		APlayerCameraManager* CameraManager = GetLuaPlayerCameraManager();
		if (CameraManager == nullptr || ScriptPath.empty())
		{
			return false;
		}

		return CameraManager->AddLuaCameraModifier(ScriptPath) != nullptr;
	});

	Lua.set_function("SetCinematicInputBlocked", [](bool bBlocked)
	{
		GGameContext::Get().SetCinematicInputBlocked(bBlocked);
	});

	Lua.set_function("IsCinematicInputBlocked", []()
	{
		return GGameContext::Get().IsCinematicInputBlocked();
	});
	
	Lua.new_usertype<UPostProcessComponent>(
		"UPostProcessComponent",
		"SetVignetteEnabled", &UPostProcessComponent::SetVignetteEnabled,
		"IsVignetteEnabled", &UPostProcessComponent::IsEnableVignette,
		"SetVignette", &UPostProcessComponent::SetVignette,
		"SetVignetteIntensity", &UPostProcessComponent::SetVignetteIntensity,
		"GetVignetteIntensity", &UPostProcessComponent::GetVignetteIntensity,
		"SetVignetteRadius", &UPostProcessComponent::SetVignetteRadius,
		"GetVignetteRadius", &UPostProcessComponent::GetVignetteRadius,
		"SetVignetteSoftness", &UPostProcessComponent::SetVignetteSoftness,
		"GetVignetteSoftness", &UPostProcessComponent::GetVignetteSoftness,
		"SetGammaCorrectionEnabled", &UPostProcessComponent::SetGammaCorrectionEnabled,
		"IsGammaCorrectionEnabled", &UPostProcessComponent::IsEnableGammaCorrection,
		"SetGamma", &UPostProcessComponent::SetGamma,
		"GetGamma", &UPostProcessComponent::GetGamma
	);

	Lua.set_function("GetPostProcessComponent", [](AActor* Actor) -> UPostProcessComponent*
	{
		if (!Actor) return nullptr;
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (UPostProcessComponent* PostProcess = Cast<UPostProcessComponent>(Comp))
				return PostProcess;
		}
		return nullptr;
	});

	// -------------------------------------------------------
	// SubUV Component (오염도 시각화용 색상 틴트)
	// -------------------------------------------------------
	Lua.new_usertype<USubUVComponent>(
		"USubUVComponent",
		"SetTintColor", [](USubUVComponent& Comp, float R, float G, float B)
		{
			Comp.SetTintColor(FVector4(R, G, B, 1.0f));
		},
		"GetTintR", [](USubUVComponent& Comp) { return Comp.GetTintColor().X; },
		"GetTintG", [](USubUVComponent& Comp) { return Comp.GetTintColor().Y; },
		"GetTintB", [](USubUVComponent& Comp) { return Comp.GetTintColor().Z; }
	);

	// 액터에서 첫 번째 SubUVComponent 찾기
	Lua.set_function("GetSubUVComponent", [](AActor* Actor) -> USubUVComponent*
	{
		if (!Actor) return nullptr;
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (USubUVComponent* SubUV = Cast<USubUVComponent>(Comp))
				return SubUV;
		}
		return nullptr;
	});

	// 액터의 모든 SubUVComponent에 동시에 색상 적용 (오염도 시각화용)
	Lua.set_function("SetAllSubUVTints", [](AActor* Actor, float R, float G, float B)
	{
		if (!Actor) return;
		FVector4 Tint(R, G, B, 1.0f);
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (USubUVComponent* SubUV = Cast<USubUVComponent>(Comp))
				SubUV->SetTintColor(Tint);
		}
	});

}
#endif
