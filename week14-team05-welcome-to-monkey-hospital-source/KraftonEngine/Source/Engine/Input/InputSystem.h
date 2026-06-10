#pragma once

#include <windows.h>

#include "Core/Singleton.h"
#include "Input/InputDevice.h"

struct FGuiInputState
{
	bool bUsingMouse = false;
	bool bUsingKeyboard = false;
	bool bUsingTextInput = false;
};

struct FInputSystemSnapshot
{
	bool KeyDown[256] = {};
	bool KeyPressed[256] = {};
	bool KeyReleased[256] = {};

	POINT MousePos = { 0, 0 };
	int MouseDeltaX = 0;
	int MouseDeltaY = 0;
	int ScrollDelta = 0;

	bool bLeftMouseDown = false;
	bool bLeftMousePressed = false;
	bool bLeftMouseReleased = false;
	bool bRightMouseDown = false;
	bool bRightMousePressed = false;
	bool bRightMouseReleased = false;
	bool bMiddleMouseDown = false;
	bool bMiddleMousePressed = false;
	bool bMiddleMouseReleased = false;
	bool bXButton1Down = false;
	bool bXButton1Pressed = false;
	bool bXButton1Released = false;
	bool bXButton2Down = false;
	bool bXButton2Pressed = false;
	bool bXButton2Released = false;

	bool bLeftDragStarted = false;
	bool bLeftDragging = false;
	bool bLeftDragEnded = false;
	POINT LeftDragVector = { 0, 0 };

	bool bRightDragStarted = false;
	bool bRightDragging = false;
	bool bRightDragEnded = false;
	POINT RightDragVector = { 0, 0 };

	bool bUsingRawMouse = false;
	bool bGuiUsingMouse = false;
	bool bGuiUsingKeyboard = false;
	bool bGuiUsingTextInput = false;
	bool bWindowFocused = true;

	EInputDeviceClass PrimaryInputDevice = EInputDeviceClass::KeyboardMouse;
	FInputDeviceSnapshot GamepadSnapshot{};
	TMap<FString, FInputActionState> Actions;
	TMap<FString, FInputAxisState> Axes;

	bool IsDown(int VK) const { return VK >= 0 && VK < 256 && KeyDown[VK]; }
	bool WasPressed(int VK) const { return VK >= 0 && VK < 256 && KeyPressed[VK]; }
	bool WasReleased(int VK) const { return VK >= 0 && VK < 256 && KeyReleased[VK]; }
	bool IsActionDown(const FString& Name) const;
	bool WasActionPressed(const FString& Name) const;
	bool WasActionReleased(const FString& Name) const;
	float GetAxis(const FString& Name) const;
};

class InputSystem : public TSingleton<InputSystem>
{
	friend class TSingleton<InputSystem>;

public:
	void InitializeDevices();
	void ShutdownDevices();
	void Tick();
	FInputSystemSnapshot TickAndMakeSnapshot();
	FInputSystemSnapshot MakeSnapshot() const;
	void RefreshSnapshot();
	void SetUseRawMouse(bool bEnable);
	bool IsUsingRawMouse() const { return DeviceManager.GetKeyboardMouseDevice().IsUsingRawMouse(); }
	void AddRawMouseDelta(int DeltaX, int DeltaY);
	void ResetTransientState();
	void ResetAllKeyStates();
	void ResetMouseDelta();
	void ResetWheelDelta();
	void ResetCaptureStateForPIEEnd();
	bool IsWindowFocused() const { return DeviceManager.GetKeyboardMouseDevice().IsWindowFocused(); }

	bool GetAction(const FString& Name) const { return CurrentSnapshot.IsActionDown(Name); }
	bool GetActionDown(const FString& Name) const { return CurrentSnapshot.WasActionPressed(Name); }
	bool GetActionUp(const FString& Name) const { return CurrentSnapshot.WasActionReleased(Name); }
	float GetAxis(const FString& Name) const { return CurrentSnapshot.GetAxis(Name); }
	FString GetActionMappingDisplayName(const FString& Name);
	FString GetAxisMappingDisplayName(const FString& Name);
	FString GetCurrentInputDeviceName() const;
	void ClearInputMappings();
	void AddActionMapping(const FString& Name, const FString& KeyName);
	void AddActionMapping(const FString& Name, FInputKeyHandle Key);
	void AddAxisMapping(const FString& Name, const FString& KeyName, float Scale = 1.0f);
	void AddAxisMapping(const FString& Name, FInputKeyHandle Key, float Scale = 1.0f);
	void LoadMappingsFromProjectSettings();
	bool PlayRumble(float LowFrequency, float HighFrequency, uint32 DurationMs);
	bool PlayTriggerRumble(float Left, float Right, uint32 DurationMs);
	bool SetInputLightColor(uint8 Red, uint8 Green, uint8 Blue);
	int32 GetInputDeviceBattery() const;
	EInputDeviceClass GetPrimaryInputDevice() const { return DeviceManager.GetPrimaryGameplayDeviceClass(); }
	const FInputDeviceSnapshot& GetGamepadSnapshot() const { return DeviceManager.GetGamepadDevice().GetSnapshot(); }

