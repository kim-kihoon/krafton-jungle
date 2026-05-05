#pragma once
#include "Core/CoreTypes.h"

class IInputController
{
public:
	virtual ~IInputController() = default;

	virtual void OnMouseMove(float DeltaX, float DeltaY) = 0;
	virtual void OnMouseMoveAbsolute(float X, float Y) {}
	virtual void OnLeftMouseClick(float X, float Y) = 0;
	virtual void OnLeftMouseDragEnd(float X, float Y) = 0;
	virtual void OnLeftMouseButtonUp(float X, float Y) = 0;
	virtual void OnRightMouseClick(float DeltaX, float DeltaY) = 0;
	virtual void OnLeftMouseDrag(float X, float Y) = 0;
	virtual void OnRightMouseDrag(float DeltaX, float DeltaY) = 0;
	virtual void OnMiddleMouseDrag(float DeltaX, float DeltaY) = 0;
	virtual void OnKeyPressed(int VK) = 0;
	virtual void OnKeyDown(int VK) = 0;
	virtual void OnKeyReleased(int VK) = 0;
	virtual void OnWheelScrolled(float Notch) = 0;
	virtual void Tick(float InDeltaTime) { DeltaTime = InDeltaTime; }
	void SetInputEnabled(bool bEnabled) { bInputEnabled = bEnabled; }
	bool IsInputEnabled() const { return bInputEnabled; }

	void SetInputModifiers(bool bInCtrlDown, bool bInAltDown, bool bInShiftDown)
	{
		bCtrlDown = bInCtrlDown;
		bAltDown = bInAltDown;
		bShiftDown = bInShiftDown;
	}

	void SetViewportDim(float X, float Y, float Width, float Height)
	{
		ViewportX = X;
		ViewportY = Y;
		ViewportWidth = Width;
		ViewportHeight = Height;
	}

protected:
	IInputController() = default;

	float ViewportX      = 0;
	float ViewportY      = 0;
	float ViewportWidth  = 0;
	float ViewportHeight = 0;
	float DeltaTime      = 0;
	bool  bCtrlDown      = false;
	bool  bAltDown       = false;
	bool  bShiftDown     = false;
	bool  bInputEnabled  = true;
};
