#pragma once

#include "Editor/UI/EditorWidget.h"
#include "Editor/UI/Util/BasicReflectionPropertyRenderer.h"

class FEditorReflectionPropertyWidget : public FEditorWidget
{
public:
	void Render(float DeltaTime) override;

private:
	FBasicReflectionPropertyRenderer Renderer;
};
