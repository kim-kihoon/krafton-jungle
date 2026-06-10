#pragma once

#include "Editor/UI/Asset/AssetEditorWidget.h"
#include "Editor/Viewport/Asset/StaticMeshEditorViewportClient.h"
#include "Object/FName.h"

class UParticleSystem;
class UParticleEmitter;
class UParticleLODLevel;
class UParticleModule;
class UParticleSystemComponent;
class AActor;
class IEditorPreviewViewportClient;
struct FFloatCurve;
struct ImVec2;

// =============================================================================
// FParticleEditorWidget (Cascade)
//   UParticleSystem 에셋 편집기.
//   레이아웃 (Cascade 참고):
//     [Toolbar]
//     [Preview Viewport] [Emitter Strip]
//     [Details Panel]    [Curve Editor]
//
//   현재 엔진에 구현되어 있는 기능을 우선 연결한다.
//   - Preview: 별도 EditorPreview World + UParticleSystemComponent 재생
//   - Emitter Strip: Emitter/LOD0 Required/Spawn/TypeData/일반 Module 표시, 선택, 활성 토글
//   - Details: System / Emitter / LOD / Module 의 지원 필드 직접 편집
//   - Curve Editor: 아직 Distribution/Curve 백엔드가 없으므로 placeholder grid
// =============================================================================
class FParticleEditorWidget : public FAssetEditorWidget
{
public:
	FParticleEditorWidget();
	~FParticleEditorWidget() override;

	bool CanEdit(UObject* Object) const override;
	bool IsEditingObject(UObject* Object) const override;
	bool AllowsMultipleInstances() const override { return true; }

	void Open(UObject* Object) override;
	void Close() override;
	void Tick(float DeltaTime) override;
	void Render(float DeltaTime) override;
	void RenderDocument(float DeltaTime) override;
	FString GetDocumentTitle() const override;
	FString GetDocumentPayloadId() const override;
	EEditorDocumentTabKind GetDocumentTabKind() const override { return EEditorDocumentTabKind::ParticleEditor; }

	void CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const override;

private:
	// 패널 단위 렌더
	void RenderToolbar();
	void RenderEmitterStrip(ImVec2 Size);
	void RenderPropertyPanel(ImVec2 Size);
	void RenderPreviewViewport(ImVec2 Size);
	void RenderCurveEditor(ImVec2 Size);
	bool RenderSelectedCurveKeyDetails();

	// emitter strip 의 1 column 렌더
	void RenderEmitterColumn(UParticleEmitter* Emitter, int32 EmitterIndex);
	void RenderModuleCard(UParticleLODLevel* LOD, UParticleModule* Module, int32 ModuleIndex);

	// 토큰 작업 (메뉴/단축키)
	void AddEmitter();
	void RemoveSelectedEmitter();
	void AddModuleToSelectedEmitter();
	void RenderAddModulePopup();
	void DuplicateSelectedModule();
	void RemoveSelectedModule();
	void ClearSelectedCurveKey();
	FFloatCurve* GetSelectedCurve(FString* OutCurveName = nullptr, FString* OutChannelName = nullptr) const;

	void SelectSystem();
	void SelectEmitter(int32 EmitterIndex);
	void SelectModule(int32 EmitterIndex, int32 ModuleIndex);
	void SelectModuleCurve(int32 EmitterIndex, int32 ModuleIndex);
	bool SelectFirstCurveForModule(UParticleModule* Module);

	void RebuildPreview(bool bResetSimulation = true);
	void RestartPreview();
	void NotifyParticleAssetChanged(bool bResetSimulation = true);
	void ApplyCurrentLODToPreview();

	UParticleEmitter* GetSelectedEmitter() const;
	UParticleLODLevel* GetSelectedLOD() const;
	UParticleModule* GetSelectedModule() const;

	// 선택 상태
	UParticleSystem* GetEditedSystem() const;

private:
	FStaticMeshEditorViewportClient ViewportClient;
	FName   PreviewWorldHandle = FName::None;
	FString WindowIdSuffix;
	AActor* PreviewActor = nullptr;
	UParticleSystemComponent* PreviewParticleComponent = nullptr;

	int32 InstanceId = 0;
	int32 SelectedEmitterIndex = -1;
	int32 SelectedModuleIndex  = -1; // -1=Emitter, -100/-101/-102=Required/Spawn/TypeData, 0..=LOD.Modules
	int32 SelectedCurveSource  = -1;
	int32 SelectedCurveChannel = -1;
	int32 SelectedCurveKeyIndex = -1;
	int32 CurrentLODIndex      = 0;
	bool  bDraggingCurveKey    = false;

	float EmitterStripHeight = 320.0f;
	float DetailsWidth       = 420.0f;
	float CurvePanelHeight   = 280.0f;
	float SimulationSpeed    = 1.0f;
	bool  bSimPlaying        = true;
	bool  bPendingClose      = false;
	bool  bShowBounds        = false;
	bool  bShowOriginAxis    = true;
};
