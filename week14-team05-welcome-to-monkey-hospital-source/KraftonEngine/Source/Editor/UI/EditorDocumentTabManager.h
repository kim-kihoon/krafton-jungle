#pragma once

#include "Core/Types/CoreTypes.h"

enum class EEditorDocumentTabKind : uint8
{
	LevelEditor,
	StaticMeshEditor,
	SkeletalMeshEditor,
	ParticleEditor,
	AnimGraphEditor,
	LuaBlueprintEditor,
	PhysicsAssetEditor,
    MaterialEditor,
	Unsupported,
};

struct FEditorDocumentTabId
{
	EEditorDocumentTabKind Kind = EEditorDocumentTabKind::LevelEditor;
	FString PayloadId;

	bool operator==(const FEditorDocumentTabId& Other) const
	{
		return Kind == Other.Kind && PayloadId == Other.PayloadId;
	}

	bool operator!=(const FEditorDocumentTabId& Other) const
	{
		return !(*this == Other);
	}
};

struct FEditorDocumentTabEntry
{
	FEditorDocumentTabId Id;
	FString Label;
	bool bCanClose = true;
	bool bDirty = false;
};

class FEditorDocumentTabManager
{
public:
	FEditorDocumentTabManager();

	void ResetToLevelEditor();
	void OpenOrFocusTab(const FEditorDocumentTabId& Id, const FString& Label, bool bCanClose);
	void CloseTab(const FEditorDocumentTabId& Id);
	void SetActiveTab(const FEditorDocumentTabId& Id);

	const FEditorDocumentTabId& GetActiveTab() const { return ActiveTab; }
	const TArray<FEditorDocumentTabEntry>& GetTabs() const { return Tabs; }

	bool IsLevelEditorActive() const { return ActiveTab.Kind == EEditorDocumentTabKind::LevelEditor; }
	bool ContainsTab(const FEditorDocumentTabId& Id) const;

private:
	int32 FindTabIndex(const FEditorDocumentTabId& Id) const;

private:
	TArray<FEditorDocumentTabEntry> Tabs;
	FEditorDocumentTabId ActiveTab;
};
