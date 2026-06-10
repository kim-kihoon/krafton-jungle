#pragma once

#include "Core/Types/CoreTypes.h"
#include "Editor/UI/Asset/AssetEditorWidget.h"
#include "Object/GarbageCollection.h"

#include <functional>
#include <memory>

class UObject;
class IEditorPreviewViewportClient;
class UEditorEngine;

struct FAssetEditorOpenResult
{
	bool bOpened = false;
	bool bDocumentTab = false;
	FEditorDocumentTabId TabId;
	FString Label;
};

class FAssetEditorManager : public FGCObject
{
public:
	~FAssetEditorManager();
	void Initialize(UEditorEngine* InEditorEngine) { EditorEngine = InEditorEngine; }

	template<typename TEditor, typename... TArgs>
	void RegisterEditor(TArgs&&... Args)
	{
		EditorFactories.push_back([Args...]()
		{
			return std::make_unique<TEditor>(Args...);
		});
	}

	void Tick(float DeltaTime);
	void Render(float DeltaTime);
	bool RenderActiveEditorDocument(const FEditorDocumentTabId& TabId, float DeltaTime);

	void CloseAll();
	FAssetEditorOpenResult OpenEditorForObject(UObject* Object);
	void CloseEditorForTab(const FEditorDocumentTabId& TabId);

	void CollectPreviewViewportClients(TArray<IEditorPreviewViewportClient*>& OutClients) const;
	void CollectPreviewViewportClientsForTab(const FEditorDocumentTabId& TabId, TArray<IEditorPreviewViewportClient*>& OutClients) const;

	bool IsMouseOverAnyEditorViewport() const;
	bool IsMouseOverEditorViewportForTab(const FEditorDocumentTabId& TabId) const;

	const char* GetReferencerName() const override { return "FAssetEditorManager"; }
	void AddReferencedObjects(FReferenceCollector& Collector) override;

	void RemoveClosedEditors();

private:
	FAssetEditorWidget* FindEditorForTab(const FEditorDocumentTabId& TabId) const;
	FAssetEditorOpenResult MakeOpenResult(FAssetEditorWidget& Editor) const;
	UEditorEngine* EditorEngine = nullptr;
	TArray<std::function<std::unique_ptr<FAssetEditorWidget>()>> EditorFactories;
	TArray<std::unique_ptr<FAssetEditorWidget>> OpenEditors;
};
