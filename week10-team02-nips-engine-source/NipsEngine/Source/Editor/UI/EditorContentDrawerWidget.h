#pragma once

#include "Editor/UI/EditorWidget.h"
#include "ImGui/imgui.h"

class FEditorConsoleWidget;

enum class EEditorAssetKind
{
	All = 0,
	StaticMesh,
	SkeletalMesh,
	Material,
	Texture,
	Particle,
	Font,
	Scene,
	Other
};

struct FEditorAssetItem
{
	FString RelativePath;
	FString DisplayName;
	FString Extension;
	EEditorAssetKind Kind = EEditorAssetKind::Other;
};

class FEditorContentDrawerWidget : public FEditorWidget
{
public:
	void Initialize(UEditorEngine* InEditorEngine) override;
	void Render(float DeltaTime) override;

	void SetOpen(bool bInOpen);
	bool IsOpen() const { return bOpen; }
	void ToggleOpen();
	bool ConsumeOpenRequest();
	void SetConsoleWidget(FEditorConsoleWidget* InConsoleWidget) { ConsoleWidget = InConsoleWidget; }
	void CloseImmediately();
	void StartConsoleTakeover();
	bool ConsumeConsoleTakeover(float& OutDrawerHeight);
	void RefreshAssetTree();
	float GetReservedBottomHeight() const;

private:
	void RenderResizeHandle(float WorkAreaHeight);
	void RenderBottomBar(const ImGuiViewport* Viewport, float BottomBarHeight);
	void RenderDrawerToolbar();
	void RenderFolderTree();
	void RenderFolderNode(const FString& FolderPath);
	void RenderAssetGrid();
	void RenderFolderTile(const FString& FolderPath, const ImVec2& TileSize);
	void RenderAssetTile(const FEditorAssetItem& Item, const ImVec2& TileSize);
	void RenderStatusBar() const;

	bool ShouldDisplayAsset(const FEditorAssetItem& Item) const;
	bool MatchesSelectedFolder(const FEditorAssetItem& Item) const;
	bool MatchesSearch(const FEditorAssetItem& Item) const;
	bool PlaceAssetInWorld(const FEditorAssetItem& Item);
	FVector GetAssetPlacementLocation() const;

private:
	bool bOpen = false;
	bool bOpenedThisFrame = false;
	bool bAssetTreeDirty = true;
	bool bPendingConsoleTakeover = false;
	float DrawerHeight = 0.0f;
	float DrawerAnimationAlpha = 0.0f;
	float PendingConsoleTakeoverHeight = 0.0f;
	int32 SelectedKindFilter = 0;
	int32 LastVisibleFolderCount = 0;
	int32 LastVisibleAssetCount = 0;
	char SearchBuffer[256] = "";

	FString SelectedFolder = "Asset";
	FString SelectedAssetPath;

	TArray<FString> FolderPaths;
	TArray<FEditorAssetItem> AssetItems;
	FEditorConsoleWidget* ConsoleWidget = nullptr;
};
