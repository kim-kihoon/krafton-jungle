#include "Engine/Input/InputDevice.h"

#include <algorithm>
#include <cmath>

#include <SDL3/SDL.h>

namespace
{
	constexpr float GAMEPAD_AXIS_DENOMINATOR = 32767.0f;
	constexpr float GAMEPAD_AXIS_DEADZONE = 0.08f;

	SDL_GamepadButton ToSDLButton(EGamepadButton Button)
	{
		switch (Button)
		{
		case EGamepadButton::FaceDown: return SDL_GAMEPAD_BUTTON_SOUTH;
		case EGamepadButton::FaceRight: return SDL_GAMEPAD_BUTTON_EAST;
		case EGamepadButton::FaceLeft: return SDL_GAMEPAD_BUTTON_WEST;
		case EGamepadButton::FaceUp: return SDL_GAMEPAD_BUTTON_NORTH;
		case EGamepadButton::Back: return SDL_GAMEPAD_BUTTON_BACK;
		case EGamepadButton::Guide: return SDL_GAMEPAD_BUTTON_GUIDE;
		case EGamepadButton::Start: return SDL_GAMEPAD_BUTTON_START;
		case EGamepadButton::LeftStick: return SDL_GAMEPAD_BUTTON_LEFT_STICK;
		case EGamepadButton::RightStick: return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
		case EGamepadButton::LeftShoulder: return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
		case EGamepadButton::RightShoulder: return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
		case EGamepadButton::DPadUp: return SDL_GAMEPAD_BUTTON_DPAD_UP;
		case EGamepadButton::DPadDown: return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
		case EGamepadButton::DPadLeft: return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
		case EGamepadButton::DPadRight: return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
		case EGamepadButton::Misc1: return SDL_GAMEPAD_BUTTON_MISC1;
		case EGamepadButton::RightPaddle1: return SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1;
		case EGamepadButton::LeftPaddle1: return SDL_GAMEPAD_BUTTON_LEFT_PADDLE1;
		case EGamepadButton::RightPaddle2: return SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2;
		case EGamepadButton::LeftPaddle2: return SDL_GAMEPAD_BUTTON_LEFT_PADDLE2;
		case EGamepadButton::Touchpad: return SDL_GAMEPAD_BUTTON_TOUCHPAD;
		default: return SDL_GAMEPAD_BUTTON_INVALID;
		}
	}

	SDL_GamepadAxis ToSDLAxis(EGamepadAxis Axis)
	{
		switch (Axis)
		{
		case EGamepadAxis::LeftStickX: return SDL_GAMEPAD_AXIS_LEFTX;
		case EGamepadAxis::LeftStickY: return SDL_GAMEPAD_AXIS_LEFTY;
		case EGamepadAxis::RightStickX: return SDL_GAMEPAD_AXIS_RIGHTX;
		case EGamepadAxis::RightStickY: return SDL_GAMEPAD_AXIS_RIGHTY;
		case EGamepadAxis::LeftTrigger: return SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
		case EGamepadAxis::RightTrigger: return SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
		default: return SDL_GAMEPAD_AXIS_INVALID;
		}
	}

	float NormalizeAxis(Sint16 Value)
	{
		const float Normalized = std::clamp(Value / GAMEPAD_AXIS_DENOMINATOR, -1.0f, 1.0f);
		return std::abs(Normalized) < GAMEPAD_AXIS_DEADZONE ? 0.0f : Normalized;
	}

	uint16 NormalizeRumble(float Value)
	{
		const float Clamped = std::clamp(Value, 0.0f, 1.0f);
		return static_cast<uint16>(Clamped * 65535.0f);
	}

	SDL_Gamepad* CastGamepad(void* Gamepad)
	{
		return static_cast<SDL_Gamepad*>(Gamepad);
	}
}

