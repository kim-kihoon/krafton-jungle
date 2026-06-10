#include "Viewport/GameViewportClient.h"

#include "Component/Camera/CameraComponent.h"
#include "Engine/Input/InputSystem.h"
#include "Math/MathUtils.h"
#include "UI/UIManager.h"
#include "Core/Logging/Log.h"

#include <windows.h>

void UGameViewportClient::BeginGameSession(FViewport* InViewport)
{
	Viewport = InViewport;
	ClearGameInputSnapshot();
	ResetInputState();
}

void UGameViewportClient::EndGameSession()
{
	SetInputPossessed(false);
	ResetInputState();
	bHasCursorClipRect = false;
	// Shutdown 경로에서는 ProcessInput 이 더 이상 안 돌아 — 커서 캡처/clip 을 명시적으로 해제.
	// 이걸 안 풀면 ::ShowCursor 카운터 음수 + ::ClipCursor 클립이 종료 후에도 남아 다른 앱
	// 까지 영향받음 (특히 ClipCursor 는 프로세스 종료 후에도 잔존하다가 다음 SetCursorPos
	// 까지 유지될 수 있다).
	SetCursorCaptured(false);
	Viewport = nullptr;
}

namespace
{
	bool IsMouseVirtualKey(int VK)
	{
		return VK == VK_LBUTTON || VK == VK_RBUTTON || VK == VK_MBUTTON ||
			VK == VK_XBUTTON1 || VK == VK_XBUTTON2;
	}

