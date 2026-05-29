#pragma once

#include "AssetEditorWidget.h"
#include "Core/EngineTypes.h"
#include "Editor/Viewport/StaticMeshEditorViewportClient.h"
#include "Object/FName.h"
#include "Slate/SWindow.h"

#include <imgui.h>

class AActor;
class UParticleEmitter;
class UParticleLODLevel;
class UParticleModule;
class UParticleModuleRequired;
class UParticleSystem;
class UParticleSystemComponent;

class FParticleEditorWidget : public FAssetEditorWidget
{
public:
	FParticleEditorWidget();

	bool CanEdit(UObject* Object) const override;
	bool IsEditingObject(UObject* Object) const override;

	void Open(UObject* Object) override;
	void Close() override;
	void Tick(float DeltaTime) override;
	void Render(float DeltaTime) override;

	void CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const override;
	bool AllowsMultipleInstances() const override { return true; }

private:
	enum class EAddableModuleType
	{
		Lifetime,
		Size,
		Velocity,
		InitialRotation,
		InitialRotationRate,
		Acceleration,
		Orbit,
		Location,
		Color,
		ColorOverLife,
		ColorScaleOverLife,
		BeamSource,
		BeamTarget,
		BeamNoise,
		EventGenerator,
		EventReceiverSpawn,
		Collision
	};

	enum class EEmitterTypeData
	{
		Sprite,
		Mesh,
		Beam,
		Ribbon
	};

	void EnsureDefaultSystem();
	UParticleEmitter* CreateDefaultEmitter(const FString& EmitterName);
	UParticleModule* CreateModule(EAddableModuleType ModuleType, UObject* Outer);
	UParticleModule* CreateTypeDataModule(EEmitterTypeData TypeData, UObject* Outer);
	void AddModuleToEmitter(int32 EmitterIndex, EAddableModuleType ModuleType);
	void SetEmitterTypeData(int32 EmitterIndex, EEmitterTypeData TypeData);
	void MoveEmitterToIndex(int32 SourceEmitterIndex, int32 TargetInsertIndex);
	void MoveModuleToEmitterAtIndex(int32 SourceEmitterIndex, UParticleModule* Module, int32 TargetEmitterIndex, int32 TargetInsertIndex);
	void DeleteModuleFromEmitter(int32 EmitterIndex, UParticleModule* Module);
	void DeleteEmitter(int32 EmitterIndex);
	int32 GetLODCount() const;
	int32 ClampLODIndex(int32 LODIndex) const;
	void SetSelectedLODIndex(int32 LODIndex);
	UParticleLODLevel* GetSelectedLODLevel(UParticleEmitter* Emitter) const;
	void AddLOD();
	void DeleteSelectedLOD();
	void ApplySelectedLODToPreview(bool bRestart);
	void InitializePreviewWorld();
	void ReleasePreviewWorld();
	void RestartPreviewSystem();
	FBoundingBox CalculatePreviewBounds() const;
	void ResetPreviewCameraToParticleBounds();

	void RenderToolbar();
	void RenderEditorLayout();
	void RenderPreviewViewport(const ImVec2& Size);
	void RenderEmitterList();
	bool RenderDetailsPanel();
	bool RenderParticleSystemDetails();
	bool RenderRequiredDetails(UParticleModuleRequired* Required);
	bool RenderModuleDetails(UParticleModule* Module);
	bool RenderCurvePanel();

	UParticleEmitter* GetSelectedEmitter() const;
	UParticleModuleRequired* GetSelectedRequiredModule() const;
	UParticleModule* GetSelectedModule() const;
	FString GetEmitterDisplayName(UParticleEmitter* Emitter, int32 Index) const;
	FString GetModuleDisplayName(UParticleModule* Module) const;
	FString GetTypeDataDisplayName(UParticleLODLevel* LOD) const;
	void ApplyEmitterEdit();
	void SyncAssetNameBuffer();
	void CommitAssetNameEdit();
	void SyncEmitterNameBuffer();
	bool CommitEmitterNameEdit();
	bool IsEmitterNameAvailable(const FString& Name, int32 IgnoreEmitterIndex) const;
	void UpdateEmitterNameReferences(const FName& OldName, const FName& NewName);

private:
	SWindow ParticleViewportWindow;
	FStaticMeshEditorViewportClient ViewportClient;
	UParticleSystem* EditingParticleSystem = nullptr;
	UParticleSystemComponent* PreviewParticleComponent = nullptr;
	AActor* PreviewActor = nullptr;

	int32 SelectedEmitterIndex = 0;
	int32 SelectedLODIndex = 0;
	UParticleModule* SelectedModule = nullptr;
	bool bParticleSystemSelected = false;
	bool bSimulating = true;

	uint32 InstanceId = 0;
	FName PreviewWorldHandle = FName::None;
	FString WindowIdSuffix;
	char AssetNameBuffer[128] = {};
	char EmitterNameBuffer[128] = {};
	int32 EmitterNameBufferIndex = -1;
	UParticleEmitter* EmitterNameBufferEmitter = nullptr;
};
