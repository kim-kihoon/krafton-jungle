#pragma once
#include "Component/SceneComponent.h"
#include "EditorLayout.h"

class Editor;
class USceneComponent;

struct EditorSceneManager
{
	EditorSceneManager(Editor* edtior);
	~EditorSceneManager();

	void Draw();

	Editor* editor;
	USceneComponent* AnchorComponent = nullptr;
};

