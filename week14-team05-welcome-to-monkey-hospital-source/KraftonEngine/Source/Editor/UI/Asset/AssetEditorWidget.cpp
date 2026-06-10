#include "AssetEditorWidget.h"

#include "Object/Object.h"

#include <cstdio>

void FAssetEditorWidget::Open(UObject* Object)
{
	if (!CanEdit(Object))
	{
		return;
	}

	EditedObject = Object;
	bOpen = true;
	RequestFocus();
	ClearDirty();
}

void FAssetEditorWidget::Close()
{
	EditedObject = nullptr;
	bOpen = false;
	bFocusRequested = false;
	ClearDirty();
}

void FAssetEditorWidget::RenderDocument(float DeltaTime)
{
	Render(DeltaTime);
}

FString FAssetEditorWidget::GetDocumentTitle() const
{
	return EditedObject ? EditedObject->GetName() : FString("Untitled");
}

FString FAssetEditorWidget::GetDocumentPayloadId() const
{
	char Buffer[32] = {};
	std::snprintf(Buffer, sizeof(Buffer), "%p", static_cast<const void*>(EditedObject));
	return FString(Buffer);
}

FEditorDocumentTabId FAssetEditorWidget::GetDocumentTabId() const
{
	FEditorDocumentTabId Id;
	Id.Kind = GetDocumentTabKind();
	Id.PayloadId = GetDocumentPayloadId();
	return Id;
}
