#pragma once
#include "datatype.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"

class PlayerInput
{
private:
	ImGuiIO& io;

public:
	PlayerInput();
	virtual ~PlayerInput();

	FVector GetMousePosition();

	//0 -> left
	//1 -> right
	//3 -> middle
	bool IsMouseClicked(int mouseButton);
	bool IsMouseDown(int mouseButton);
	bool IsMouseReleased(int mouseButton);

	//	Flaging
	bool IsMouseHoveringUI();
};