	bool GetKeyDown(int VK) const { return DeviceManager.GetKeyboardMouseDevice().GetKeyDown(VK); }
	bool GetKey(int VK) const { return DeviceManager.GetKeyboardMouseDevice().GetKey(VK); }
	bool GetKeyUp(int VK) const { return DeviceManager.GetKeyboardMouseDevice().GetKeyUp(VK); }

	POINT GetMousePos() const { return DeviceManager.GetKeyboardMouseDevice().GetMousePos(); }
	POINT GetMouseClientPos() const { return DeviceManager.GetKeyboardMouseDevice().GetMouseClientPos(); }
	POINT GetMouseClientSize() const { return DeviceManager.GetKeyboardMouseDevice().GetMouseClientSize(); }
	int MouseDeltaX() const { return DeviceManager.GetKeyboardMouseDevice().MouseDeltaX(); }
	int MouseDeltaY() const { return DeviceManager.GetKeyboardMouseDevice().MouseDeltaY(); }
	bool MouseMoved() const { return MouseDeltaX() != 0 || MouseDeltaY() != 0; }

	bool IsDraggingLeft() const { return DeviceManager.GetKeyboardMouseDevice().IsDraggingLeft(); }
	bool GetLeftDragStart() const { return DeviceManager.GetKeyboardMouseDevice().GetLeftDragStart(); }
	bool GetLeftDragging() const { return DeviceManager.GetKeyboardMouseDevice().GetLeftDragging(); }
	bool GetLeftDragEnd() const { return DeviceManager.GetKeyboardMouseDevice().GetLeftDragEnd(); }
	POINT GetLeftDragVector() const { return DeviceManager.GetKeyboardMouseDevice().GetLeftDragVector(); }
	float GetLeftDragDistance() const { return DeviceManager.GetKeyboardMouseDevice().GetLeftDragDistance(); }

	bool IsDraggingRight() const { return DeviceManager.GetKeyboardMouseDevice().IsDraggingRight(); }
	bool GetRightDragStart() const { return DeviceManager.GetKeyboardMouseDevice().GetRightDragStart(); }
	bool GetRightDragging() const { return DeviceManager.GetKeyboardMouseDevice().GetRightDragging(); }
	bool GetRightDragEnd() const { return DeviceManager.GetKeyboardMouseDevice().GetRightDragEnd(); }
	POINT GetRightDragVector() const { return DeviceManager.GetKeyboardMouseDevice().GetRightDragVector(); }
	float GetRightDragDistance() const { return DeviceManager.GetKeyboardMouseDevice().GetRightDragDistance(); }

	void AddScrollDelta(int Delta) { DeviceManager.GetKeyboardMouseDevice().AddScrollDelta(Delta); }
	int GetScrollDelta() const { return DeviceManager.GetKeyboardMouseDevice().GetScrollDelta(); }
	bool ScrolledUp() const { return DeviceManager.GetKeyboardMouseDevice().ScrolledUp(); }
	bool ScrolledDown() const { return DeviceManager.GetKeyboardMouseDevice().ScrolledDown(); }
	float GetScrollNotches() const { return DeviceManager.GetKeyboardMouseDevice().GetScrollNotches(); }

	void SetOwnerWindow(HWND InHWnd) { DeviceManager.GetKeyboardMouseDevice().SetOwnerWindow(InHWnd); }

	FGuiInputState& GetGuiInputState() { return GuiState; }
	const FGuiInputState& GetGuiInputState() const { return GuiState; }
	void SetGuiMouseCapture(bool bCapture) { GuiState.bUsingMouse = bCapture; }
	void SetGuiKeyboardCapture(bool bCapture) { GuiState.bUsingKeyboard = bCapture; }
	void SetGuiTextInputCapture(bool bCapture) { GuiState.bUsingTextInput = bCapture; }
	bool IsGuiUsingMouse() const { return GuiState.bUsingMouse; }
	bool IsGuiUsingKeyboard() const { return GuiState.bUsingKeyboard; }
	bool IsGuiUsingTextInput() const { return GuiState.bUsingTextInput; }

private:
	FInputDeviceManager DeviceManager;
	FGuiInputState GuiState{};
	FInputSystemSnapshot CurrentSnapshot{};
	TArray<FInputMapping> ActionMappings;
	TArray<FInputMapping> AxisMappings;
	bool bDevicesInitialized = false;
	bool bInputMappingsInitialized = false;

	FInputActionState EvaluateActionHandle(const FInputKeyHandle& Key) const;
	float EvaluateAxisHandle(const FInputKeyHandle& Key) const;
	EInputDeviceClass GetHandleDeviceClass(const FInputKeyHandle& Key) const;
	FString GetMappingDisplayName(const FString& Name, const TArray<FInputMapping>& Mappings) const;
	FString GetInputKeyDisplayName(const FInputKeyHandle& Key) const;
	void EnsureDefaultInputMappings();
	void UpdateCurrentSnapshot();
};
