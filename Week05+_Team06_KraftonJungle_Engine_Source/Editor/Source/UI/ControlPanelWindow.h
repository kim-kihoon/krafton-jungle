#pragma once
#include "CoreMinimal.h"

class FEditorEngine;

class FControlPanelWindow
{
public:
	void Render(FEditorEngine* Engine);

private:
	TArray<FString> LevelFiles;
	int32 SelectedLevelIndex = -1;
};