bool FKeyboardMouseInputDevice::Initialize()
{
	Snapshot.Info.DeviceClass = EInputDeviceClass::KeyboardMouse;
	Snapshot.Info.Name = "KeyboardMouse";
	Snapshot.Info.bConnected = true;
	Snapshot.Capabilities.bHasButtons = true;
	Snapshot.Capabilities.bHasAxes = true;
	Snapshot.Capabilities.bHasMouse = true;
	ResetMouseDelta();
	UpdateSnapshot();
	return true;
}

void FKeyboardMouseInputDevice::Shutdown()
{
	ResetAllKeyStates();
	ResetTransientState();
}

void FKeyboardMouseInputDevice::Tick()
{
	bWindowFocused = !OwnerHWnd || GetForegroundWindow() == OwnerHWnd;
	if (!bWindowFocused)
	{
		ResetAllKeyStates();
		ResetTransientState();
		UpdateSnapshot();
		return;
	}

	for (int VK = 0; VK < 256; ++VK)
	{
		PrevStates[VK] = CurrentStates[VK];
		CurrentStates[VK] = (GetAsyncKeyState(VK) & 0x8000) != 0;
	}

	bLeftDragJustStarted = false;
	bRightDragJustStarted = false;
	bLeftDragJustEnded = false;
	bRightDragJustEnded = false;

	PrevScrollDelta = ScrollDelta;
	ScrollDelta = 0;

	PrevMousePos = MousePos;
	GetCursorPos(&MousePos);
	FrameMouseDeltaX = MousePos.x - PrevMousePos.x;
	FrameMouseDeltaY = MousePos.y - PrevMousePos.y;
	if (bUseRawMouse)
	{
		FrameMouseDeltaX = RawMouseDeltaAccumX;
		FrameMouseDeltaY = RawMouseDeltaAccumY;
	}
	RawMouseDeltaAccumX = 0;
	RawMouseDeltaAccumY = 0;

	if (GetKeyDown(VK_LBUTTON))
	{
		bLeftDragCandidate = true;
		LeftMouseDownPos = MousePos;
	}
	if (GetKeyDown(VK_RBUTTON))
	{
		bRightDragCandidate = true;
		RightMouseDownPos = MousePos;
	}

	if (!bLeftDragging && IsDraggingLeft())
	{
		FilterDragThreshold(bLeftDragCandidate, bLeftDragging, bLeftDragJustStarted, LeftMouseDownPos, LeftDragStartPos);
	}
	else if (GetKeyUp(VK_LBUTTON))
	{
		if (bLeftDragging)
		{
			bLeftDragJustEnded = true;
		}
		bLeftDragging = false;
		bLeftDragCandidate = false;
	}

	if (!bRightDragging && IsDraggingRight())
	{
		FilterDragThreshold(bRightDragCandidate, bRightDragging, bRightDragJustStarted, RightMouseDownPos, RightDragStartPos);
	}
	else if (GetKeyUp(VK_RBUTTON))
	{
		if (bRightDragging)
		{
			bRightDragJustEnded = true;
		}
		bRightDragging = false;
		bRightDragCandidate = false;
	}

	UpdateSnapshot();
}

void FKeyboardMouseInputDevice::SetUseRawMouse(bool bEnable)
{
	if (bUseRawMouse == bEnable)
	{
		return;
	}
	bUseRawMouse = bEnable;
	ResetMouseDelta();
}

void FKeyboardMouseInputDevice::AddRawMouseDelta(int DeltaX, int DeltaY)
{
	RawMouseDeltaAccumX += DeltaX;
	RawMouseDeltaAccumY += DeltaY;
}

void FKeyboardMouseInputDevice::ResetTransientState()
{
	bLeftDragJustStarted = false;
	bRightDragJustStarted = false;
	bLeftDragJustEnded = false;
	bRightDragJustEnded = false;
	ResetDragState();
	ResetMouseDelta();
	ResetWheelDelta();
	UpdateSnapshot();
}

void FKeyboardMouseInputDevice::ResetAllKeyStates()
{
	for (int VK = 0; VK < 256; ++VK)
	{
		CurrentStates[VK] = false;
		PrevStates[VK] = false;
	}
	UpdateSnapshot();
}

