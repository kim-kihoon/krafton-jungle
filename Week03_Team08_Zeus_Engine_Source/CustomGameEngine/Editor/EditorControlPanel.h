#pragma once
#include "EditorLayout.h"

class UCameraComponent;
class Editor;

struct EditorControlPanel
{
	EditorControlPanel(Editor* parent);
	~EditorControlPanel();

	void Draw(UCameraComponent* camera);

	Editor* editor;
};
