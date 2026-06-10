#include "EditorDocumentTabManager.h"

#include <algorithm>

namespace
{
	FEditorDocumentTabEntry MakeLevelEditorTab()
	{
		FEditorDocumentTabEntry Entry;
		Entry.Id.Kind = EEditorDocumentTabKind::LevelEditor;
		Entry.Id.PayloadId.clear();
		Entry.Label = "Level";
		Entry.bCanClose = false;
		return Entry;
	}
}

FEditorDocumentTabManager::FEditorDocumentTabManager()
{
	ResetToLevelEditor();
}

void FEditorDocumentTabManager::ResetToLevelEditor()
{
	Tabs.clear();
	Tabs.push_back(MakeLevelEditorTab());
	ActiveTab = Tabs.front().Id;
}

void FEditorDocumentTabManager::OpenOrFocusTab(const FEditorDocumentTabId& Id, const FString& Label, bool bCanClose)
{
	const int32 ExistingIndex = FindTabIndex(Id);
	if (ExistingIndex >= 0)
	{
		Tabs[ExistingIndex].Label = Label;
		Tabs[ExistingIndex].bCanClose = bCanClose;
		ActiveTab = Id;
		return;
	}

	FEditorDocumentTabEntry Entry;
	Entry.Id = Id;
	Entry.Label = Label;
	Entry.bCanClose = bCanClose;
	Tabs.push_back(Entry);
	ActiveTab = Id;
}

void FEditorDocumentTabManager::CloseTab(const FEditorDocumentTabId& Id)
{
	const int32 Index = FindTabIndex(Id);
	if (Index < 0 || !Tabs[Index].bCanClose)
	{
		return;
	}

	const bool bWasActive = Tabs[Index].Id == ActiveTab;
	Tabs.erase(Tabs.begin() + Index);

	if (Tabs.empty())
	{
		ResetToLevelEditor();
		return;
	}

	if (bWasActive)
	{
		const int32 NextIndex = (std::min)(Index, static_cast<int32>(Tabs.size()) - 1);
		ActiveTab = Tabs[NextIndex].Id;
	}
}

void FEditorDocumentTabManager::SetActiveTab(const FEditorDocumentTabId& Id)
{
	if (ContainsTab(Id))
	{
		ActiveTab = Id;
	}
}

bool FEditorDocumentTabManager::ContainsTab(const FEditorDocumentTabId& Id) const
{
	return FindTabIndex(Id) >= 0;
}

int32 FEditorDocumentTabManager::FindTabIndex(const FEditorDocumentTabId& Id) const
{
	for (int32 Index = 0; Index < static_cast<int32>(Tabs.size()); ++Index)
	{
		if (Tabs[Index].Id == Id)
		{
			return Index;
		}
	}

	return -1;
}