	void ClearMouseInput(FInputSystemSnapshot& Snapshot)
	{
		const int MouseKeys[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
		for (int VK : MouseKeys)
		{
			Snapshot.KeyDown[VK] = false;
			Snapshot.KeyPressed[VK] = false;
			Snapshot.KeyReleased[VK] = false;
		}

		Snapshot.MouseDeltaX = 0;
		Snapshot.MouseDeltaY = 0;
		Snapshot.ScrollDelta = 0;
		Snapshot.bLeftMouseDown = false;
		Snapshot.bLeftMousePressed = false;
		Snapshot.bLeftMouseReleased = false;
		Snapshot.bRightMouseDown = false;
		Snapshot.bRightMousePressed = false;
		Snapshot.bRightMouseReleased = false;
		Snapshot.bMiddleMouseDown = false;
		Snapshot.bMiddleMousePressed = false;
		Snapshot.bMiddleMouseReleased = false;
		Snapshot.bXButton1Down = false;
		Snapshot.bXButton1Pressed = false;
		Snapshot.bXButton1Released = false;
		Snapshot.bXButton2Down = false;
		Snapshot.bXButton2Pressed = false;
		Snapshot.bXButton2Released = false;
		Snapshot.bLeftDragStarted = false;
		Snapshot.bLeftDragging = false;
		Snapshot.bLeftDragEnded = false;
		Snapshot.LeftDragVector = { 0, 0 };
		Snapshot.bRightDragStarted = false;
		Snapshot.bRightDragging = false;
		Snapshot.bRightDragEnded = false;
		Snapshot.RightDragVector = { 0, 0 };
	}

	void ClearKeyboardInput(FInputSystemSnapshot& Snapshot)
	{
		for (int VK = 0; VK < 256; ++VK)
		{
			if (IsMouseVirtualKey(VK))
			{
				continue;
			}
			Snapshot.KeyDown[VK] = false;
			Snapshot.KeyPressed[VK] = false;
			Snapshot.KeyReleased[VK] = false;
		}
	}
}

void UGameViewportClient::ProcessInput(const FInputSystemSnapshot& Snapshot, float /*DeltaTime*/)
{
	ClearGameInputSnapshot();

	if (!Snapshot.bWindowFocused)
	{
		ReleaseGameCapture();
		ResetInputState();
		return;
	}

	if (!bInputPossessed)
	{
		ReleaseGameCapture();
		return;
	}

	const FUIInputCaptureState UIState = UUIManager::Get().GetViewportInputCaptureState();
	ApplyGameCapturePolicy(UIState);

	if (InputMode == EGameInputMode::UIOnly || UIState.bBlocksGameInput)
	{
		return;
	}

	FInputSystemSnapshot GameSnapshot = Snapshot;
	const bool bBlockGameMouse = UIState.bWantsMouse || UIState.bBlocksGameMouseLook;
	const bool bBlockGameKeyboard = UIState.bWantsTextInput || UIState.bBlocksGameKeyboard;

	if (bBlockGameMouse)
	{
		ClearMouseInput(GameSnapshot);
	}
	if (bBlockGameKeyboard)
	{
		ClearKeyboardInput(GameSnapshot);
	}

	SetGameInputSnapshot(GameSnapshot);
}

void UGameViewportClient::SetInputPossessed(bool bPossessed)
{
	if (bInputPossessed == bPossessed)
	{
		return;
	}

	bInputPossessed = bPossessed;
	ResetInputState();

	// 커서 가시성/캡처는 ProcessInput 이 매 프레임 possess + UI WantsMouse 를 보고 결정.
	// 여기서는 게임 입력 라우팅만 토글한다.

	// possess off 로 전환되는 순간 GameInputSnapshot 도 비워서 Lua 폴링이 즉시 빈 입력을 본다.
	// (ProcessInput 호출이 멈춘 뒤에도 이전 값이 남아있는 케이스 방지.)
	if (!bPossessed)
	{
		ClearGameInputSnapshot();
		ReleaseGameCapture();
	}
}

void UGameViewportClient::SetInputMode(EGameInputMode InMode)
{
	if (InputMode == InMode)
	{
		return;
	}

	InputMode = InMode;
	ClearGameInputSnapshot();
	ResetInputState();
	if (InputMode == EGameInputMode::UIOnly)
	{
		ReleaseGameCapture();
	}
}

void UGameViewportClient::SetCursorClipRect(const FRect& InViewportScreenRect)
{
	if (InViewportScreenRect.Width <= 1.0f || InViewportScreenRect.Height <= 1.0f)
	{
		bHasCursorClipRect = false;
		if (bCursorCaptured)
		{
			ApplyCursorClip();
		}
		return;
	}

	CursorClipClientRect.left = static_cast<LONG>(InViewportScreenRect.X);
	CursorClipClientRect.top = static_cast<LONG>(InViewportScreenRect.Y);
	CursorClipClientRect.right = static_cast<LONG>(InViewportScreenRect.X + InViewportScreenRect.Width);
	CursorClipClientRect.bottom = static_cast<LONG>(InViewportScreenRect.Y + InViewportScreenRect.Height);
	bHasCursorClipRect = CursorClipClientRect.right > CursorClipClientRect.left
		&& CursorClipClientRect.bottom > CursorClipClientRect.top;

	if (bCursorCaptured)
	{
		ApplyCursorClip();
	}
}

void UGameViewportClient::ResetInputState()
{
	InputSystem::Get().ResetMouseDelta();
	InputSystem::Get().ResetWheelDelta();
}

void UGameViewportClient::ReleaseGameCapture()
{
	InputSystem::Get().SetUseRawMouse(false);
	SetCursorCaptured(false);
}

void UGameViewportClient::ApplyGameCapturePolicy(const FUIInputCaptureState& UIState)
{
	const bool bShouldCaptureMouse = InputMode != EGameInputMode::UIOnly &&
		!UIState.bWantsMouse &&
		!UIState.bBlocksGameInput &&
		!UIState.bBlocksGameMouseLook;

	InputSystem::Get().SetUseRawMouse(bShouldCaptureMouse);
	SetCursorCaptured(bShouldCaptureMouse);
}

void UGameViewportClient::SetCursorCaptured(bool bCaptured)
{
	if (bCursorCaptured == bCaptured)
	{
		if (bCaptured)
		{
			ApplyCursorClip();
		}
		return;
	}

	bCursorCaptured = bCaptured;
	if (bCursorCaptured)
	{
		while (::ShowCursor(FALSE) >= 0) {}
		ApplyCursorClip();
		return;
	}

	while (::ShowCursor(TRUE) < 0) {}
	::ClipCursor(nullptr);
}

void UGameViewportClient::ApplyCursorClip()
{
	if (!OwnerHWnd)
	{
		return;
	}

	RECT ClientRect = {};
	if (bHasCursorClipRect)
	{
		ClientRect = CursorClipClientRect;
	}
	else if (!::GetClientRect(OwnerHWnd, &ClientRect))
	{
		return;
	}

	POINT TopLeft = { ClientRect.left, ClientRect.top };
	POINT BottomRight = { ClientRect.right, ClientRect.bottom };
	if (!::ClientToScreen(OwnerHWnd, &TopLeft) || !::ClientToScreen(OwnerHWnd, &BottomRight))
	{
		return;
	}

	RECT ScreenRect = { TopLeft.x, TopLeft.y, BottomRight.x, BottomRight.y };
	if (ScreenRect.right > ScreenRect.left && ScreenRect.bottom > ScreenRect.top)
	{
		::ClipCursor(&ScreenRect);
	}
}

void UGameViewportClient::SetGameInputSnapshot(const FInputSystemSnapshot& Snapshot)
{
	GameInputSnapshot = Snapshot;
	bHasGameInputSnapshot = true;
}

void UGameViewportClient::ClearGameInputSnapshot()
{
	GameInputSnapshot = FInputSystemSnapshot{};
	bHasGameInputSnapshot = false;
}
