#pragma once
#include "Editor/UI/EditorWidget.h"
#include "Asset/SkeletalMeshTypes.h"
#include <Viewer/FSkeletalMeshViewer.h>

class USkeletalMeshComponent;
class FEditorViewer;
class FAnimationViewer;
class FAnimStateMachineGraphViewer;
struct ID3D11ShaderResourceView;

enum class EEditorViewerIcon : uint8
{
    JumpToNextKey,
    JumpToPreviousKey,
    Looping,
    LoopingSelectionRange,
    NoLooping,
    Pause,
    PlayForward,
    PlayReverse,
    Record,
    SetPlaybackEnd,
    SetPlaybackStart,
    Stop,
    ToEnd,
    ToFront,
    ToNext,
    ToPrevious,
    Count
};

struct FEditorViewerIconResources
{
    ID3D11ShaderResourceView* Icons[static_cast<int32>(EEditorViewerIcon::Count)] = {};
};

struct FAnimTimelineUIState
{
    float TrackListWidth = 280.0f;
    float FrameWidth = 30.0f;
    int32 CurrentFrame = 16;
    int32 SelectedNotifyIndex = -1;
    char NotifyNameBuffer[64] = "Notify";
    char TrackFilter[128] = {};
    char AnimationSearchFilter[128] = {};
};

class FEditorViewerWindowWidget : public FEditorWidget
{
public:
    ~FEditorViewerWindowWidget() override;

    void Initialize(UEditorEngine* InEditorEngine) override;
    void Render(float DeltaTime) override;
    void Shutdown();
    void RenderEmbedded(float DeltaTime);
    void DrawBoneNode(
        int32 BoneIndex,
        const TArray<FBoneInfo>& Bones,
        const TArray<TArray<int32>>& Children);

    // Bone tree에 socket을 leaf 노드로 표시. SocketIdx는 CachedMesh->Sockets 배열 인덱스.
    void DrawSocketNode(int32 SocketIdx);

	void SetViewer(FEditorViewer* InViewer) { Viewer = InViewer; }
    FEditorViewer* GetViewer() const { return Viewer; }

	bool IsOpen() const { return bOpen; }
    void SetOpen(bool NewOpen) { bOpen = NewOpen; }

    FString GetWindowName() const;
	void RequestSaveMesh();
	bool CanSaveMesh() const;
	bool IsMeshDirty() const;
	void RequestSaveGraph();
	void RequestValidateGraph();
	bool CanSaveGraph() const;
	bool IsGraphDirty() const;

private:
    // bone tree 캐시들. CachedMesh가 바뀌면 둘 다 재빌드.
    // socket을 add/delete할 때도 BoneToSocketIndices만 다시 빌드해야 한다.
    void RebuildBoneTreeCaches(const FSkeletalMesh* MeshData);
    void RebuildBoneToSocketIndices(const FSkeletalMesh* MeshData);

    // Socket 편집 액션들. Render()안에서만 호출 — CachedMesh/CachedSkComp가 유효한 시점.
    void QueueBoneSubtreeOpenState(int32 BoneIdx, bool bOpen);
    void ApplyPendingBoneTreeOpenState(const FSkeletalMesh* MeshData);
    void SetBoneSubtreeOpenState(int32 BoneIdx, const TArray<TArray<int32>>& InChildren, bool bOpen);
    void    AddSocketOnBone(int32 BoneIdx);
    void DeleteSocket(int32 SocketIdx);
    bool HasPreview(const FName& SocketName) const;
    FString GenerateUniqueSocketName(const char* Base = "Socket") const;

    // 모달 picker — Render() 끝에서 호출하여 그림.
    void DrawPreviewPickerModal();

    // 좌측 패널 하단의 선택된 socket properties 편집기 + Save Mesh 버튼.
    void    DrawSocketInspector();
    void    TriggerSaveMesh();

