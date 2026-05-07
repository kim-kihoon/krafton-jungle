#pragma once

#include "Game/Systems/CleaningGameTypes.h"

class FCleaningToolSystem
{
public:
	static FCleaningToolSystem& Get();

	void ClearToolData();
	void RegisterToolData(const FCleaningToolData& ToolData);
	const FCleaningToolData* FindToolData(const FString& ToolId) const;
	const TArray<FCleaningToolData>& GetAllToolData() const { return Tools; }

	bool SelectTool(const FString& ToolId);
	FCleaningToolUseResult BuildUseResult(const FString& SurfaceType) const;

private:
	FCleaningToolSystem() = default;

	bool CanCleanSurface(const FCleaningToolData& ToolData, const FString& SurfaceType) const;

	TArray<FCleaningToolData> Tools;
};
