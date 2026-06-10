#pragma once

#include <windows.h>

#include "Core/Types/CoreTypes.h"
#include "Input/InputTypes.h"

class IInputDevice
{
public:
	virtual ~IInputDevice() = default;

	virtual bool Initialize() = 0;
	virtual void Shutdown() = 0;
	virtual void Tick() = 0;
	virtual const FInputDeviceSnapshot& GetSnapshot() const = 0;
	virtual FInputDeviceCapabilities GetCapabilities() const = 0;
};

class FKeyboardMouseInputDevice : public IInputDevice
{
public:
	bool Initialize() override;
	void Shutdown() override;
	void Tick() override;
	const FInputDeviceSnapshot& GetSnapshot() const override { return Snapshot; }
	FInputDeviceCapabilities GetCapabilities() const override { return Snapshot.Capabilities; }

	void SetOwnerWindow(HWND InHWnd) { OwnerHWnd = InHWnd; }
	HWND GetOwnerWindow() const { return OwnerHWnd; }
	bool IsWindowFocused() const { return bWindowFocused; }

	void SetUseRawMouse(bool bEnable);
	bool IsUsingRawMouse() const { return bUseRawMouse; }
	void AddRawMouseDelta(int DeltaX, int DeltaY);
	void AddScrollDelta(int Delta) { ScrollDelta += Delta; }
	void ResetTransientState();
	void ResetAllKeyStates();
	void ResetMouseDelta();
	void ResetWheelDelta();
	void ResetCaptureStateForPIEEnd();

	bool GetKeyDown(int VK) const { return IsValidVK(VK) && CurrentStates[VK] && !PrevStates[VK]; }
	bool GetKey(int VK) const { return IsValidVK(VK) && CurrentStates[VK]; }
	bool GetKeyUp(int VK) const { return IsValidVK(VK) && !CurrentStates[VK] && PrevStates[VK]; }
	bool WasKeyDown(int VK) const { return IsValidVK(VK) && PrevStates[VK]; }

	POINT GetMousePos() const { return MousePos; }
	POINT GetMouseClientPos() const;
	POINT GetMouseClientSize() const;
	int MouseDeltaX() const { return FrameMouseDeltaX; }
	int MouseDeltaY() const { return FrameMouseDeltaY; }
	bool MouseMoved() const { return MouseDeltaX() != 0 || MouseDeltaY() != 0; }

	bool IsDraggingLeft() const { return GetKey(VK_LBUTTON) && MouseMoved(); }
	bool GetLeftDragStart() const { return bLeftDragJustStarted; }
	bool GetLeftDragging() const { return bLeftDragging; }
	bool GetLeftDragEnd() const { return bLeftDragJustEnded; }
	POINT GetLeftDragVector() const;
	float GetLeftDragDistance() const;

	bool IsDraggingRight() const { return GetKey(VK_RBUTTON) && MouseMoved(); }
	bool GetRightDragStart() const { return bRightDragJustStarted; }
	bool GetRightDragging() const { return bRightDragging; }
	bool GetRightDragEnd() const { return bRightDragJustEnded; }
	POINT GetRightDragVector() const;
	float GetRightDragDistance() const;

	int GetScrollDelta() const { return PrevScrollDelta; }
	bool ScrolledUp() const { return PrevScrollDelta > 0; }
	bool ScrolledDown() const { return PrevScrollDelta < 0; }
	float GetScrollNotches() const { return PrevScrollDelta / (float)WHEEL_DELTA; }

private:
	bool CurrentStates[256] = {};
	bool PrevStates[256] = {};

	POINT MousePos = { 0, 0 };
	POINT PrevMousePos = { 0, 0 };
	int FrameMouseDeltaX = 0;
	int FrameMouseDeltaY = 0;
	int RawMouseDeltaAccumX = 0;
	int RawMouseDeltaAccumY = 0;
	bool bUseRawMouse = false;

	bool bLeftDragCandidate = false;
	bool bRightDragCandidate = false;
	bool bLeftDragging = false;
	bool bRightDragging = false;

	bool bLeftDragJustStarted = false;
	bool bRightDragJustStarted = false;
	bool bLeftDragJustEnded = false;
	bool bRightDragJustEnded = false;

	POINT LeftDragStartPos = { 0, 0 };
	POINT LeftMouseDownPos = { 0, 0 };
	POINT RightDragStartPos = { 0, 0 };
	POINT RightMouseDownPos = { 0, 0 };

	int ScrollDelta = 0;
	int PrevScrollDelta = 0;
	HWND OwnerHWnd = nullptr;
	bool bWindowFocused = true;

	FInputDeviceSnapshot Snapshot{};

	static constexpr int DRAG_THRESHOLD = 5;

	static bool IsValidVK(int VK) { return VK >= 0 && VK < 256; }
	void FilterDragThreshold(bool& bCandidate, bool& bDragging, bool& bJustStarted, const POINT& MouseDownPos, POINT& DragStartPos);
	void ResetDragState();
	void UpdateSnapshot();
};

class FSDLGamepadInputDevice : public IInputDevice
{
public:
	bool Initialize() override;
	void Shutdown() override;
	void Tick() override;
	const FInputDeviceSnapshot& GetSnapshot() const override { return Snapshot; }
	FInputDeviceCapabilities GetCapabilities() const override { return Snapshot.Capabilities; }

	bool IsConnected() const { return Snapshot.Info.bConnected; }
	bool PlayRumble(float LowFrequency, float HighFrequency, uint32 DurationMs);
	bool PlayTriggerRumble(float Left, float Right, uint32 DurationMs);
	bool SetLightColor(uint8 Red, uint8 Green, uint8 Blue);

private:
	void* Gamepad = nullptr;
	int32 GamepadId = 0;
	bool bSDLInitialized = false;
	FInputDeviceSnapshot Snapshot{};

	void OpenFirstAvailableGamepad();
	void CloseGamepad();
	void UpdateCapabilities();
	void UpdateButtonsAndAxes();
	void UpdateTouchpad();
	void UpdateSensors();
	void UpdateBattery();
};

class FInputDeviceManager
{
public:
	bool Initialize();
	void Shutdown();
	void Tick();

	FKeyboardMouseInputDevice& GetKeyboardMouseDevice() { return KeyboardMouseDevice; }
	const FKeyboardMouseInputDevice& GetKeyboardMouseDevice() const { return KeyboardMouseDevice; }
	FSDLGamepadInputDevice& GetGamepadDevice() { return GamepadDevice; }
	const FSDLGamepadInputDevice& GetGamepadDevice() const { return GamepadDevice; }

	EInputDeviceClass GetPrimaryGameplayDeviceClass() const { return PrimaryGameplayDeviceClass; }
	const FInputDeviceSnapshot& GetPrimaryGamepadSnapshot() const { return GamepadDevice.GetSnapshot(); }
	bool PlayRumble(float LowFrequency, float HighFrequency, uint32 DurationMs);
	bool PlayTriggerRumble(float Left, float Right, uint32 DurationMs);
	bool SetInputLightColor(uint8 Red, uint8 Green, uint8 Blue);
	int32 GetInputDeviceBattery() const;

private:
	FKeyboardMouseInputDevice KeyboardMouseDevice;
	FSDLGamepadInputDevice GamepadDevice;
	EInputDeviceClass PrimaryGameplayDeviceClass = EInputDeviceClass::KeyboardMouse;
};
