#pragma once

#include "Editor/UI/EditorWidget.h"
#include "Core/CoreTypes.h"

class FEditorSceneWidget : public FEditorWidget
{
public:
	virtual void Initialize(UEditorEngine* InEditorEngine) override;
	virtual void Render(float DeltaTime) override;
	void Render(float DeltaTime, bool* bOpen);

private:
	void RenderActorOutliner();

	TArray<int32> ValidActorIndices;
};
