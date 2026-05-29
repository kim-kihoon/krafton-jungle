#pragma once

#include "Editor/UI/EditorWidget.h"

class FEditorControlWidget : public FEditorWidget
{
public:
	virtual void Render(float DeltaTime) override;
	void Render(float DeltaTime, bool* bOpen);
};
