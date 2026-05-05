#include "Scripting/LuaBindings.h"

#if WITH_LUA
#include "GameFramework/AActor.h"
#include "Component/PrimitiveComponent.h"
#include "Component/DecalComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/Physics/RigidBodyComponent.h"
#include "Math/Vector.h"
#include "Math/Vector2.h"
#include "Math/Vector4.h"
#include "Object/Object.h"
#include "Core/CollisionTypes.h"
#include "Core/Logger.h"
#include "Audio/AudioSystem.h"
#include "Engine/Input/InputRouter.h"
#include "Game/UI/GameUISystem.h"
#include "Game/Systems/GameContext.h"
#include "Game/Systems/CleaningToolSystem.h"
#include "Game/Systems/ItemSystem.h"
#include "GameFramework/World.h"
#include "Scripting/LuaScriptSystem.h"

namespace
{
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
		if (GameUISystem::Get().WantsMouseCursor()) return false;
		return FInputRouter::GetKeyDown(VK);
	});

	Lua.set_function("GetKey", [](int VK)
	{
		if (GameUISystem::Get().WantsMouseCursor()) return false;
		return FInputRouter::GetKey(VK);
	});

	Lua.set_function("GetKeyUp", [](int VK)
	{
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
