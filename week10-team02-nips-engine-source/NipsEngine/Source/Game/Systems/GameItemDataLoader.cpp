#include "Game/Systems/GameItemDataLoader.h"

#include "Game/Systems/CleaningToolSystem.h"
#include "Game/Systems/ItemSystem.h"
#include "Engine/Core/Logger.h"
#include "Engine/Core/Paths.h"
#include "SimpleJSON/json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
	FString GetStringField(const json::JSON& Node, const char* Key)
	{
		if (!Node.hasKey(Key))
		{
			return "";
		}

		return Node.at(Key).ToString();
	}

	bool GetBoolField(const json::JSON& Node, const char* Key, bool DefaultValue)
	{
		if (!Node.hasKey(Key))
		{
			return DefaultValue;
		}

		bool bOk = false;
		const bool Value = Node.at(Key).ToBool(bOk);
		return bOk ? Value : DefaultValue;
	}

	float GetFloatField(const json::JSON& Node, const char* Key, float DefaultValue)
	{
		if (!Node.hasKey(Key))
		{
			return DefaultValue;
		}

		bool bOk = false;
		float Value = static_cast<float>(Node.at(Key).ToFloat(bOk));
		if (bOk)
		{
			return Value;
		}

		Value = static_cast<float>(Node.at(Key).ToInt(bOk));
		return bOk ? Value : DefaultValue;
	}

	FVector GetVectorField(const json::JSON& Node, const char* Key, const FVector& DefaultValue)
	{
		if (!Node.hasKey(Key))
		{
			return DefaultValue;
		}

		const json::JSON& ArrayNode = Node.at(Key);
		if (ArrayNode.JSONType() != json::JSON::Class::Array || ArrayNode.size() < 3)
		{
			return DefaultValue;
		}

		auto ReadNumber = [](const json::JSON& ValueNode, float DefaultNumber)
		{
			bool bOk = false;
			float Result = static_cast<float>(ValueNode.ToFloat(bOk));
			if (bOk)
			{
				return Result;
			}

			Result = static_cast<float>(ValueNode.ToInt(bOk));
			return bOk ? Result : DefaultNumber;
		};

		float Components[3] = { DefaultValue.X, DefaultValue.Y, DefaultValue.Z };
		int32 Index = 0;
		for (const json::JSON& ValueNode : ArrayNode.ArrayRange())
		{
			if (Index >= 3)
			{
				break;
			}

			Components[Index] = ReadNumber(ValueNode, Components[Index]);
			++Index;
		}

		return FVector(Components[0], Components[1], Components[2]);
	}

	EGameItemType ParseItemType(const FString& TypeName)
	{
		if (TypeName == "DummyItem")
		{
			return EGameItemType::DummyItem;
		}

		if (TypeName == "CleaningTool")
		{
			return EGameItemType::CleaningTool;
		}

		return EGameItemType::StoryItem;
	}

	EGameItemEndingRole ParseEndingRole(const FString& RoleName)
	{
		if (RoleName == "Bad")
		{
			return EGameItemEndingRole::Bad;
		}

		if (RoleName == "Normal")
		{
			return EGameItemEndingRole::Normal;
		}

		if (RoleName == "Good")
		{
			return EGameItemEndingRole::Good;
		}

		return EGameItemEndingRole::None;
	}

	TArray<FString> ReadStringArray(const json::JSON& Node, const char* Key)
	{
		TArray<FString> Values;
		if (!Node.hasKey(Key))
		{
			return Values;
		}

		const json::JSON& ArrayNode = Node.at(Key);
		if (ArrayNode.JSONType() != json::JSON::Class::Array)
		{
			return Values;
		}

		for (const json::JSON& ValueNode : ArrayNode.ArrayRange())
		{
			bool bOk = false;
			const FString Value = ValueNode.ToString(bOk);
			if (bOk && !Value.empty())
			{
				Values.push_back(Value);
			}
		}

		return Values;
	}

	FString JoinDescriptionLines(const TArray<FString>& Lines)
	{
		FString Result;
		for (size_t Index = 0; Index < Lines.size(); ++Index)
		{
			if (Index > 0)
			{
				Result += "\n";
			}
			Result += Lines[Index];
		}
		return Result;
	}

	FString ReadFoundDescription(const json::JSON& Node)
	{
		if (Node.hasKey("foundDesc"))
		{
			return Node.at("foundDesc").ToString();
		}

		return JoinDescriptionLines(ReadStringArray(Node, "desc"));
	}

	FGameItemData ReadItemData(const json::JSON& Node)
	{
		FGameItemData ItemData;
		ItemData.ItemId = GetStringField(Node, "id");
		ItemData.DisplayName = GetStringField(Node, "name");
		ItemData.ItemType = ParseItemType(GetStringField(Node, "type"));
		ItemData.EndingRole = ParseEndingRole(GetStringField(Node, "endingRole"));
		ItemData.DescriptionWhenFound = ReadFoundDescription(Node);
		ItemData.DescriptionWhenKept = GetStringField(Node, "keptDesc");
		ItemData.DescriptionWhenDiscarded = GetStringField(Node, "discardedDesc");
		ItemData.IconPath = GetStringField(Node, "iconPath");
		ItemData.PickSoundId = GetStringField(Node, "pickSoundId");
		ItemData.DropSoundId = GetStringField(Node, "dropSoundId");
		ItemData.FirstFoundSoundId = GetStringField(Node, "firstFoundSoundId");
		ItemData.bCanClassify = GetBoolField(Node, "canClassify", true);
		ItemData.bCanInspect = GetBoolField(Node, "canInspect", true);
		ItemData.StoryFlags = ReadStringArray(Node, "storyFlags");
		return ItemData;
	}

	FCleaningToolData ReadCleaningToolData(const json::JSON& Node, const FGameItemData& ItemData)
	{
		FCleaningToolData ToolData;
		ToolData.ToolId = ItemData.ItemId;
		ToolData.DisplayName = ItemData.DisplayName;
		ToolData.AnimationSetId = GetStringField(Node, "animationSetId");
		ToolData.EffectId = GetStringField(Node, "effectId");
		ToolData.InteractionSoundId = GetStringField(Node, "interactionSoundId");
		ToolData.CleaningPower = std::max(0.0f, GetFloatField(Node, "cleaningPower", 1.0f));
		ToolData.CleaningRadius = std::max(0.0f, GetFloatField(Node, "cleaningRadius", 0.8f));
		ToolData.HoldDistance = std::max(0.1f, GetFloatField(Node, "holdDistance", 4.0f));
		ToolData.HoldCameraLocalOffset = GetVectorField(Node, "holdCameraLocalOffset", FVector::ZeroVector);
		ToolData.UseStrokeCameraLocalDirection = GetVectorField(Node, "useStrokeCameraLocalDirection", FVector(0.0f, 0.0f, 1.0f)).GetSafeNormal();
		if (ToolData.UseStrokeCameraLocalDirection.IsNearlyZero())
		{
			ToolData.UseStrokeCameraLocalDirection = FVector(0.0f, 0.0f, 1.0f);
		}
		ToolData.HandleCameraLocalDirection = GetVectorField(Node, "handleCameraLocalDirection", FVector::ZeroVector).GetSafeNormal();
		ToolData.ViewModelLocalRotationDegrees = GetVectorField(
			Node,
			"viewModelLocalRotationDegrees",
			FVector(GetFloatField(Node, "viewModelLocalXRotationDegrees", 0.0f), 0.0f, 0.0f));
		ToolData.UseBobAmplitude = std::max(0.0f, GetFloatField(Node, "useBobAmplitude", 0.15f));
		ToolData.UseBobSpeed = std::max(0.0f, GetFloatField(Node, "useBobSpeed", 8.0f));
		ToolData.UseReturnSpeed = std::max(0.0f, GetFloatField(Node, "useReturnSpeed", 14.0f));
		ToolData.ValidSurfaceTypes = ReadStringArray(Node, "validSurfaceTypes");
		return ToolData;
	}

	const json::JSON* FindItemsArray(const json::JSON& Root)
	{
		if (Root.JSONType() == json::JSON::Class::Array)
		{
			return &Root;
		}

		if (Root.JSONType() == json::JSON::Class::Object && Root.hasKey("items"))
		{
			const json::JSON& ItemsNode = Root.at("items");
			if (ItemsNode.JSONType() == json::JSON::Class::Array)
			{
				return &ItemsNode;
			}
		}

		return nullptr;
	}
}

