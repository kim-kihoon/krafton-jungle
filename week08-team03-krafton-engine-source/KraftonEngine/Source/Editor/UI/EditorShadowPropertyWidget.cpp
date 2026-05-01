#include "EditorShadowPropertyWidget.h"
#include "Editor/EditorEngine.h"
#include "Editor/Viewport/LevelEditorViewportClient.h"
#include "Engine/Runtime/Engine.h"
#include "imgui.h"
#include "Engine/Component/Light/PointLightComponent.h"
#include "Engine/Render/Resource/TexturePool/TextureCubeShadowPool.h"
#include "GameFramework/World.h"
#include <algorithm>
#include <cstdio>

namespace
{
	ImU32 MakeAllocatedRectColor(uint32 HandleIndex, float Alpha)
	{
		const uint32 Hash = HandleIndex * 2654435761u;
		const int32 R = 80 + static_cast<int32>((Hash >> 16) & 0x7f);
		const int32 G = 110 + static_cast<int32>((Hash >> 8) & 0x5f);
		const int32 B = 170 + static_cast<int32>(Hash & 0x55);
		return ImGui::GetColorU32(ImVec4(R / 255.0f, G / 255.0f, B / 255.0f, Alpha));
	}

	ImVec2 AtlasPixelToPreview(const FAtlasDebugRect& Rect, const ImVec2& ImageMin, float ScaleX, float ScaleY)
	{
		return ImVec2(
			ImageMin.x + static_cast<float>(Rect.X) * ScaleX,
			ImageMin.y + static_cast<float>(Rect.Y) * ScaleY);
	}

	ImVec2 AtlasPixelSizeToPreview(const FAtlasDebugRect& Rect, float ScaleX, float ScaleY)
	{
		return ImVec2(
			static_cast<float>(Rect.W) * ScaleX,
			static_cast<float>(Rect.H) * ScaleY);
	}

