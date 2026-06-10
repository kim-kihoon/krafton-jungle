#pragma once
#include "Editor/UI/Asset/AssetEditorWidget.h"
#include "Editor/Viewport/Asset/MeshEditorViewportClient.h"
#include "Editor/UI/Dialog/FbxImportOptionsDialog.h"
#include "Asset/AssetRegistry.h"
#include "Editor/UI/Asset/Physics/PhysicsAssetEditorWidget.h"

struct FSkeletalMesh;
struct FReferenceSkeleton;
struct ImDrawList;
struct ImVec2;
class USkeleton;
class UAnimSequence;
class UAnimMontage;
class UAnimSingleNodeInstance;
class UPhysicsAsset;

enum class EMeshEditorTab : uint8 { Skeleton, Mesh, Animation, Physics };

struct FAnimationTabState
{
	UAnimSequence* CurrentSequence    = nullptr;
	UAnimMontage*  CurrentMontage     = nullptr;
	int32          SelectedAnimIndex     = -1;
	int32          SelectedMontageIndex  = -1;
	bool           bMontageSelected      = false;     // true 면 좌측 패널이 montage 표시
	// 타임라인에서 선택된 Notify entry 인덱스 (현재 시퀀스의 DataModel->Notifies 기준).
	// -1 = 미선택. 시퀀스/몽타주 전환 시 -1 reset 필요.
	// 유효 시 좌상단 AssetDetails 패널이 시퀀스 정보 대신 Notify 의 UPROPERTY 편집 UI 를 그림.
	int32         SelectedNotifyIndex     = -1;
	int32         SelectedMorphCurveIndex = -1;
	int32         SelectedMorphKeyIndex   = -1;
	TArray<float> MorphPreviewWeights;
	TArray<uint8> MorphPreviewOverrideMask;
	bool          bMorphPreviewOverrideEnabled = false;

	// Animation tab asset browser cache.
	// Render 중 매 프레임 ListAnimationsForSkeleton() -> LoadAnimation() -> Serialize() 되는 것을 막는다.
	TArray<FAssetListItem> CachedAnimationFiles;
	TArray<FAssetListItem> CachedMontageFiles;
	FSkeletonBinding       CachedAnimationListBinding;
	bool                   bAnimationListDirty = true;
	TSet<UAnimSequence*>   DirtySequences;
	TSet<UAnimMontage*>    DirtyMontages;

	float         AnimListWidth                = 200.0f;
	float         AnimDetailsWidth             = 380.0f;

	FFbxAnimationImportDialogState AnimationImportDialog;
};

class FMeshEditorWidget : public FAssetEditorWidget
{
public:
	FMeshEditorWidget();

	bool CanEdit(UObject* Object) const override;
	bool IsEditingObject(UObject* Object) const override;

	void Open(UObject* Object) override;
	void Close() override;
	void Tick(float DeltaTime) override;

	void CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const override;
	void AddReferencedObjects(FReferenceCollector& Collector) override;

	bool AllowsMultipleInstances() const override { return true; }

	void Render(float DeltaTime) override;
	void RenderDocument(float DeltaTime) override;
	FString GetDocumentTitle() const override;
	FString GetDocumentPayloadId() const override;
	EEditorDocumentTabKind GetDocumentTabKind() const override { return EEditorDocumentTabKind::SkeletalMeshEditor; }

	bool IsMouseOverViewport() const { return IsOpen() && ViewportClient.IsMouseOverViewport(); }

	FMeshEditorViewportClient* GetViewportClient() { return &ViewportClient; }

	static void RecordImportDurationForAsset(const FString& AssetPath, double Seconds);
	static void ClearImportDurationForAsset(const FString& AssetPath);

private:
	// Tab bar
	void RenderTabBar();

	// Per-tab layouts
	void RenderSkeletonLayout();
	void RenderMeshLayout();
	void RenderClothAuthoringPanel(USkeletalMesh* SkeletalMesh, FSkeletalMesh* Asset);
	void RenderClothBrushRadiusOverlay(ImDrawList* DrawList, const ImVec2& ViewportPos, const ImVec2& ViewportSize) const;
	void RenderAnimationLayout(float TotalHeight);
	void RenderPhysicsLayout(float TotalHeight);
	void TickClothPaintBrush();
	void UpdateClothMaxDistanceOverlayOptions();
	void ApplyClothPreviewForcesToComponent();
	FVector GetClothPreviewWindVelocity() const;