void FKeyboardMouseInputDevice::ResetMouseDelta()
{
	GetCursorPos(&MousePos);
	PrevMousePos = MousePos;
	FrameMouseDeltaX = 0;
	FrameMouseDeltaY = 0;
	RawMouseDeltaAccumX = 0;
	RawMouseDeltaAccumY = 0;
	UpdateSnapshot();
}

void FKeyboardMouseInputDevice::ResetWheelDelta()
{
	ScrollDelta = 0;
	PrevScrollDelta = 0;
	UpdateSnapshot();
}

void FKeyboardMouseInputDevice::ResetCaptureStateForPIEEnd()
{
	SetUseRawMouse(false);
	ResetAllKeyStates();
	ResetTransientState();
	UpdateSnapshot();
}

POINT FKeyboardMouseInputDevice::GetMouseClientPos() const
{
	POINT ClientPos = MousePos;
	if (OwnerHWnd)
	{
		ScreenToClient(OwnerHWnd, &ClientPos);
	}
	return ClientPos;
}

POINT FKeyboardMouseInputDevice::GetMouseClientSize() const
{
	RECT ClientRect = {};
	if (OwnerHWnd && GetClientRect(OwnerHWnd, &ClientRect))
	{
		return POINT{ ClientRect.right - ClientRect.left, ClientRect.bottom - ClientRect.top };
	}
	return POINT{ 0, 0 };
}

POINT FKeyboardMouseInputDevice::GetLeftDragVector() const
{
	POINT V;
	V.x = MousePos.x - LeftDragStartPos.x;
	V.y = MousePos.y - LeftDragStartPos.y;
	return V;
}

float FKeyboardMouseInputDevice::GetLeftDragDistance() const
{
	const POINT V = GetLeftDragVector();
	return std::sqrt(static_cast<float>(V.x * V.x + V.y * V.y));
}

POINT FKeyboardMouseInputDevice::GetRightDragVector() const
{
	POINT V;
	V.x = MousePos.x - RightDragStartPos.x;
	V.y = MousePos.y - RightDragStartPos.y;
	return V;
}

float FKeyboardMouseInputDevice::GetRightDragDistance() const
{
	const POINT V = GetRightDragVector();
	return std::sqrt(static_cast<float>(V.x * V.x + V.y * V.y));
}

void FKeyboardMouseInputDevice::FilterDragThreshold(bool& bCandidate, bool& bDragging, bool& bJustStarted, const POINT& MouseDownPos, POINT& DragStartPos)
{
	if (bCandidate && !bDragging)
	{
		const int DX = MousePos.x - MouseDownPos.x;
		const int DY = MousePos.y - MouseDownPos.y;
		const int DistSq = DX * DX + DY * DY;
		if (DistSq >= DRAG_THRESHOLD * DRAG_THRESHOLD)
		{
			bJustStarted = true;
			bDragging = true;
			DragStartPos = MouseDownPos;
		}
	}
}

void FKeyboardMouseInputDevice::ResetDragState()
{
	bLeftDragCandidate = false;
	bRightDragCandidate = false;
	bLeftDragging = false;
	bRightDragging = false;
	bLeftDragJustStarted = false;
	bRightDragJustStarted = false;
	bLeftDragJustEnded = false;
	bRightDragJustEnded = false;
	LeftDragStartPos = MousePos;
	LeftMouseDownPos = MousePos;
	RightDragStartPos = MousePos;
	RightMouseDownPos = MousePos;
}

void FKeyboardMouseInputDevice::UpdateSnapshot()
{
	Snapshot.Info.DeviceClass = EInputDeviceClass::KeyboardMouse;
	Snapshot.Info.Name = "KeyboardMouse";
	Snapshot.Info.bConnected = true;
	Snapshot.bHadInputThisFrame = false;
	for (int VK = 0; VK < 256; ++VK)
	{
		if (CurrentStates[VK] != PrevStates[VK])
		{
			Snapshot.bHadInputThisFrame = true;
			break;
		}
	}
	if (FrameMouseDeltaX != 0 || FrameMouseDeltaY != 0 || PrevScrollDelta != 0)
	{
		Snapshot.bHadInputThisFrame = true;
	}
}

