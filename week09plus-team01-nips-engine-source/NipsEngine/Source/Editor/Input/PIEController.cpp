#include "PIEController.h"
#include "Engine/Component/Physics/PhysicsHandleComponent.h"
#include <windows.h>

void FPIEController::Tick(float InDeltaTime)
{
	IBaseEditorController::Tick(InDeltaTime);
}

void FPIEController::OnMouseMove(float DeltaX, float DeltaY)
{
	(void)DeltaX;
	(void)DeltaY;
}

void FPIEController::OnLeftMouseClick(float X, float Y)
{
	(void)X;
	(void)Y;
}

void FPIEController::OnLeftMouseDragEnd(float X, float Y)
{
	(void)X;
	(void)Y;
}

void FPIEController::OnLeftMouseButtonUp(float X, float Y)
{
	(void)X;
	(void)Y;
}

void FPIEController::OnRightMouseClick(float DeltaX, float DeltaY)
{
	(void)DeltaX;
	(void)DeltaY;
}

void FPIEController::OnLeftMouseDrag(float X, float Y)
{
	(void)X;
	(void)Y;
}

void FPIEController::OnRightMouseDrag(float DeltaX, float DeltaY)
{
	(void)DeltaX;
	(void)DeltaY;
}

void FPIEController::OnMiddleMouseDrag(float DeltaX, float DeltaY)
{
	(void)DeltaX;
	(void)DeltaY;
}

void FPIEController::OnKeyPressed(int VK)
{
	switch (VK)
	{
	case VK_ESCAPE:
		if (OnRequestEndPIE)
			OnRequestEndPIE();
		break;
	case VK_F4:
		if (OnRequestToggleInputCapture)
			OnRequestToggleInputCapture();
		break;
	default:
		break;
	}
}

void FPIEController::OnKeyDown(int VK)
{
	(void)VK;
}

void FPIEController::OnKeyReleased(int VK)
{
	(void)VK;
}

void FPIEController::OnWheelScrolled(float Notch)
{
	(void)Notch;
}