	void DrawAtlasDebugRects(
		FTextureAtlasPool& AtlasPool,
		uint32 SliceIndex,
		const ImVec2& ImageMin,
		const ImVec2& ImageMax,
		bool bShowAllocatedIndices)
	{
		const uint32 AtlasSize = AtlasPool.GetTextureSize();
		if (AtlasSize == 0)
		{
			return;
		}

		const float ScaleX = (ImageMax.x - ImageMin.x) / static_cast<float>(AtlasSize);
		const float ScaleY = (ImageMax.y - ImageMin.y) / static_cast<float>(AtlasSize);
		ImDrawList* DrawList = ImGui::GetWindowDrawList();

		TArray<FAtlasDebugRect> FreeRects;
		AtlasPool.GetAllocatorFreeRects(FreeRects);
		const ImU32 FreeFillColor = ImGui::GetColorU32(ImVec4(0.1f, 0.85f, 0.25f, 0.12f));
		const ImU32 FreeOutlineColor = ImGui::GetColorU32(ImVec4(0.1f, 0.95f, 0.25f, 0.9f));
		for (const FAtlasDebugRect& Rect : FreeRects)
		{
			if (Rect.ArrayIndex != SliceIndex || Rect.W == 0 || Rect.H == 0)
			{
				continue;
			}

			const ImVec2 RectMin = AtlasPixelToPreview(Rect, ImageMin, ScaleX, ScaleY);
			const ImVec2 RectSize = AtlasPixelSizeToPreview(Rect, ScaleX, ScaleY);
			const ImVec2 RectMax(RectMin.x + RectSize.x, RectMin.y + RectSize.y);
			DrawList->AddRectFilled(RectMin, RectMax, FreeFillColor);
			DrawList->AddRect(RectMin, RectMax, FreeOutlineColor, 0.0f, 0, 1.0f);
		}

		TArray<FAtlasDebugRect> AllocatedRects;
		AtlasPool.GetAllocatorAllocatedRects(AllocatedRects);
		const ImU32 AllocatedOutlineColor = ImGui::GetColorU32(ImVec4(1.0f, 0.86f, 0.18f, 1.0f));
		const ImU32 TextColor = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
		const ImU32 TextShadowColor = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.85f));
		for (const FAtlasDebugRect& Rect : AllocatedRects)
		{
			if (Rect.ArrayIndex != SliceIndex || Rect.W == 0 || Rect.H == 0)
			{
				continue;
			}

			const ImVec2 RectMin = AtlasPixelToPreview(Rect, ImageMin, ScaleX, ScaleY);
			const ImVec2 RectSize = AtlasPixelSizeToPreview(Rect, ScaleX, ScaleY);
			const ImVec2 RectMax(RectMin.x + RectSize.x, RectMin.y + RectSize.y);
			DrawList->AddRectFilled(RectMin, RectMax, MakeAllocatedRectColor(Rect.HandleIndex, 0.22f));
			DrawList->AddRect(RectMin, RectMax, AllocatedOutlineColor, 0.0f, 0, 2.0f);

			if (bShowAllocatedIndices && RectSize.x >= 24.0f && RectSize.y >= 16.0f)
			{
				char Label[32] = {};
				std::snprintf(Label, sizeof(Label), "%u", Rect.HandleIndex);
				const ImVec2 TextSize = ImGui::CalcTextSize(Label);
				const float TextOffsetX = (RectSize.x - TextSize.x) * 0.5f;
				const float TextOffsetY = (RectSize.y - TextSize.y) * 0.5f;
				ImVec2 TextPos(
					RectMin.x + (TextOffsetX > 2.0f ? TextOffsetX : 2.0f),
					RectMin.y + (TextOffsetY > 2.0f ? TextOffsetY : 2.0f));
				DrawList->AddText(ImVec2(TextPos.x + 1.0f, TextPos.y + 1.0f), TextShadowColor, Label);
				DrawList->AddText(TextPos, TextColor, Label);
			}
		}
	}

	void DrawAtlasPriorityControls(FShadowAtlasPrioritySettings& Settings)
	{
		ImGui::Separator();
		ImGui::TextUnformatted("Atlas Priority");

		ImGui::DragFloat("Screen Coverage Weight", &Settings.ScreenCoverageWeight, 0.01f, 0.0f, 1.0f, "%.2f");
		ImGui::DragFloat("Light Contribution Weight", &Settings.LightContributionWeight, 0.01f, 0.0f, 1.0f, "%.2f");
		ImGui::DragFloat("Proximity Weight", &Settings.ProximityWeight, 0.01f, 0.0f, 1.0f, "%.2f");
		ImGui::DragFloat("Caster Receiver Weight", &Settings.CasterReceiverWeight, 0.01f, 0.0f, 1.0f, "%.2f");
		ImGui::DragFloat("Stability Weight", &Settings.StabilityWeight, 0.01f, 0.0f, 1.0f, "%.2f");

		if (ImGui::Button("Normalize Weights"))
		{
			const float Sum =
				Settings.ScreenCoverageWeight +
				Settings.LightContributionWeight +
				Settings.ProximityWeight +
				Settings.CasterReceiverWeight +
				Settings.StabilityWeight;
			if (Sum > 0.0001f)
			{
				Settings.ScreenCoverageWeight /= Sum;
				Settings.LightContributionWeight /= Sum;
				Settings.ProximityWeight /= Sum;
				Settings.CasterReceiverWeight /= Sum;
				Settings.StabilityWeight /= Sum;
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset Defaults"))
		{
			Settings = FShadowAtlasPrioritySettings{};
		}

		ImGui::DragFloat("Must Coverage Threshold", &Settings.SpotMustCoverageThreshold, 0.01f, 0.0f, 1.0f, "%.2f");
		ImGui::DragFloat("Must Proximity Threshold", &Settings.SpotMustProximityThreshold, 0.01f, 0.0f, 1.0f, "%.2f");
		ImGui::DragFloat("Hysteresis Factor", &Settings.HysteresisFactor, 0.01f, 1.0f, 4.0f, "%.2f");
		ImGui::DragFloat("Projected Resolution Scale", &Settings.ProjectedResolutionScale, 0.01f, 0.25f, 4.0f, "%.2f");
	}
}

void FEditorShadowPropertyWidget::ShowShadowProperty(ULightComponent* LightComponent)
{
	if (CurrentShowLightComponent != LightComponent)
	{
		CurrentShowLightComponent = LightComponent;
	}

	if (!ImGui::Begin("Where there is light, there is also shadow."))
	{
		ImGui::End();
		return;
	}

	ShowShadowMapPropertWindow();
	ImGui::End();
}

void FEditorShadowPropertyWidget::ShowShadowMapPropertWindow()
{
	if (!CurrentShowLightComponent)
	{
		ImGui::TextUnformatted("No light selected.");
		return;
	}

	FViewportRenderOptions* ActiveRenderOptions = nullptr;
	if (UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine))
	{
		if (FLevelEditorViewportClient* ActiveViewport = EditorEngine->GetActiveViewport())
		{
			FViewportRenderOptions& RenderOptions = ActiveViewport->GetRenderOptions();
			ActiveRenderOptions = &RenderOptions;
			bool bOverrideCamera = RenderOptions.bOverrideCameraWithSelectedLight;
			if (ImGui::Checkbox("Override camera with light's perspective", &bOverrideCamera))
			{
				RenderOptions.bOverrideCameraWithSelectedLight = bOverrideCamera;
			}

			if (CurrentShowLightComponent->GetClass() == UPointLightComponent::StaticClass())
			{
				static constexpr const char* FaceLabels[] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
				RenderOptions.PointLightPreviewFaceIndex = RenderOptions.PointLightPreviewFaceIndex < static_cast<uint32>(std::size(FaceLabels))
					? RenderOptions.PointLightPreviewFaceIndex
					: 0u;

				if (ImGui::BeginCombo("Point Light Preview Face", FaceLabels[RenderOptions.PointLightPreviewFaceIndex]))
				{
					for (uint32 FaceIndex = 0; FaceIndex < static_cast<uint32>(std::size(FaceLabels)); ++FaceIndex)
					{
						const bool bSelected = RenderOptions.PointLightPreviewFaceIndex == FaceIndex;
						if (ImGui::Selectable(FaceLabels[FaceIndex], bSelected))
						{
							RenderOptions.PointLightPreviewFaceIndex = FaceIndex;
						}
						if (bSelected)
						{
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}
			}

			ImGui::BeginDisabled(true);
			EShadowFilterMode FilterMode = RenderOptions.ShadowFilterMode;
			ImGui::Text("Shadow Filter: %s",
					FilterMode == EShadowFilterMode::VSM ? "VSM" : 
					FilterMode == EShadowFilterMode::PCF ?  "PCF" : "None");
			ImGui::EndDisabled();
		}
		else
		{
			ImGui::TextDisabled("Shadow viewport settings unavailable without an active viewport.");
		}
	}

	ImGui::Separator();

	if (ImGui::RadioButton("Selected Light ShadowMap", PreviewMode == EShadowPreviewMode::SelectedLight))
	{
		PreviewMode = EShadowPreviewMode::SelectedLight;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Atlas Texture Layer", PreviewMode == EShadowPreviewMode::AtlasLayer))
	{
		PreviewMode = EShadowPreviewMode::AtlasLayer;
	}

	UWorld* World = CurrentShowLightComponent->GetWorld();
	if (!World)
	{
		ImGui::TextUnformatted("No world for selected light.");
		return;
	}
	if (!World->GetScene().IsShadowAtlasInitialized())
	{
		ImGui::TextUnformatted("Shadow atlas is not initialized for selected light's world.");
		return;
	}

	FTextureAtlasPool& AtlasPool = World->GetScene().GetShadowAtlasPool();
	const uint32 AllocatedLayerCount = AtlasPool.GetAllocatedLayerCount();
	const int32 MaxLayerIndex = AllocatedLayerCount > 0 ? static_cast<int32>(AllocatedLayerCount - 1) : 0;

	if (PreviewAtlasLayerIndex < 0)
	{
		PreviewAtlasLayerIndex = 0;
	}
	else if (PreviewAtlasLayerIndex > MaxLayerIndex)
	{
		PreviewAtlasLayerIndex = MaxLayerIndex;
	}

	ID3D11ShaderResourceView* PreviewSRV = nullptr;
	if (PreviewMode == EShadowPreviewMode::SelectedLight)
	{
		if (CurrentShowLightComponent->GetClass() == UPointLightComponent::StaticClass())
		{
			const FShadowMapKey ShadowMapKey = CurrentShowLightComponent->GetShadowMapKey();
			PreviewSRV = FTextureCubeShadowPool::Get().GetDebugSRV(ShadowMapKey.CubeMap);
		}
		else
		{
			FShadowHandleSet* Handle = CurrentShowLightComponent->GetShadowHandleSet();
			PreviewSRV = Handle ? AtlasPool.GetDebugSRV(Handle) : nullptr;
		}
		if (!PreviewSRV)
		{
			ImGui::TextUnformatted("No shadow map for selected light.");
			return;
		}
	}
	else
	{
		PreviewSRV = AtlasPool.GetDebugLayerSRV(static_cast<uint32>(PreviewAtlasLayerIndex));
		//PreviewSRV = AtlasPool.GetSliceSRV(PreviewAtlasLayerIndex);
		if (!PreviewSRV)
		{
			ImGui::TextUnformatted("Atlas layer preview unavailable.");
			return;
		}
	}

	const ImVec2 PreviewImageSize(500, 500);
	if (PreviewMode == EShadowPreviewMode::AtlasLayer)
	{
		if (ImGui::BeginTable("ShadowAtlasPreviewLayout", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings))
		{
			ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthFixed, PreviewImageSize.x + 8.0f);
			ImGui::TableSetupColumn("Controls", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextColumn();
			ImGui::Image(PreviewSRV, PreviewImageSize);
			const ImVec2 ImageMin = ImGui::GetItemRectMin();
			const ImVec2 ImageMax = ImGui::GetItemRectMax();
			if (bShowAtlasRectOverlay)
			{
				DrawAtlasDebugRects(
					AtlasPool,
					static_cast<uint32>(PreviewAtlasLayerIndex),
					ImageMin,
					ImageMax,
					bShowAtlasAllocatedIndices);
			}

			ImGui::TableNextColumn();
			ImGui::SliderInt("Atlas Layer", &PreviewAtlasLayerIndex, 0, MaxLayerIndex);
			ImGui::Checkbox("Show Atlas Rect Overlay", &bShowAtlasRectOverlay);
			ImGui::Checkbox("Show Allocated Indices", &bShowAtlasAllocatedIndices);
			if (ActiveRenderOptions)
			{
				DrawAtlasPriorityControls(ActiveRenderOptions->ShadowAtlasPriority);
			}
			else
			{
				ImGui::TextDisabled("Atlas priority controls unavailable without an active viewport.");
			}

			ImGui::EndTable();
		}
		return;
	}

	ImGui::Image(PreviewSRV, PreviewImageSize);
}
