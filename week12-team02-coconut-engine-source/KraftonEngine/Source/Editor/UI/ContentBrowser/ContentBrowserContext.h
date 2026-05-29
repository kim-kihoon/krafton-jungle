#pragma once
#include "Core/CoreTypes.h"
#include "imgui.h" 
#include "Platform/Paths.h"
#include <functional>
#include <memory>

class ContentBrowserElement;
class UEditorEngine;

struct ContentBrowserContext final
{
	std::wstring CurrentPath = FPaths::RootDir();
	std::wstring PendingRevealPath;
	ImVec2 ContentSize = ImVec2(50.0f, 50.0f);
	std::shared_ptr<ContentBrowserElement> SelectedElement;

	UEditorEngine* EditorEngine;
	std::function<void(const FString&)> OnImportFbxSource;

	bool bPendingContentRefresh = false;
	bool bShowSourceFiles = false;
};
