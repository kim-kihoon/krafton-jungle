#include "AssetEditorManager.h"

#include "Editor/UI/Asset/AssetEditorWidget.h"
#include "Viewport/EditorPreviewViewportClient.h"

#include <algorithm>

FAssetEditorManager::~FAssetEditorManager() = default;

void FAssetEditorManager::Tick(float DeltaTime)
{
	for (const auto& Editor : OpenEditors)
	{
		if (Editor->IsOpen())
		{
			Editor->Tick(DeltaTime);
		}
	}

	RemoveClosedEditors();
}

void FAssetEditorManager::Render(float DeltaTime)
{
	for (const auto& Editor : OpenEditors)
	{
		if (Editor && !Editor->SupportsDocumentTabs())
		{
			Editor->Render(DeltaTime);
		}
	}
}

bool FAssetEditorManager::RenderActiveEditorDocument(const FEditorDocumentTabId& TabId, float DeltaTime)
{
	FAssetEditorWidget* Editor = FindEditorForTab(TabId);
	if (!Editor || !Editor->IsOpen())
	{
		return false;
	}

	Editor->RenderDocument(DeltaTime);
	return Editor->IsOpen();
}

void FAssetEditorManager::CloseAll()
{
	for (const auto& Editor : OpenEditors)
	{
		if (Editor && Editor->IsOpen())
		{
			Editor->Close();
		}
	}
	OpenEditors.clear();
}

FAssetEditorOpenResult FAssetEditorManager::OpenEditorForObject(UObject* Object)
{
	RemoveClosedEditors();

	for (const auto& Editor : OpenEditors)
	{
		if (Editor && Editor->IsEditingObject(Object))
		{
			Editor->RequestFocus();
			return MakeOpenResult(*Editor);
		}
	}

	for (const auto& Editor : OpenEditors)
	{
		if (Editor->CanEdit(Object) && !Editor->AllowsMultipleInstances())
		{
			Editor->Initialize(EditorEngine);
			Editor->Open(Object);
			return MakeOpenResult(*Editor);
		}
	}

	for (const auto& Factory : EditorFactories)
	{
		auto Editor = Factory();
		if (!Editor || !Editor->CanEdit(Object)) continue;

		Editor->Initialize(EditorEngine);
		Editor->Open(Object);
		FAssetEditorOpenResult Result = MakeOpenResult(*Editor);
		OpenEditors.push_back(std::move(Editor));
		return Result;
	}

	return FAssetEditorOpenResult {};
}

void FAssetEditorManager::CloseEditorForTab(const FEditorDocumentTabId& TabId)
{
    FAssetEditorWidget* Editor = FindEditorForTab(TabId);
    if (Editor && Editor->IsOpen())
    {
        Editor->Close();
    }
    RemoveClosedEditors();
}

void FAssetEditorManager::CollectPreviewViewportClients(TArray<IEditorPreviewViewportClient*>& OutClients) const
{
	for (const auto& Editor : OpenEditors)
	{
		Editor->CollectPreviewViewports(OutClients);
	}
}

void FAssetEditorManager::CollectPreviewViewportClientsForTab(const FEditorDocumentTabId& TabId, TArray<IEditorPreviewViewportClient*>& OutClients) const
{
	const FAssetEditorWidget* Editor = FindEditorForTab(TabId);
	if (Editor && Editor->IsOpen())
	{
		Editor->CollectPreviewViewports(OutClients);
	}
}

bool FAssetEditorManager::IsMouseOverAnyEditorViewport() const
{
	TArray<IEditorPreviewViewportClient*> PreviewViewportClients;
	CollectPreviewViewportClients(PreviewViewportClients);

	for (IEditorPreviewViewportClient* Client : PreviewViewportClients)
	{
		if (Client && Client->IsMouseOverViewport())
		{
			return true;
		}
	}

	return false;
}

bool FAssetEditorManager::IsMouseOverEditorViewportForTab(const FEditorDocumentTabId& TabId) const
{
	TArray<IEditorPreviewViewportClient*> PreviewViewportClients;
	CollectPreviewViewportClientsForTab(TabId, PreviewViewportClients);

	for (IEditorPreviewViewportClient* Client : PreviewViewportClients)
	{
		if (Client && Client->IsMouseOverViewport())
		{
			return true;
		}
	}

	return false;
}


void FAssetEditorManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (const auto& Editor : OpenEditors)
	{
		if (Editor && Editor->IsOpen())
		{
			Editor->AddReferencedObjects(Collector);
		}
	}
}

void FAssetEditorManager::RemoveClosedEditors()
{
	OpenEditors.erase(std::remove_if(OpenEditors.begin(), OpenEditors.end(),
		[](const std::unique_ptr<FAssetEditorWidget>& Editor)
		{
			return !Editor || !Editor->IsOpen();
		}),
	OpenEditors.end());
}

FAssetEditorWidget* FAssetEditorManager::FindEditorForTab(const FEditorDocumentTabId& TabId) const
{
	for (const auto& Editor : OpenEditors)
	{
		if (Editor && Editor->SupportsDocumentTabs() && Editor->GetDocumentTabId() == TabId)
		{
			return Editor.get();
		}
	}

	return nullptr;
}

FAssetEditorOpenResult FAssetEditorManager::MakeOpenResult(FAssetEditorWidget& Editor) const
{
	FAssetEditorOpenResult Result;
	Result.bOpened = Editor.IsOpen();
	Result.bDocumentTab = Editor.SupportsDocumentTabs();
	if (Result.bDocumentTab)
	{
		Result.TabId = Editor.GetDocumentTabId();
		Result.Label = Editor.GetDocumentTitle();
	}
	return Result;
}