    // Rename 다이얼로그
    void DrawRenameModal();
    bool    IsSocketNameUnique(const FString& Candidate, int32 IgnoreIdx) const;

	void RenderBoneDetails(USkeletalMeshComponent* SkelComp);
	void RenderSkeletalMeshContent(float DeltaTime);
    void RenderAnimationContent(float DeltaTime);
    void RenderAnimStateMachineGraphContent(float DeltaTime);
	void RenderDetachedDocumentChrome(bool& bDockRequested, bool& bCloseRequested);
	void RenderDetachedDocumentToolbar(bool& bDockRequested);
	FSkeletalMesh* ResolveCurrentMeshData() const;
	uint64 ComputeEditableMeshSignature(const FSkeletalMesh* MeshData) const;
	void ResetMeshDirtyBaseline();
	bool HasMeshAssetEdits() const;
    void LoadViewerIcons();
    void ReleaseViewerIcons();
    ID3D11ShaderResourceView* GetViewerIcon(EEditorViewerIcon Icon) const;

	void RenderAnimationSequencePanelContent(
		FAnimationViewer* AnimationViewer,
		FAnimTimelineUIState& State);

    void RenderAnimationPickerDetails(
        FAnimationViewer* AnimationViewer,
        FAnimTimelineUIState& State);

	void DrawSelectedNotifyDetails(FAnimationViewer* AnimationViewer);
    TArray<TArray<int32>> Children;             // bone idx → child bone indices
    TArray<TArray<int32>> BoneToSocketIndices;  // bone idx → socket array indices
    FSkeletalMesh* CachedMesh = nullptr;
    USkeletalMeshComponent* CachedSkComp = nullptr;   // Render() 내내만 유효한 transient cache

    int32 PendingPreviewPickerSocketIdx = -1;  // picker modal 트리거; -1이면 닫힌 상태
    int32 RenameSocketIdx = -1;                // rename modal 트리거; -1이면 닫힌 상태
    int32 PendingBoneTreeOpenStateRoot = -1;
    bool bPendingBoneTreeOpenStateValue = false;
    char  RenameBuffer[256] = {};
    bool  bMeshDirty = false;         // socket 등 mesh asset 데이터 변경 후 Save 트리거용
    uint64 CleanMeshEditSignature = 0;
	bool bHasCleanMeshEditSignature = false;

	FEditorViewer* Viewer = nullptr;
    FEditorViewerIconResources ViewerIconResources;
    FAnimTimelineUIState TimelineState;
    uint32 GraphInspectorStateBufferId = 0;
    uint32 GraphInspectorTransitionBufferId = 0;
    FName GraphSelectedParameterName;
    char GraphNewStateNameBuffer[128] = "New State";
    char GraphStateNameBuffer[128] = {};
    char GraphNewParameterNameBuffer[128] = "NewParameter";
    char GraphRenameParameterBuffer[128] = {};
    int32 GraphNewParameterTypeIndex = 0;
    uint32 GraphTransitionTargetStateId = 0;
    uint32 GraphTransitionFromStateId = 0;
    uint32 GraphTransitionToStateId = 0;
    uint32 GraphContextStateId = 0;
    uint32 GraphContextTransitionId = 0;
    int32 GraphTransitionParameterIndex = 0;
    int32 GraphTransitionCompareOpIndex = 0;
    int32 GraphTransitionEaseIndex = 0;
    int32 GraphTransitionPriority = 0;
    bool GraphTransitionBoolValue = true;
    int32 GraphTransitionIntValue = 0;
    float GraphTransitionFloatValue = 0.0f;
    float GraphTransitionVectorValue[3] = { 0.0f, 0.0f, 0.0f };
    float GraphTransitionBlendTime = 0.0f;

    bool bOpen = false;

    float LeftPanelWidth = 250.0f;
    float RightPanelWidth = 250.0f;
    float DownPanelHeight = 340.0f;
};
