#include "PlayerInput.h"
#include <Windows.h>

PlayerInput::PlayerInput() : io(ImGui::GetIO())
{
}

PlayerInput::~PlayerInput()
{
}

FVector PlayerInput::GetMousePosition()
{
	//0 ~ display size
    float mouseX = io.MousePos.x;
    float mouseY = io.MousePos.y;

	float width = 1050.f;
	float height = io.DisplaySize.y;


	//-1 ~ 1 사이 좌표 리턴
	return FVector((mouseX / width - 0.5f) * 2.f, -(mouseY / height - 0.5f) * 2.f, 0.f);
}

//클릭
bool PlayerInput::IsMouseClicked(int mouseButton)
{
	return ImGui::IsMouseClicked(mouseButton);
}

//누르고 있음
bool PlayerInput::IsMouseDown(int mouseButton)
{
	return ImGui::IsMouseDown(mouseButton);
}

//뗌
bool PlayerInput::IsMouseReleased(int mouseButton)
{
	return ImGui::IsMouseReleased(mouseButton);
}

bool PlayerInput::IsMouseHoveringUI()
{
	return ImGui::IsAnyItemHovered();
}