bool FSDLGamepadInputDevice::Initialize()
{
	Snapshot.Info.DeviceClass = EInputDeviceClass::Gamepad;
	Snapshot.Info.Name = "SDLGamepad";
	Snapshot.Info.bConnected = false;

	bSDLInitialized = SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_SENSOR | SDL_INIT_EVENTS);
	if (!bSDLInitialized)
	{
		return false;
	}

	OpenFirstAvailableGamepad();
	return true;
}

void FSDLGamepadInputDevice::Shutdown()
{
	if (Gamepad)
	{
		SDL_RumbleGamepad(CastGamepad(Gamepad), 0, 0, 0);
		SDL_RumbleGamepadTriggers(CastGamepad(Gamepad), 0, 0, 0);
	}
	CloseGamepad();
	if (bSDLInitialized)
	{
		SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_SENSOR | SDL_INIT_EVENTS);
		bSDLInitialized = false;
	}
}

void FSDLGamepadInputDevice::Tick()
{
	if (!bSDLInitialized)
	{
		return;
	}

	if (!Gamepad || !SDL_GamepadConnected(CastGamepad(Gamepad)))
	{
		CloseGamepad();
		OpenFirstAvailableGamepad();
	}

	Snapshot.bHadInputThisFrame = false;
	if (!Gamepad)
	{
		return;
	}

	SDL_UpdateGamepads();
	UpdateButtonsAndAxes();
	UpdateTouchpad();
	UpdateSensors();
	UpdateBattery();
}

bool FSDLGamepadInputDevice::PlayRumble(float LowFrequency, float HighFrequency, uint32 DurationMs)
{
	if (!Gamepad)
	{
		return false;
	}
	return SDL_RumbleGamepad(CastGamepad(Gamepad), NormalizeRumble(LowFrequency), NormalizeRumble(HighFrequency), DurationMs);
}

bool FSDLGamepadInputDevice::PlayTriggerRumble(float Left, float Right, uint32 DurationMs)
{
	if (!Gamepad)
	{
		return false;
	}
	return SDL_RumbleGamepadTriggers(CastGamepad(Gamepad), NormalizeRumble(Left), NormalizeRumble(Right), DurationMs);
}

bool FSDLGamepadInputDevice::SetLightColor(uint8 Red, uint8 Green, uint8 Blue)
{
	if (!Gamepad)
	{
		return false;
	}
	return SDL_SetGamepadLED(CastGamepad(Gamepad), Red, Green, Blue);
}

void FSDLGamepadInputDevice::OpenFirstAvailableGamepad()
{
	int Count = 0;
	SDL_JoystickID* GamepadIds = SDL_GetGamepads(&Count);
	if (!GamepadIds)
	{
		return;
	}

	for (int Index = 0; Index < Count; ++Index)
	{
		SDL_Gamepad* OpenedGamepad = SDL_OpenGamepad(GamepadIds[Index]);
		if (OpenedGamepad)
		{
			Gamepad = OpenedGamepad;
			GamepadId = GamepadIds[Index];
			Snapshot.Info.DeviceId = GamepadId;
			const char* Name = SDL_GetGamepadName(OpenedGamepad);
			Snapshot.Info.Name = Name ? Name : "SDLGamepad";
			Snapshot.Info.DeviceClass = EInputDeviceClass::Gamepad;
			Snapshot.Info.bConnected = true;
			UpdateCapabilities();
			break;
		}
	}

	SDL_free(GamepadIds);
}

void FSDLGamepadInputDevice::CloseGamepad()
{
	if (Gamepad)
	{
		SDL_RumbleGamepad(CastGamepad(Gamepad), 0, 0, 0);
		SDL_RumbleGamepadTriggers(CastGamepad(Gamepad), 0, 0, 0);
		SDL_CloseGamepad(CastGamepad(Gamepad));
	}

	Gamepad = nullptr;
	GamepadId = 0;
	Snapshot = FInputDeviceSnapshot{};
	Snapshot.Info.DeviceClass = EInputDeviceClass::Gamepad;
	Snapshot.Info.Name = "SDLGamepad";
	Snapshot.Info.bConnected = false;
}

