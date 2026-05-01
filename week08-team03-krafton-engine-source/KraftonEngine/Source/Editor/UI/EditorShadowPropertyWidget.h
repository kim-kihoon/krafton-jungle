#pragma once

#include "Engine/Component/Light/LightComponent.h"
class FEditorShadowPropertyWidget
{
#pragma region DefineSingleton
public:
	static FEditorShadowPropertyWidget& Get()
	{
		static FEditorShadowPropertyWidget Instance;
		return Instance;
	}

	FEditorShadowPropertyWidget(const FEditorShadowPropertyWidget&) = delete;
	FEditorShadowPropertyWidget& operator=(const FEditorShadowPropertyWidget&) = delete;
	FEditorShadowPropertyWidget(FEditorShadowPropertyWidget&&) = delete;
	FEditorShadowPropertyWidget& operator=(FEditorShadowPropertyWidget&&) = delete;

protected:
	FEditorShadowPropertyWidget() = default;
	~FEditorShadowPropertyWidget() = default;
#pragma endregion

public:
	void ShowShadowProperty(ULightComponent* LightComponent);
	bool GetEnablePointer() { return bIsEnabled; }
	void SetEnabled(bool InEnable) { bIsEnabled = InEnable; }

private:
	enum class EShadowPreviewMode : uint8
	{
		SelectedLight = 0,
		AtlasLayer = 1
	};

	void ShowShadowMapPropertWindow();

private:
	ULightComponent* CurrentShowLightComponent = nullptr;
	EShadowPreviewMode PreviewMode = EShadowPreviewMode::SelectedLight;
	int32 PreviewAtlasLayerIndex = 0;
	bool bShowAtlasRectOverlay = true;
	bool bShowAtlasAllocatedIndices = true;
	bool bIsEnabled = false;
};

