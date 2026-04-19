#include "UIManager.h"
#include <iostream>

UIManager::UIManager()
{
}

UIManager::~UIManager()
{
	UIFrameList.clear();
}

UIFrame& UIManager::CreateFrame(const std::string& title)
{
	UIFrameList.push_back(std::make_unique<UIFrame>(title));
	return *UIFrameList.back();
}

void UIManager::InitImGui(HWND hWnd, URenderer* renderer)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui_ImplWin32_Init((void*)hWnd);
	ImGui_ImplDX11_Init(renderer->Device, renderer->DeviceContext);
}

void UIManager::ShutdownImGui()
{
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void UIManager::Render()
{
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();

	ImGui::NewFrame();

	std::sort(UIFrameList.begin(), UIFrameList.end(),
		[](const std::unique_ptr<UIFrame>& a, const std::unique_ptr<UIFrame>& b)
		{
			return a->GetLayer() < b->GetLayer();
		});

	for (auto& frame : UIFrameList)
	{
		//std::cout << frame->GetTitle() << " Layer: " << frame->GetLayer() << std::endl;
		frame->Render();
	}
	 
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}