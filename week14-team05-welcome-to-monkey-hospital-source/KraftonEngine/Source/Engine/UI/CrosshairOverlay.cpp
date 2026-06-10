#include "UI/CrosshairOverlay.h"

#include "ImGui/imgui.h"

namespace
{
	bool bCrosshairVisible = false;
}

void FCrosshairOverlay::SetVisible(bool bInVisible)
{
	bCrosshairVisible = bInVisible;
}

bool FCrosshairOverlay::IsVisible()
{
	return bCrosshairVisible;
}

void FCrosshairOverlay::Draw(ImDrawList* DrawList, const ImVec2& Center)
{
	if (!DrawList)
	{
		return;
	}

	const float Radius = 1.4f;
	DrawList->AddCircleFilled(Center, Radius, IM_COL32(150, 150, 150, 255), 10);
}