bool FGameItemDataLoader::LoadFromFile(const FString& RelativePath, FItemSystem& ItemSystem)
{
	const std::wstring AbsolutePath = FPaths::ToAbsolute(FPaths::ToWide(RelativePath));
	std::ifstream File{ std::filesystem::path(AbsolutePath) };
	if (!File.is_open())
	{
		UE_LOG("GameItemDataLoader: failed to open '%s'.", RelativePath.c_str());
		return false;
	}

	std::stringstream Buffer;
	Buffer << File.rdbuf();

	json::JSON Root = json::JSON::Load(Buffer.str());
	const json::JSON* ItemsArray = FindItemsArray(Root);
	if (!ItemsArray)
	{
		UE_LOG("GameItemDataLoader: '%s' does not contain an items array.", RelativePath.c_str());
		return false;
	}

	int32 LoadedCount = 0;
	for (const json::JSON& ItemNode : ItemsArray->ArrayRange())
	{
		if (ItemNode.JSONType() != json::JSON::Class::Object)
		{
			continue;
		}

		FGameItemData ItemData = ReadItemData(ItemNode);
		if (ItemData.ItemId.empty())
		{
			UE_LOG("GameItemDataLoader: skipped item without id in '%s'.", RelativePath.c_str());
			continue;
		}

		ItemSystem.RegisterItemData(ItemData);
		if (ItemData.ItemType == EGameItemType::CleaningTool)
		{
			UE_LOG("[CleaningTool] Loading cleaning tool item: id=%s type=CleaningTool",
				ItemData.ItemId.c_str());
			FCleaningToolSystem::Get().RegisterToolData(ReadCleaningToolData(ItemNode, ItemData));
		}
		++LoadedCount;
	}

	UE_LOG("GameItemDataLoader: loaded %d items from '%s'.", LoadedCount, RelativePath.c_str());
	return LoadedCount > 0;
}
