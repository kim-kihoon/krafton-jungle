#pragma once

#include "Editor/UI/EditorWidget.h"
#include "Core/Types/CoreTypes.h"

class AActor;

class FEditorSceneWidget : public FEditorWidget
{
public:
	virtual void Initialize(UEditorEngine* InEditorEngine) override;
	virtual void Render(float DeltaTime) override;

private:
	void RenderActorOutliner();
	bool RenameActor(AActor* Actor);

	TArray<int32> ValidActorIndices;
	char RenameBuffer[256] = {};
	bool bShowDuplicateWarning = false;
};