void FSDLGamepadInputDevice::UpdateCapabilities()
{
	Snapshot.Capabilities = FInputDeviceCapabilities{};
	Snapshot.Capabilities.bHasButtons = true;
	Snapshot.Capabilities.bHasAxes = true;
	Snapshot.Capabilities.bHasTouchpad = SDL_GetNumGamepadTouchpads(CastGamepad(Gamepad)) > 0;
	Snapshot.Capabilities.bHasGyro = SDL_GamepadHasSensor(CastGamepad(Gamepad), SDL_SENSOR_GYRO);
	Snapshot.Capabilities.bHasAccel = SDL_GamepadHasSensor(CastGamepad(Gamepad), SDL_SENSOR_ACCEL);
	Snapshot.Capabilities.bCanRumble = true;
	Snapshot.Capabilities.bCanTriggerRumble = true;
	Snapshot.Capabilities.bCanSetLightColor = true;
	Snapshot.Capabilities.bHasBattery = true;

	if (Snapshot.Capabilities.bHasGyro)
	{
		SDL_SetGamepadSensorEnabled(CastGamepad(Gamepad), SDL_SENSOR_GYRO, true);
	}
	if (Snapshot.Capabilities.bHasAccel)
	{
		SDL_SetGamepadSensorEnabled(CastGamepad(Gamepad), SDL_SENSOR_ACCEL, true);
	}
}

void FSDLGamepadInputDevice::UpdateButtonsAndAxes()
{
	for (int32 ButtonIndex = 0; ButtonIndex < static_cast<int32>(EGamepadButton::Count); ++ButtonIndex)
	{
		Snapshot.PrevButtons[ButtonIndex] = Snapshot.Buttons[ButtonIndex];
		const SDL_GamepadButton SDLButton = ToSDLButton(static_cast<EGamepadButton>(ButtonIndex));
		Snapshot.Buttons[ButtonIndex] = SDLButton != SDL_GAMEPAD_BUTTON_INVALID && SDL_GetGamepadButton(CastGamepad(Gamepad), SDLButton);
		if (Snapshot.Buttons[ButtonIndex] != Snapshot.PrevButtons[ButtonIndex])
		{
			Snapshot.bHadInputThisFrame = true;
		}
	}

	for (int32 AxisIndex = 0; AxisIndex < static_cast<int32>(EGamepadAxis::Count); ++AxisIndex)
	{
		const SDL_GamepadAxis SDLAxis = ToSDLAxis(static_cast<EGamepadAxis>(AxisIndex));
		const float PreviousValue = Snapshot.Axes[AxisIndex];
		Snapshot.PrevAxes[AxisIndex] = PreviousValue;
		Snapshot.Axes[AxisIndex] = SDLAxis != SDL_GAMEPAD_AXIS_INVALID ? NormalizeAxis(SDL_GetGamepadAxis(CastGamepad(Gamepad), SDLAxis)) : 0.0f;
		if (std::abs(Snapshot.Axes[AxisIndex]) > GAMEPAD_AXIS_DEADZONE || std::abs(Snapshot.Axes[AxisIndex] - PreviousValue) > 0.02f)
		{
			Snapshot.bHadInputThisFrame = true;
		}
	}
}

void FSDLGamepadInputDevice::UpdateTouchpad()
{
	for (FInputTouchFingerState& Finger : Snapshot.TouchFingers)
	{
		Finger = FInputTouchFingerState{};
	}

	if (!Snapshot.Capabilities.bHasTouchpad)
	{
		return;
	}

	const int FingerCount = (std::min)(SDL_GetNumGamepadTouchpadFingers(CastGamepad(Gamepad), 0), 4);
	for (int FingerIndex = 0; FingerIndex < FingerCount; ++FingerIndex)
	{
		bool bDown = false;
		float X = 0.0f;
		float Y = 0.0f;
		float Pressure = 0.0f;
		if (SDL_GetGamepadTouchpadFinger(CastGamepad(Gamepad), 0, FingerIndex, &bDown, &X, &Y, &Pressure))
		{
			Snapshot.TouchFingers[FingerIndex].bDown = bDown;
			Snapshot.TouchFingers[FingerIndex].X = X;
			Snapshot.TouchFingers[FingerIndex].Y = Y;
			Snapshot.TouchFingers[FingerIndex].Pressure = Pressure;
			if (bDown)
			{
				Snapshot.bHadInputThisFrame = true;
			}
		}
	}
}

