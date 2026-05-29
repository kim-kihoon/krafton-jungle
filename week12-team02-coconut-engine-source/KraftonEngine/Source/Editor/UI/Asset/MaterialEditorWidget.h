#pragma once

#include "AssetEditorWidget.h"
#include "Editor/Viewport/StaticMeshEditorViewportClient.h"
#include "Object/FName.h"
#include "Slate/SWindow.h"

class UMaterial;
class UStaticMeshComponent;

class FMaterialEditorWidget : public FAssetEditorWidget
{
public:
	FMaterialEditorWidget();

	bool CanEdit(UObject* Object) const override;
	bool IsEditingObject(UObject* Object) const override;

	void Open(UObject* Object) override;
	void Close() override;
	void Tick(float DeltaTime) override;
	void Render(float DeltaTime) override;

	void CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const override;
	bool AllowsMultipleInstances() const override { return true; }

private:
	bool RenderDetailsPanel(UMaterial* Material);
	bool RenderRenderStateControls(UMaterial* Material);
	bool RenderParticleSettings(UMaterial* Material);
	bool RenderShaderParameters(UMaterial* Material);
	bool RenderTextureSlots(UMaterial* Material);
	void RenderPreviewViewport(float DetailsWidth);

private:
	SWindow MaterialViewportWindow;
	FStaticMeshEditorViewportClient ViewportClient;
	UStaticMeshComponent* PreviewMeshComponent = nullptr;
	UMaterial* EditingMaterial = nullptr;

	uint32 InstanceId = 0;
	FName PreviewWorldHandle = FName::None;
	FString WindowIdSuffix;
};
