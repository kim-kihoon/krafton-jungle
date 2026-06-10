#pragma once

#include "Core/Types/CoreTypes.h"
#include "Editor/UI/Asset/AssetEditorWidget.h"
#include "Object/GarbageCollection.h"

#include <functional>
#include <memory>
#include <type_traits>

class UObject;
class IEditorPreviewViewportClient;
class UEditorEngine;

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

	void CloseAll();
	bool OpenEditorForObject(UObject* Object);

	template<typename TEditor>
	bool OpenEditorForObjectAs(UObject* Object)
	{
		static_assert(std::is_base_of_v<FAssetEditorWidget, TEditor>, "TEditor must derive from FAssetEditorWidget");

		RemoveClosedEditors();

		for (const auto& Editor : OpenEditors)
		{
			TEditor* TypedEditor = dynamic_cast<TEditor*>(Editor.get());
			if (TypedEditor && TypedEditor->IsEditingObject(Object))
			{
				TypedEditor->RequestFocus();
				return true;
			}
		}

		auto Editor = std::make_unique<TEditor>();
		if (!Editor || !Editor->CanEdit(Object))
		{
			return false;
		}

		Editor->Initialize(EditorEngine);
		Editor->Open(Object);
		OpenEditors.push_back(std::move(Editor));
		return true;
	}

	void CollectPreviewViewportClients(TArray<IEditorPreviewViewportClient*>& OutClients) const;

	bool IsMouseOverAnyEditorViewport() const;

	const char* GetReferencerName() const override { return "FAssetEditorManager"; }
	void AddReferencedObjects(FReferenceCollector& Collector) override;

	void RemoveClosedEditors();

private:
	UEditorEngine* EditorEngine = nullptr;
	TArray<std::function<std::unique_ptr<FAssetEditorWidget>()>> EditorFactories;
	TArray<std::unique_ptr<FAssetEditorWidget>> OpenEditors;
};