	// Shared helpers
	void RenderViewportPanel(ImVec2 Size);
	void RenderBoneTree(USkeleton* Skeleton, const FReferenceSkeleton& RefSkeleton, int32 Index);
	void RenderSocketTreeNode(USkeleton* Skeleton, int32 SocketIndex);
	void RenderMeshStatsOverlay(ImDrawList* DrawList, const ImVec2& ViewportPos) const;
	void SaveCurrentSkeleton();
	void SaveCurrentAnimationAsset();
	bool SaveCurrentMeshAsset();
	bool SaveCurrentPhysicsAsset();
	void SaveAllDirtyAssets();
	void MarkCurrentMeshDirty();
	void RefreshSelectedSocketEditBuffers(USkeleton* Skeleton);

	// Animation tab helpers
	void ApplyAnimationToComponent();
	void ResetMorphPreviewOverrides();
	void EnsureMorphPreviewOverrideSize();
	void ApplyMorphPreviewOverrides(TArray<float>& InOutMorphWeights) const;
	void RefreshAnimationPreviewPose();
	void MarkAnimationListDirty();
	bool IsCurrentAnimationDirty() const;
	void MarkCurrentAnimationDirty();
	const TArray<FAssetListItem>& GetCachedAnimationFilesForCurrentSkeleton();
	const TArray<FAssetListItem>& GetCachedMontageFilesForCurrentSkeleton();

	// Physics tab helpers
	UPhysicsAsset* GetCurrentPhysicsAsset();
	UPhysicsAsset* CreateAndAssignPhysicsAssetForCurrentMesh();
	bool AssignPhysicsAssetToCurrentMesh(UPhysicsAsset* PhysicsAsset);
	void RefreshPhysicsAssetList();
	void RenderMissingPhysicsAssetPanel(USkeletalMesh* SkeletalMesh);
	bool IsCurrentPhysicsAssetDirty() const;
	void UpdatePhysicsSimulationViewportState();
	void RestorePhysicsSimulationShowFlags();

private:
	FMeshEditorViewportClient ViewportClient;

	// Tab state
	EMeshEditorTab     ActiveTab = EMeshEditorTab::Skeleton;
	FAnimationTabState AnimTabState;
	FPhysicsAssetEditorWidget PhysicsAssetEditor;
	TArray<FAssetListItem> CachedPhysicsAssetFiles;
	bool bPhysicsAssetListDirty = true;
	int32 SelectedPhysicsAssetIndex = -1;
	float PhysicsPanelWidth = 420.0f;
	float PhysicsDetailsWidth = 360.0f;
	bool bPhysicsSimulationShowFlagsSaved = false;
	bool bLastPhysicsSimulationActive = false;
	FShowFlags SavedPhysicsSimulationShowFlags;

	// Skeleton tab state
	int32 SelectedBoneIndex = -1;
	int32 SelectedSocketIndex = -1;
	int32 SelectedClothLODIndex = 0;
	int32 SelectedClothIndex = -1;
	int32 SelectedClothSectionIndex = 0;
	float ClothBrushValue = 50.0f;
	float ClothBrushRadius = 25.0f;
	float ClothBrushSmoothStrength = 0.5f;
	FVector ClothPreviewWindDirection = FVector::XAxisVector;
	float ClothPreviewWindSpeed = 0.0f;
	bool bClothPreviewWindEnabled = false;
	bool bClothPaintBrushEnabled = false;
	bool bShowClothPaintValues = true;
	bool bMeshDirty = false;
	bool bSkeletonDirty = false;
	int32 BufferedSocketIndex = -2;
	char SocketNameBuffer[128] = {};
	char SocketBoneNameBuffer[128] = {};
	float MeshInfoWidth    = 340.0f;
	float HierarchyWidth    = 250.0f;
	float DetailsWidth      = 300.0f;

	uint32  InstanceId;
	FName   PreviewWorldHandle = FName::None;
	FString WindowIdSuffix;

	bool bPendingClose = false;
};
