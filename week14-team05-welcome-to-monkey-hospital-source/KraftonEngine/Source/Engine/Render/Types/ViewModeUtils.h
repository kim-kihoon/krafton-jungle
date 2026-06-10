#pragma once

#include "Render/Types/ViewTypes.h"

namespace ViewModeUtils
{
	inline bool IsSceneViewMode(EViewMode ViewMode)
	{
		switch (ViewMode)
		{
		case EViewMode::Lit_Phong:
		case EViewMode::Unlit:
		case EViewMode::Lit_Gouraud:
		case EViewMode::Lit_Lambert:
		case EViewMode::Wireframe:
			return true;
		default:
			return false;
		}
	}

	inline bool IsPureMeshDebugViewMode(EViewMode ViewMode)
	{
		return ViewMode == EViewMode::WorldNormal || ViewMode == EViewMode::LightCulling;
	}

	inline bool IsFullscreenDebugViewMode(EViewMode ViewMode)
	{
		return ViewMode == EViewMode::SceneDepth;
	}

	inline bool IsPureDebugViewMode(EViewMode ViewMode)
	{
		return IsPureMeshDebugViewMode(ViewMode) || IsFullscreenDebugViewMode(ViewMode);
	}

	inline bool SuppressesEditorOverlays(EViewMode ViewMode)
	{
		return IsPureDebugViewMode(ViewMode) || ViewMode == EViewMode::DoFCoC;
	}

	inline bool IsBoneHeatmapOverlayEnabled(const FViewportRenderOptions& Options)
	{
		return Options.bWeightBoneHeatMap && Options.WeightBoneHeatMapBoneIndex >= 0;
	}

	inline bool IsClothMaxDistanceOverlayEnabled(const FViewportRenderOptions& Options)
	{
		return Options.bClothMaxDistanceOverlay &&
			Options.ClothOverlayLODIndex >= 0 &&
			Options.ClothOverlayIndex >= 0;
	}
}
