#pragma once

#include "EngineTypes.h"
#include "EditorLayout.h"

class Editor;
class USceneComponent;

struct EditorPropertyPanel
{
	EditorPropertyPanel(Editor* parent);
	~EditorPropertyPanel();

	void Draw(USceneComponent* selectedComponent, const TArray<USceneComponent*>& selectedComponents);
	void RemoveSelected(const TArray<USceneComponent*>& selectedComponents);

private:
	void DrawMultiTransformProperties(USceneComponent* selectedComponent, const TArray<USceneComponent*>& selectedComponents);

private:
	Editor* editor = nullptr;

	USceneComponent* CachedComponent = nullptr;
	char TextBuffer[1000] = {};
	char ImagePathBuffer[1000] = {};
};
