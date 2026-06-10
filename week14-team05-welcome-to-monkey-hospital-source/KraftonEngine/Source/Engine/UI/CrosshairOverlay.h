#pragma once

struct ImDrawList;
struct ImVec2;

class FCrosshairOverlay
{
public:
	static void SetVisible(bool bInVisible);
	static bool IsVisible();
	static void Draw(ImDrawList* DrawList, const ImVec2& Center);
};
