#pragma once

#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimTypes.h"
#include "AssetEditorWidget.h"
#include "Editor/Viewport/MeshEditorViewportClient.h"
#include "Math/Transform.h"
#include "Object/FName.h"
#include "Slate/SWindow.h"

class USkeletalMesh;
class USkeletalMeshComponent;
class UAnimSequence;
struct FSkeletonAsset;
struct FSkeletalMesh;
struct ImDrawList;
struct ImVec2;

// =====================================================================
// Animation Sequence Viewer
// Skeleton Preview와 Notify Marker UI만 편집한다
// Edited Object는 UAnimSequence이다
// =====================================================================
class FAnimSequenceEditorWidget : public FAssetEditorWidget
{
public:
	FAnimSequenceEditorWidget();
	~FAnimSequenceEditorWidget();

	bool CanEdit(UObject* Object) const override;
	bool IsEditingObject(UObject* Object) const override;

	void Open(UObject* Object) override;
	void Close() override;

	void Tick(float DeltaTime) override;
	void Render(float DeltaTime) override;

	void CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const override;
	bool AllowsMultipleInstances() const override { return true; }

private:
	void RenderSkeletonTree(const FSkeletonAsset* SkeletonAsset, int32 BoneIndex);
	void RenderRelatedAnimSequenceList();
	void RenderViewportPanel(float Deltatime);
	void RenderBoneDetailsPanel();
	void RenderStatsOverlay(ImDrawList* DrawList, const ImVec2& ViewportPos) const;
	void RenderTimelinePanel();

	void SetSelectedBones(int32 BoneIndex);
	void InitializeFromAnimSequence();
	void RefreshRelatedAnimSequences();
	void OpenRelatedAnimSequence(const FString& AnimSequencePath);
	USkeletalMesh* FindPreviewSkeletalMesh();
	void InitializePreviewWorld();
	void ReleasePreviewWorld();
	const FSkeletonAsset* GetEditableSkeletonAsset() const;
	void ApplyAnimationPoseToPreview(float Time);
	void CaptureSelectedBoneOverrideFromPreview();
	FTransform GetCurrentBoneLocalTransformForDetails(int32 BoneIndex) const;

	// --------------- Time line Section -----------------
	void TickTimeline(float DeltaTime);
	float GetTimelineFrameStep() const;
	void SetTimelineTime(float NewTime);
	void StopTimelinePlayback();
	void PlayTimeline(float PlayRate);
	void StepTimelineFrame(int32 FrameOffset);

	void DrawNotifyTrack(
		ImDrawList* DrawList,
		const ImVec2& PanelPos,
		const ImVec2& PanelEnd,
		float TimelineX,
		float TrackTop,
		float TimelineWidth,
		float VisibleRange,
		bool bCanvasHovered,
		const ImVec2& Mouse);

	float TimeToX(float Time, float TimelineX, float TimelineWidth, float VisibleRange) const;
	float XToTime(float X, float TimelineX, float TimelineWidth, float VisibleRange) const;
	float SnapTime(float Time) const;

	TArray<FAnimNotifyEvent>& GetNotifyMarkers();

	void AddVisualNotifyAtTime(float Time);
	void MoveVisualNotify(int32 NotifyIndex, float NewTime);
	void DeleteSelectedVisualNotify();
	void SortVisualNotifyMarkers();

	void SetEditedBoneTransform(int32 BoneIndex, const FTransform& LocalTransform);
	void ApplyEditorBoneOverrides();

private:
	UAnimSequence* AnimSequence = nullptr;
	USkeletalMesh* PreviewSkeletalMesh = nullptr;
	USkeletalMeshComponent* PreviewMeshComponent = nullptr;
	UAnimSingleNodeInstance* SingleNodeInstance = nullptr;
	FString PreviewStatusMessage;
	FPoseContext EvaluatedLocalPose;
	bool bLastPoseEvaluationSucceeded = false;

	SWindow MeshViewportWindow;
	FMeshEditorViewportClient ViewportClient;

	FName PreviewWorldHandle = FName::None;
	FString WindowIdSuffix;

	int32 SelectedBoneIndex = -1;

	// ============================================================
	// Animation Timeline State
	// ============================================================
	float CurrentTime = 0.0f;
	float PreviousTime = 0.0f;
	float PlayLength = 0.0f;
	bool bPlaying = false;
	bool bLooping = true;
	float TimelinePlayRate = 1.0f;  // 역재생용

	// ============================================================
	// Timeline View State
	// ============================================================
	float ViewStartTime = -0.25f;
	float ViewEndTime = 2.5f;
	float TimelinePanelHeight = 220.0f;

	// ============================================================
	// Notify Marker Edit State
	// ============================================================
	TArray<FAnimNotifyEvent> PreviewNotifyMarkers;

	int32 SelectedNotifyIndex = -1;
	int32 DraggingNotifyIndex = -1;
	bool bDraggingNotify = false;
	float ContextTimelineTime = 0.0f;

	// ============================================================
	// Bone Override State
	// 원본 AnimSequence / Skeleton을 건드리지 않고 Preview Pose 위에 덮어쓰기
	// ============================================================
	struct FEditorBoneOverride
	{
		int32 BoneIndex = -1;
		FTransform LocalTransform;
	};

	TArray<FEditorBoneOverride> EditorBoneOverrides;

	// --- Bone을 공유하는 Sequence 목록을 저장해둔다 ---
	struct FRelatedAnimSequenceItem
	{
		FString Name;
		FString Path;
		UAnimSequence* Sequence = nullptr;
	};

	TArray<FRelatedAnimSequenceItem> RelatedAnimSequences;

	bool bPendingClose = false;
};
