#include "Game/Systems/CleaningToolSystem.h"

#include "Core/Logger.h"
#include "Game/Systems/GameContext.h"

FCleaningToolSystem& FCleaningToolSystem::Get()
{
	static FCleaningToolSystem Instance;
	return Instance;
}

void FCleaningToolSystem::ClearToolData()
{
	Tools.clear();
	UE_LOG("[CleaningTool] Cleared registered tool data.");
}

void FCleaningToolSystem::RegisterToolData(const FCleaningToolData& ToolData)
{
	if (ToolData.ToolId.empty())
	{
		UE_LOG("[CleaningTool] RegisterToolData skipped: empty tool id.");
		return;
	}

	for (FCleaningToolData& ExistingTool : Tools)
	{
		if (ExistingTool.ToolId == ToolData.ToolId)
		{
			ExistingTool = ToolData;
			UE_LOG("[CleaningTool] Updated tool data: toolId=%s animation=%s tools=%d",
				ToolData.ToolId.c_str(),
				ToolData.AnimationSetId.c_str(),
				static_cast<int32>(Tools.size()));
			return;
		}
	}

	Tools.push_back(ToolData);
	UE_LOG("[CleaningTool] Registered tool data: toolId=%s animation=%s tools=%d",
		ToolData.ToolId.c_str(),
		ToolData.AnimationSetId.c_str(),
		static_cast<int32>(Tools.size()));
}

const FCleaningToolData* FCleaningToolSystem::FindToolData(const FString& ToolId) const
{
	for (const FCleaningToolData& Tool : Tools)
	{
		if (Tool.ToolId == ToolId)
		{
			return &Tool;
		}
	}

	return nullptr;
}

bool FCleaningToolSystem::SelectTool(const FString& ToolId)
{
	if (!FindToolData(ToolId))
	{
		FString RegisteredToolIds;
		for (const FCleaningToolData& Tool : Tools)
		{
			if (!RegisteredToolIds.empty())
			{
				RegisteredToolIds += ", ";
			}
			RegisteredToolIds += Tool.ToolId;
		}

		UE_LOG("[CleaningTool] SelectTool failed: requested=%s registeredCount=%d registered=[%s]",
			ToolId.c_str(),
			static_cast<int32>(Tools.size()),
			RegisteredToolIds.c_str());
		return false;
	}

	GGameContext::Get().SetCurrentTool(ToolId);
	UE_LOG("[CleaningTool] SelectTool succeeded: toolId=%s", ToolId.c_str());
	return true;
}

FCleaningToolUseResult FCleaningToolSystem::BuildUseResult(const FString& SurfaceType) const
{
	FCleaningToolUseResult Result;
	const FCleaningToolData* ToolData = FindToolData(GGameContext::Get().GetCurrentToolId());
	if (!ToolData)
	{
		Result.bCanCleanSurface = false;
		return Result;
	}

	Result.ToolId = ToolData->ToolId;
	Result.AnimationSetId = ToolData->AnimationSetId;
	Result.EffectId = ToolData->EffectId;
	Result.InteractionSoundId = ToolData->InteractionSoundId;
	Result.CleaningPower = ToolData->CleaningPower;
	Result.CleaningRadius = ToolData->CleaningRadius;
	Result.bCanCleanSurface = CanCleanSurface(*ToolData, SurfaceType);
	return Result;
}

bool FCleaningToolSystem::CanCleanSurface(const FCleaningToolData& ToolData, const FString& SurfaceType) const
{
	if (SurfaceType.empty() || ToolData.ValidSurfaceTypes.empty())
	{
		return true;
	}

	for (const FString& ValidSurfaceType : ToolData.ValidSurfaceTypes)
	{
		if (ValidSurfaceType == SurfaceType)
		{
			return true;
		}
	}

	return false;
}
