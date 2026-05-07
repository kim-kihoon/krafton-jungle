#pragma once

#include "Engine/Core/CoreMinimal.h"

enum class EGameItemDisposition
{
	None,
	Found,
	Kept,
	Discarded,
};

enum class EItemDecisionBoxType
{
	KeepBox,
	DiscardBox,
};

enum class EGameItemType
{
	StoryItem,
	DummyItem,
	CleaningTool,
};

enum class EGameItemEndingRole
{
	None,
	Bad,
	Normal,
	Good,
};

struct FGameItemData
{
	FString ItemId;
	FString DisplayName;
	EGameItemType ItemType = EGameItemType::StoryItem;
	EGameItemEndingRole EndingRole = EGameItemEndingRole::None;
	FString DescriptionWhenFound;
	FString DescriptionWhenKept;
	FString DescriptionWhenDiscarded;
	FString IconPath;
	FString PickSoundId;
	FString DropSoundId;
	FString FirstFoundSoundId;
	bool bCanClassify = true;
	bool bCanInspect = true;
	TArray<FString> StoryFlags;
};

struct FCleaningToolData
{
	FString ToolId;
	FString DisplayName;
	FString AnimationSetId;
	FString EffectId;
	float CleaningPower = 1.0f;
	float CleaningRadius = 0.8f;
	float HoldDistance = 4.0f;
	FVector HoldCameraLocalOffset = FVector::ZeroVector;
	FVector UseStrokeCameraLocalDirection = FVector(0.0f, 0.0f, 1.0f);
	FVector HandleCameraLocalDirection = FVector::ZeroVector;
	FVector ViewModelLocalRotationDegrees = FVector::ZeroVector;
	float UseBobAmplitude = 0.15f;
	float UseBobSpeed = 8.0f;
	float UseReturnSpeed = 14.0f;
	TArray<FString> ValidSurfaceTypes;
	FString InteractionSoundId;
};

struct FCleaningToolUseResult
{
	FString ToolId;
	FString AnimationSetId;
	FString EffectId;
	FString InteractionSoundId;
	float CleaningPower = 0.0f;
	float CleaningRadius = 0.0f;
	bool bCanCleanSurface = true;
};

struct FEndingResult
{
	FString EndingId;
	TArray<FString> KeptBadItemIds;
	TArray<FString> KeptNormalItemIds;
	TArray<FString> KeptGoodItemIds;
};
