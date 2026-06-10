#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"

enum class EInputDeviceClass : uint8
{
	KeyboardMouse,
	Gamepad,
};

enum class EInputKeyKind : uint8
{
	None,
	Keyboard,
	MouseButton,
	MouseAxis,
	GamepadButton,
	GamepadAxis,
};

enum class EMouseAxis : uint8
{
	X,
	Y,
	Wheel,
};

enum class EGamepadButton : uint8
{
	FaceDown,
	FaceRight,
	FaceLeft,
	FaceUp,
	Back,
	Guide,
	Start,
	LeftStick,
	RightStick,
	LeftShoulder,
	RightShoulder,
	DPadUp,
	DPadDown,
	DPadLeft,
	DPadRight,
	Misc1,
	RightPaddle1,
	LeftPaddle1,
	RightPaddle2,
	LeftPaddle2,
	Touchpad,
	Count
};

enum class EGamepadAxis : uint8
{
	LeftStickX,
	LeftStickY,
	RightStickX,
	RightStickY,
	LeftTrigger,
	RightTrigger,
	Count
};

struct FInputKeyHandle
{
	EInputKeyKind Kind = EInputKeyKind::None;
	int32 KeyCode = 0;

	bool IsValid() const { return Kind != EInputKeyKind::None; }
	bool IsButton() const
	{
		return Kind == EInputKeyKind::Keyboard ||
			Kind == EInputKeyKind::MouseButton ||
			Kind == EInputKeyKind::GamepadButton;
	}
	bool IsAxis() const
	{
		return Kind == EInputKeyKind::MouseAxis ||
			Kind == EInputKeyKind::GamepadAxis;
	}
};

struct FInputActionState
{
	bool bDown = false;
	bool bPressed = false;
	bool bReleased = false;
};

struct FInputAxisState
{
	float Value = 0.0f;
};

struct FInputTouchFingerState
{
	bool bDown = false;
	float X = 0.0f;
	float Y = 0.0f;
	float Pressure = 0.0f;
};

struct FInputDeviceCapabilities
{
	bool bHasButtons = false;
	bool bHasAxes = false;
	bool bHasMouse = false;
	bool bHasTouchpad = false;
	bool bHasGyro = false;
	bool bHasAccel = false;
	bool bCanRumble = false;
	bool bCanTriggerRumble = false;
	bool bCanSetLightColor = false;
	bool bHasBattery = false;
};

struct FInputDeviceInfo
{
	int32 DeviceId = 0;
	EInputDeviceClass DeviceClass = EInputDeviceClass::KeyboardMouse;
	FString Name;
	bool bConnected = false;
};

struct FInputDeviceSnapshot
{
	FInputDeviceInfo Info;
	FInputDeviceCapabilities Capabilities;

	bool Buttons[static_cast<int32>(EGamepadButton::Count)] = {};
	bool PrevButtons[static_cast<int32>(EGamepadButton::Count)] = {};
	float Axes[static_cast<int32>(EGamepadAxis::Count)] = {};
	float PrevAxes[static_cast<int32>(EGamepadAxis::Count)] = {};

	FInputTouchFingerState TouchFingers[4] = {};
	FVector Gyro = FVector::ZeroVector;
	FVector Accel = FVector::ZeroVector;
	int32 BatteryPercent = -1;
	bool bHadInputThisFrame = false;
};

struct FInputMapping
{
	FString Name;
	FInputKeyHandle Key;
	float Scale = 1.0f;
};
