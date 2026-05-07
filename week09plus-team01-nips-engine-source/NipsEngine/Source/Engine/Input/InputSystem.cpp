#include "Engine/Input/InputSystem.h"
#include <algorithm>
#include <cmath>

void InputSystem::Tick()
{
	bIgnoreMouseDelta = IgnoreMouseDeltaTicks > 0;
	if (IgnoreMouseDeltaTicks > 0)
	{
		--IgnoreMouseDeltaTicks;
	}

	// 윈도우 포커스가 없으면 모든 입력 상태 해제
	if (OwnerHWnd && GetForegroundWindow() != OwnerHWnd)
	{
		for (int i = 0; i < 256; ++i)
		{
			PrevStates[i] = CurrentStates[i];
			CurrentStates[i] = false;
		}
		bIsMouseLocked = false;

		bLeftDragJustStarted = false;
		bMiddleDragJustStarted = false;
		bRightDragJustStarted = false;

		bLeftDragJustEnded = bLeftDragging;
		bMiddleDragJustEnded = bMiddleDragging;
		bRightDragJustEnded = bRightDragging;

		bLeftDragging = false;
		bMiddleDragging = false;
		bRightDragging = false;
		bLeftDragCandidate = false;
		bMiddleDragCandidate = false;
		bRightDragCandidate = false;
		PrevScrollDelta = ScrollDelta;
		ScrollDelta = 0;
		// 마우스 위치 동기화 (복귀 시 델타 점프 방지)
		GetCursorPos(&MousePos);
		PrevMousePos = MousePos;
		bIgnoreMouseDelta = true;
		IgnoreMouseDeltaTicks = 1;
		return;
	}

	// 단 한 번의 API 호출로 모든 키 상태 갱신 (CPU 10% 병목)
	BYTE keyState[256];
	if (GetKeyboardState(keyState))
	{
		for (int i = 0; i < 256; ++i)
		{
			PrevStates[i] = CurrentStates[i];
			// GetKeyboardState는 최상위 비트(0x80)가 눌림 상태를 나타냄
			CurrentStates[i] = (keyState[i] & 0x80) != 0;
		}
	}

	bLeftDragJustStarted = false;
	bMiddleDragJustStarted = false;
	bRightDragJustStarted = false;
	bLeftDragJustEnded = false;
	bMiddleDragJustEnded = false;
	bRightDragJustEnded = false;

	PrevScrollDelta = ScrollDelta;
	ScrollDelta = 0;

	PrevMousePos = MousePos;
	GetCursorPos(&MousePos);

	if (bIsMouseLocked)
	{
		SetCursorPos(LockedCenterScreen.x, LockedCenterScreen.y);
	}

	if (GetKeyDown(VK_LBUTTON))
	{
		bLeftDragCandidate = true;
		LeftMouseDownPos = MousePos;
	}

	if (GetKeyDown(VK_MBUTTON))
	{
		bMiddleDragCandidate = true;
		MiddleMouseDownPos = MousePos;
	}

	if (GetKeyDown(VK_RBUTTON))
	{
		bRightDragCandidate = true;
		RightMouseDownPos = MousePos;
	}

	// Left drag
	if (!bLeftDragging && IsDraggingLeft())
	{
		FilterDragThreshold(bLeftDragCandidate, bLeftDragging, bLeftDragJustStarted, LeftMouseDownPos,
							LeftDragStartPos);
	}
	else if (GetKeyUp(VK_LBUTTON))
	{
		if (bLeftDragging)
			bLeftDragJustEnded = true;
		bLeftDragging = false;
		bLeftDragCandidate = false;
	}

	// Middle drag
	if (!bMiddleDragging && IsDraggingMiddle())
	{
		FilterDragThreshold(bMiddleDragCandidate, bMiddleDragging, bMiddleDragJustStarted, MiddleMouseDownPos,
							MiddleDragStartPos);
	}
	else if (GetKeyUp(VK_MBUTTON))
	{
		if (bMiddleDragging)
			bMiddleDragJustEnded = true;
		bMiddleDragging = false;
		bMiddleDragCandidate = false;
	}

	// Right drag
	if (!bRightDragging && IsDraggingRight())
	{
		FilterDragThreshold(bRightDragCandidate, bRightDragging, bRightDragJustStarted, RightMouseDownPos,
							RightDragStartPos);
	}
	else if (GetKeyUp(VK_RBUTTON))
	{
		if (bRightDragging)
			bRightDragJustEnded = true;
		bRightDragging = false;
		bRightDragCandidate = false;
	}
}

