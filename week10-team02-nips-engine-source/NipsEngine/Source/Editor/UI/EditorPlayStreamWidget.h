#pragma once
#include "Editor/UI/EditorWidget.h"
#include "ImGui/imgui.h"

class UTexture;

class FEditorPlayStreamWidget : public FEditorWidget
{
public:
	virtual void Initialize(UEditorEngine* InEditorEngine) override;
	virtual void Render(float DeltaTime) override;

private:
	const char* PlayLabel   = "▶ Play";
	const char* ResumeLabel = "▶ Resume";
	const char* PauseLabel  = "❚❚ Pause"; 
	const char* StopLabel   = "■ Stop";

	UTexture* PlayIconTexture = nullptr;
	UTexture* PauseIconTexture = nullptr;
	UTexture* StopIconTexture = nullptr;
};

