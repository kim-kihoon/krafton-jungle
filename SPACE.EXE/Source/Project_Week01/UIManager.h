#pragma once
#include "UIFrame.h"
#include "URenderer.h"
#include <Memory>

class UIManager
{
private:
	std::vector<std::unique_ptr<UIFrame>> UIFrameList;
public:
	UIManager();
	~UIManager();

	UIFrame& CreateFrame(const std::string& title);

	static void InitImGui(HWND hWnd, URenderer* renderer);
	static void ShutdownImGui();

	void Render();
};