void FSDLGamepadInputDevice::UpdateSensors()
{
	Snapshot.Gyro = FVector::ZeroVector;
	Snapshot.Accel = FVector::ZeroVector;

	float Data[3] = {};
	if (Snapshot.Capabilities.bHasGyro && SDL_GetGamepadSensorData(CastGamepad(Gamepad), SDL_SENSOR_GYRO, Data, 3))
	{
		Snapshot.Gyro = FVector(Data[0], Data[1], Data[2]);
	}

	if (Snapshot.Capabilities.bHasAccel && SDL_GetGamepadSensorData(CastGamepad(Gamepad), SDL_SENSOR_ACCEL, Data, 3))
	{
		Snapshot.Accel = FVector(Data[0], Data[1], Data[2]);
	}
}

void FSDLGamepadInputDevice::UpdateBattery()
{
	int Percent = -1;
	const SDL_PowerState PowerState = SDL_GetGamepadPowerInfo(CastGamepad(Gamepad), &Percent);
	Snapshot.BatteryPercent = PowerState == SDL_POWERSTATE_UNKNOWN ? -1 : Percent;
}

bool FInputDeviceManager::Initialize()
{
	const bool bKeyboardMouseInitialized = KeyboardMouseDevice.Initialize();
	GamepadDevice.Initialize();
	return bKeyboardMouseInitialized;
}

void FInputDeviceManager::Shutdown()
{
	GamepadDevice.Shutdown();
	KeyboardMouseDevice.Shutdown();
	PrimaryGameplayDeviceClass = EInputDeviceClass::KeyboardMouse;
}

void FInputDeviceManager::Tick()
{
	KeyboardMouseDevice.Tick();
	GamepadDevice.Tick();

	if (!GamepadDevice.IsConnected() && PrimaryGameplayDeviceClass == EInputDeviceClass::Gamepad)
	{
		PrimaryGameplayDeviceClass = EInputDeviceClass::KeyboardMouse;
		return;
	}

	if (GamepadDevice.GetSnapshot().bHadInputThisFrame)
	{
		PrimaryGameplayDeviceClass = EInputDeviceClass::Gamepad;
	}
	if (KeyboardMouseDevice.GetSnapshot().bHadInputThisFrame)
	{
		PrimaryGameplayDeviceClass = EInputDeviceClass::KeyboardMouse;
	}
}

bool FInputDeviceManager::PlayRumble(float LowFrequency, float HighFrequency, uint32 DurationMs)
{
	return PrimaryGameplayDeviceClass == EInputDeviceClass::Gamepad && GamepadDevice.PlayRumble(LowFrequency, HighFrequency, DurationMs);
}

bool FInputDeviceManager::PlayTriggerRumble(float Left, float Right, uint32 DurationMs)
{
	return PrimaryGameplayDeviceClass == EInputDeviceClass::Gamepad && GamepadDevice.PlayTriggerRumble(Left, Right, DurationMs);
}

bool FInputDeviceManager::SetInputLightColor(uint8 Red, uint8 Green, uint8 Blue)
{
	return PrimaryGameplayDeviceClass == EInputDeviceClass::Gamepad && GamepadDevice.SetLightColor(Red, Green, Blue);
}

int32 FInputDeviceManager::GetInputDeviceBattery() const
{
	return PrimaryGameplayDeviceClass == EInputDeviceClass::Gamepad ? GamepadDevice.GetSnapshot().BatteryPercent : -1;
}
