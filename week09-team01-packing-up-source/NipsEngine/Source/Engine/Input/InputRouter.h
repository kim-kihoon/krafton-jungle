#pragma once

#include "Engine/GameFramework/WorldContext.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Input/InputTypes.h"
#include "Viewport/ViewportRect.h"

class FWindowsWindow;

class IInputController;
class IUIInputHandler;

struct FInputRouteContext
{
	FWindowsWindow* Window = nullptr;
	FViewportRect ViewportRect;
	bool bHovered = false;
	bool bControlLocked = false;
	bool bInputActive = true;
	bool bHasActiveCamera = false;
	bool bUseCustomCursor = false;
};

class FInputRouter
{
public:
	static void SetWorldType(EWorldType InWorldType);
	static EWorldType GetWorldType();

	void SetEditorWorldController(IInputController* InController) { EditorWorldController = InController; }
	void SetPIEController(IInputController* InController) { PIEController = InController; }
	void SetGamePlayerController(IInputController* InController) { GamePlayerController = InController; }
	void SetUIInputHandler(IUIInputHandler* InHandler) { UIInputHandler = InHandler; }

	void Tick(float DeltaTime);
	void Tick(float DeltaTime, const FInputRouteContext& Context);
	void RouteKeyboardInput(EKeyInputType Type, int VK);
	void RouteMouseInput(EMouseInputType Type, float DeltaX, float DeltaY);
	void SetViewportDim(float X, float Y, float Width, float Height);

	static POINT GetMousePos();
	static bool GetKey(int VK);
	static bool GetKeyDown(int VK);
	static bool GetKeyUp(int VK);
	static bool GetRightDragging();
	static bool GetMiddleDragging();
	static bool GetLeftDragging();
	static bool GetLeftDragEnd();
	static int MouseDeltaX();
	static int MouseDeltaY();
	static bool MouseMoved();
	static float GetScrollNotches();
	static FGuiInputState& GetGuiInputState();
	static void SetCursorVisibility(bool bVisible);
	static void LockMouse(bool bLock, float X = 0, float Y = 0, float Width = 0, float Height = 0);
	static void ResetMouseDelta(int SuppressTicks = 1);
	static void TickInputSystem();
	static void SetOwnerWindow(HWND HWnd);
	static void AddScrollDelta(int Delta);

private:
	IInputController* GetActiveController() const;
	void UpdateControllerModifiers();
	void TickCursorCapture(const FInputRouteContext& Context);
	void TickKeyboardInput(const FInputRouteContext& Context);
	void TickMouseInput(const FInputRouteContext& Context);
	void LockCursorToContextViewport(const FInputRouteContext& Context);
	bool IsPIESpecialKey(int VK) const;

private:
	IInputController* EditorWorldController = nullptr;
	IInputController* PIEController = nullptr;
	IInputController* GamePlayerController = nullptr;
	IUIInputHandler* UIInputHandler = nullptr;

	static EWorldType WorldType;
};
