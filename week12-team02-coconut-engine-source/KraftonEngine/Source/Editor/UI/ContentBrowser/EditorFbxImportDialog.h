#pragma once

#include "Core/CoreTypes.h"
#include "Editor/Import/EditorFbxImportService.h"
#include "Mesh/MeshManager.h"

class UEditorEngine;

class FEditorFbxImportDialog
{
public:
	void Open(const FString& SourcePath);
	bool Render(UEditorEngine* EditorEngine);
	bool IsOpen() const { return bIsOpen; }

private:
	struct FImportRow
	{
		EFbxImportAssetType Type = EFbxImportAssetType::StaticMesh;
		bool bImport = true;
		bool bSkinned = false;
		int32 SourceIndex = -1;
		FString SourceName;
		FString DetailText;
		char FileStem[192] = {};
		FString PackagePath;
		FString ValidationMessage;
		bool bWillOverwrite = false;
	};

	void Reset();
	void BuildRows();
	void ValidateRows();
	void RenderSummary() const;
	void RenderStaticOptions();
	void RenderTargetSkeletonCombo();
	void RenderSection(const char* Label, TArray<FImportRow>& Rows);
	bool ExecuteImport(UEditorEngine* EditorEngine);

	bool IsRowVisible(const FImportRow& Row) const;
	bool HasVisibleRows(const TArray<FImportRow>& Rows) const;
	bool HasSkinnedStaticMeshRows() const;
	void AutoSelectTargetSkeleton();
	int32 FindTargetSkeletonOptionIndex(const FString& PackagePath) const;
	FString BuildPackagePath(const char* FileStem) const;
	TArray<FMeshAssetListItem> ScanSkeletonAssets() const;

private:
	static constexpr const char* PopupTitle = "FBX Import Options";

	bool bIsOpen = false;
	bool bOpenPopup = false;
	bool bInspectSucceeded = false;
	bool bImportedThisSession = false;
	bool bAnySelected = false;
	bool bHasValidationErrors = false;

	FString SourcePath;
	FFbxImportSourceInfo SourceInfo;
	FFbxImportResult LastResult;
	TArray<FString> StatusMessages;

	TArray<FImportRow> StaticMeshRows;
	TArray<FImportRow> SkeletalMeshRows;
	TArray<FImportRow> AnimSequenceRows;

	int32 PendingStaticFbxSkinnedMeshPolicy = 1;
	bool bCombineStaticMeshes = false;
	char CombinedStaticMeshStem[192] = {};
	FString CombinedStaticMeshPackagePath;
	FString CombinedStaticMeshValidationMessage;
	bool bCombinedStaticMeshWillOverwrite = false;
	int32 SelectedTargetSkeletonIndex = 0;
	TArray<FMeshAssetListItem> TargetSkeletonOptions;
};
