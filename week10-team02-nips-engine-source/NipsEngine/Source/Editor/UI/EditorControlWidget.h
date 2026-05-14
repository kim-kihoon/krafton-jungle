#pragma once

#include "Editor/UI/EditorWidget.h"

class FEditorControlWidget : public FEditorWidget
{
public:
	virtual void Initialize(UEditorEngine* InEditorEngine) override;
	virtual void Render(float DeltaTime) override;
};