void InputSystem::FilterDragThreshold(bool& bCandidate, bool& bDragging, bool& bJustStarted, const POINT& MouseDownPos,
									  POINT& DragStartPos)
{
	if (bCandidate && !bDragging)
	{
		int DX = MousePos.x - MouseDownPos.x;
		int DY = MousePos.y - MouseDownPos.y;
		int DistSq = DX * DX + DY * DY;

		if (DistSq >= DRAG_THRESHOLD * DRAG_THRESHOLD)
		{
			bJustStarted = true;
			bDragging = true;
			DragStartPos = MouseDownPos;
		}
	}
}

POINT InputSystem::GetLeftDragVector() const
{
	POINT V;
	V.x = MousePos.x - LeftDragStartPos.x;
	V.y = MousePos.y - LeftDragStartPos.y;
	return V;
}

float InputSystem::GetLeftDragDistance() const
{
	POINT V = GetLeftDragVector();
	return std::sqrt(static_cast<float>(V.x * V.x + V.y * V.y));
}

POINT InputSystem::GetMiddleDragVector() const
{
	POINT V;
	V.x = MousePos.x - MiddleDragStartPos.x;
	V.y = MousePos.y - MiddleDragStartPos.y;
	return V;
}

float InputSystem::GetMiddleDragDistance() const
{
	POINT V = GetMiddleDragVector();
	return std::sqrt(static_cast<float>(V.x) * V.x + V.y * V.y);
}

POINT InputSystem::GetRightDragVector() const
{
	POINT V;
	V.x = MousePos.x - RightDragStartPos.x;
	V.y = MousePos.y - RightDragStartPos.y;
	return V;
}

float InputSystem::GetRightDragDistance() const
{
	POINT V = GetRightDragVector();
	return std::sqrt(static_cast<float>(V.x * V.x + V.y * V.y));
}

// --- Mouse lock ------------------------------------------------
void InputSystem::LockMouse(bool bLock, float x, float y, float w, float h)
{
	const bool bWasMouseLocked = bIsMouseLocked;

	bIsMouseLocked = bLock;
	if (bIsMouseLocked)
	{
		auto WarpX = x + w * 0.5f;
		auto WarpY = y + h * 0.5f;
		POINT NewLockedCenterScreen = {static_cast<LONG>(WarpX), static_cast<LONG>(WarpY)};
		const bool bLockCenterChanged = NewLockedCenterScreen.x != LockedCenterScreen.x || NewLockedCenterScreen.y != LockedCenterScreen.y;

		LockedCenterScreen = NewLockedCenterScreen;
		if (!bWasMouseLocked || bLockCenterChanged)
		{
			SetCursorPos(LockedCenterScreen.x, LockedCenterScreen.y);
			MousePos = LockedCenterScreen;
			PrevMousePos = LockedCenterScreen;
			bIgnoreMouseDelta = true;
			IgnoreMouseDeltaTicks = 1;
		}
	}
	else if (bWasMouseLocked)
	{
		GetCursorPos(&MousePos);
		PrevMousePos = MousePos;
		bIgnoreMouseDelta = true;
		IgnoreMouseDeltaTicks = 1;
	}
}

void InputSystem::ResetMouseDelta(int SuppressTicks)
{
	if (bIsMouseLocked)
	{
		SetCursorPos(LockedCenterScreen.x, LockedCenterScreen.y);
		MousePos = LockedCenterScreen;
		PrevMousePos = LockedCenterScreen;
	}
	else
	{
		GetCursorPos(&MousePos);
		PrevMousePos = MousePos;
	}

	bIgnoreMouseDelta = true;
	IgnoreMouseDeltaTicks = std::max(1, SuppressTicks);
}

void InputSystem::SetCursorVisibility(bool bVisible)
{
	if (bVisible)
	{
		while (ShowCursor(true) < 0)
		{
		}
	}
	else
	{
		while (ShowCursor(false) >= 0)
		{
		}
	}
}
