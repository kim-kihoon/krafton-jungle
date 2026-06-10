#include "Engine/Input/InputSystem.h"

#include <cmath>

#include "Engine/Input/InputKeyCodes.h"
#include "Core/ProjectSettings.h"

namespace
{
	constexpr float AXIS_EPSILON = 0.001f;

	bool IsMouseButtonVK(int VK)
	{
		return VK == VK_LBUTTON || VK == VK_RBUTTON || VK == VK_MBUTTON || VK == VK_XBUTTON1 || VK == VK_XBUTTON2;
	}

	bool ContainsDisplayName(const TArray<FString>& Names, const FString& Name)
	{
		for (const FString& ExistingName : Names)
		{
			if (ExistingName == Name)
			{
				return true;
			}
		}
		return false;
	}

	FString JoinDisplayNames(const TArray<FString>& Names)
	{
		FString Result;
		for (const FString& Name : Names)
		{
			if (!Result.empty())
			{
				Result += "/";
			}
			Result += Name;
		}
		return Result;
	}
}

bool FInputSystemSnapshot::IsActionDown(const FString& Name) const
{
	const auto It = Actions.find(Name);
	return It != Actions.end() && It->second.bDown;
}

bool FInputSystemSnapshot::WasActionPressed(const FString& Name) const
{
	const auto It = Actions.find(Name);
	return It != Actions.end() && It->second.bPressed;
}

bool FInputSystemSnapshot::WasActionReleased(const FString& Name) const
{
	const auto It = Actions.find(Name);
	return It != Actions.end() && It->second.bReleased;
}

float FInputSystemSnapshot::GetAxis(const FString& Name) const
{
	const auto It = Axes.find(Name);
	return It != Axes.end() ? It->second.Value : 0.0f;
}

void InputSystem::InitializeDevices()
{
	if (bDevicesInitialized)
	{
		return;
	}

	DeviceManager.Initialize();
	bDevicesInitialized = true;
	UpdateCurrentSnapshot();
}

void InputSystem::ShutdownDevices()
{
	if (!bDevicesInitialized)
	{
		return;
	}

	DeviceManager.Shutdown();
	bDevicesInitialized = false;
	UpdateCurrentSnapshot();
}

void InputSystem::Tick()
{
	InitializeDevices();
	EnsureDefaultInputMappings();
	DeviceManager.Tick();
	UpdateCurrentSnapshot();
}

FInputSystemSnapshot InputSystem::TickAndMakeSnapshot()
{
	Tick();
	return MakeSnapshot();
}

FInputSystemSnapshot InputSystem::MakeSnapshot() const
{
	return CurrentSnapshot;
}

void InputSystem::RefreshSnapshot()
{
	InitializeDevices();
	EnsureDefaultInputMappings();
	UpdateCurrentSnapshot();
}

void InputSystem::SetUseRawMouse(bool bEnable)
{
	DeviceManager.GetKeyboardMouseDevice().SetUseRawMouse(bEnable);
	UpdateCurrentSnapshot();
}

void InputSystem::AddRawMouseDelta(int DeltaX, int DeltaY)
{
	DeviceManager.GetKeyboardMouseDevice().AddRawMouseDelta(DeltaX, DeltaY);
}

void InputSystem::ResetTransientState()
{
	DeviceManager.GetKeyboardMouseDevice().ResetTransientState();
	UpdateCurrentSnapshot();
}

void InputSystem::ResetAllKeyStates()
{
	DeviceManager.GetKeyboardMouseDevice().ResetAllKeyStates();
	UpdateCurrentSnapshot();
}

void InputSystem::ResetMouseDelta()
{
	DeviceManager.GetKeyboardMouseDevice().ResetMouseDelta();
	UpdateCurrentSnapshot();
}

void InputSystem::ResetWheelDelta()
{
	DeviceManager.GetKeyboardMouseDevice().ResetWheelDelta();
	UpdateCurrentSnapshot();
}

void InputSystem::ResetCaptureStateForPIEEnd()
{
	SetUseRawMouse(false);
	ResetAllKeyStates();
	ResetTransientState();
	GuiState.bUsingMouse = false;
	GuiState.bUsingKeyboard = false;
	GuiState.bUsingTextInput = false;
	UpdateCurrentSnapshot();
}

void InputSystem::ClearInputMappings()
{
	ActionMappings.clear();
	AxisMappings.clear();
	bInputMappingsInitialized = true;
	UpdateCurrentSnapshot();
}

void InputSystem::AddActionMapping(const FString& Name, const FString& KeyName)
{
	AddActionMapping(Name, ResolveInputKeyHandle(KeyName));
}

void InputSystem::AddActionMapping(const FString& Name, FInputKeyHandle Key)
{
	if (!Name.empty() && (Key.IsButton() || Key.Kind == EInputKeyKind::GamepadAxis))
	{
		ActionMappings.push_back(FInputMapping{ Name, Key, 1.0f });
		bInputMappingsInitialized = true;
	}
}

void InputSystem::AddAxisMapping(const FString& Name, const FString& KeyName, float Scale)
{
	AddAxisMapping(Name, ResolveInputKeyHandle(KeyName), Scale);
}

void InputSystem::AddAxisMapping(const FString& Name, FInputKeyHandle Key, float Scale)
{
	if (!Name.empty() && Key.IsValid())
	{
		AxisMappings.push_back(FInputMapping{ Name, Key, Scale });
		bInputMappingsInitialized = true;
	}
}

void InputSystem::LoadMappingsFromProjectSettings()
{
	ClearInputMappings();

	const FProjectSettings& Settings = FProjectSettings::Get();
	for (const FInputBindingSetting& Mapping : Settings.Input.ActionMappings)
	{
		AddActionMapping(Mapping.Name, Mapping.Key);
	}
	for (const FInputBindingSetting& Mapping : Settings.Input.AxisMappings)
	{
		AddAxisMapping(Mapping.Name, Mapping.Key, Mapping.Scale);
	}

	if (ActionMappings.empty() && AxisMappings.empty())
	{
		bInputMappingsInitialized = false;
		EnsureDefaultInputMappings();
	}

	UpdateCurrentSnapshot();
}

bool InputSystem::PlayRumble(float LowFrequency, float HighFrequency, uint32 DurationMs)
{
	return DeviceManager.PlayRumble(LowFrequency, HighFrequency, DurationMs);
}

bool InputSystem::PlayTriggerRumble(float Left, float Right, uint32 DurationMs)
{
	return DeviceManager.PlayTriggerRumble(Left, Right, DurationMs);
}

bool InputSystem::SetInputLightColor(uint8 Red, uint8 Green, uint8 Blue)
{
	return DeviceManager.SetInputLightColor(Red, Green, Blue);
}

int32 InputSystem::GetInputDeviceBattery() const
{
	return DeviceManager.GetInputDeviceBattery();
}

FString InputSystem::GetActionMappingDisplayName(const FString& Name)
{
	EnsureDefaultInputMappings();
	return GetMappingDisplayName(Name, ActionMappings);
}

FString InputSystem::GetAxisMappingDisplayName(const FString& Name)
{
	EnsureDefaultInputMappings();
	return GetMappingDisplayName(Name, AxisMappings);
}

FString InputSystem::GetCurrentInputDeviceName() const
{
	return DeviceManager.GetPrimaryGameplayDeviceClass() == EInputDeviceClass::Gamepad ? "Gamepad" : "KeyboardMouse";
}

FString InputSystem::GetMappingDisplayName(const FString& Name, const TArray<FInputMapping>& Mappings) const
{
	const EInputDeviceClass PrimaryDevice = DeviceManager.GetPrimaryGameplayDeviceClass();
	TArray<FString> PrimaryNames;
	TArray<FString> FallbackNames;

	for (const FInputMapping& Mapping : Mappings)
	{
		if (Mapping.Name != Name)
		{
			continue;
		}

		const FString DisplayName = GetInputKeyDisplayName(Mapping.Key);
		if (DisplayName.empty())
		{
			continue;
		}

		TArray<FString>& TargetNames = GetHandleDeviceClass(Mapping.Key) == PrimaryDevice ? PrimaryNames : FallbackNames;
		if (!ContainsDisplayName(TargetNames, DisplayName))
		{
			TargetNames.push_back(DisplayName);
		}
	}

	if (!PrimaryNames.empty())
	{
		return JoinDisplayNames(PrimaryNames);
	}
	return JoinDisplayNames(FallbackNames);
}

FString InputSystem::GetInputKeyDisplayName(const FInputKeyHandle& Key) const
{
	if (!Key.IsValid())
	{
		return "";
	}

	switch (Key.Kind)
	{
	case EInputKeyKind::Keyboard:
		return GetInputKeyName(Key.KeyCode);
	case EInputKeyKind::MouseButton:
		switch (Key.KeyCode)
		{
		case VK_LBUTTON: return "LMB";
		case VK_RBUTTON: return "RMB";
		case VK_MBUTTON: return "MMB";
		case VK_XBUTTON1: return "Mouse X1";
		case VK_XBUTTON2: return "Mouse X2";
		default: return GetInputKeyName(Key.KeyCode);
		}
	case EInputKeyKind::MouseAxis:
		switch (static_cast<EMouseAxis>(Key.KeyCode))
		{
		case EMouseAxis::X: return "Mouse X";
		case EMouseAxis::Y: return "Mouse Y";
		case EMouseAxis::Wheel: return "Mouse Wheel";
		default: return "";
		}
	case EInputKeyKind::GamepadButton:
		switch (static_cast<EGamepadButton>(Key.KeyCode))
		{
		case EGamepadButton::FaceDown: return "Cross";
		case EGamepadButton::FaceRight: return "Circle";
		case EGamepadButton::FaceLeft: return "Square";
		case EGamepadButton::FaceUp: return "Triangle";
		case EGamepadButton::Back: return "Share";
		case EGamepadButton::Guide: return "PS";
		case EGamepadButton::Start: return "Options";
		case EGamepadButton::LeftStick: return "L3";
		case EGamepadButton::RightStick: return "R3";
		case EGamepadButton::LeftShoulder: return "L1";
		case EGamepadButton::RightShoulder: return "R1";
		case EGamepadButton::DPadUp: return "D-Pad Up";
		case EGamepadButton::DPadDown: return "D-Pad Down";
		case EGamepadButton::DPadLeft: return "D-Pad Left";
		case EGamepadButton::DPadRight: return "D-Pad Right";
		case EGamepadButton::Misc1: return "Gamepad Misc";
		case EGamepadButton::RightPaddle1: return "Right Paddle 1";
		case EGamepadButton::LeftPaddle1: return "Left Paddle 1";
		case EGamepadButton::RightPaddle2: return "Right Paddle 2";
		case EGamepadButton::LeftPaddle2: return "Left Paddle 2";
		case EGamepadButton::Touchpad: return "Touchpad";
		default: return "";
		}
	case EInputKeyKind::GamepadAxis:
		switch (static_cast<EGamepadAxis>(Key.KeyCode))
		{
		case EGamepadAxis::LeftStickX:
		case EGamepadAxis::LeftStickY:
			return "Left Stick";
		case EGamepadAxis::RightStickX:
		case EGamepadAxis::RightStickY:
			return "Right Stick";
		case EGamepadAxis::LeftTrigger:
			return "L2";
		case EGamepadAxis::RightTrigger:
			return "R2";
		default:
			return "";
		}
	default:
		return "";
	}
}

FInputActionState InputSystem::EvaluateActionHandle(const FInputKeyHandle& Key) const
{
	FInputActionState State{};
	if (!Key.IsButton() && Key.Kind != EInputKeyKind::GamepadAxis)
	{
		return State;
	}

	if (Key.Kind == EInputKeyKind::Keyboard || Key.Kind == EInputKeyKind::MouseButton)
	{
		State.bDown = DeviceManager.GetKeyboardMouseDevice().GetKey(Key.KeyCode);
		State.bPressed = DeviceManager.GetKeyboardMouseDevice().GetKeyDown(Key.KeyCode);
		State.bReleased = DeviceManager.GetKeyboardMouseDevice().GetKeyUp(Key.KeyCode);
		return State;
	}

	if (Key.Kind == EInputKeyKind::GamepadButton)
	{
		const FInputDeviceSnapshot& Gamepad = DeviceManager.GetGamepadDevice().GetSnapshot();
		const int32 ButtonIndex = Key.KeyCode;
		if (ButtonIndex >= 0 && ButtonIndex < static_cast<int32>(EGamepadButton::Count))
		{
			State.bDown = Gamepad.Buttons[ButtonIndex];
			State.bPressed = Gamepad.Buttons[ButtonIndex] && !Gamepad.PrevButtons[ButtonIndex];
			State.bReleased = !Gamepad.Buttons[ButtonIndex] && Gamepad.PrevButtons[ButtonIndex];
		}
	}

	if (Key.Kind == EInputKeyKind::GamepadAxis)
	{
		constexpr float TriggerActionThreshold = 0.5f;
		const FInputDeviceSnapshot& Gamepad = DeviceManager.GetGamepadDevice().GetSnapshot();
		const int32 AxisIndex = Key.KeyCode;
		if (AxisIndex >= 0 && AxisIndex < static_cast<int32>(EGamepadAxis::Count))
		{
			const bool bDown = Gamepad.Axes[AxisIndex] >= TriggerActionThreshold;
			const bool bPrevDown = Gamepad.PrevAxes[AxisIndex] >= TriggerActionThreshold;
			State.bDown = bDown;
			State.bPressed = bDown && !bPrevDown;
			State.bReleased = !bDown && bPrevDown;
		}
	}

	return State;
}

float InputSystem::EvaluateAxisHandle(const FInputKeyHandle& Key) const
{
	if (!Key.IsValid())
	{
		return 0.0f;
	}

	switch (Key.Kind)
	{
	case EInputKeyKind::Keyboard:
	case EInputKeyKind::MouseButton:
		return DeviceManager.GetKeyboardMouseDevice().GetKey(Key.KeyCode) ? 1.0f : 0.0f;
	case EInputKeyKind::MouseAxis:
		switch (static_cast<EMouseAxis>(Key.KeyCode))
		{
		case EMouseAxis::X: return static_cast<float>(DeviceManager.GetKeyboardMouseDevice().MouseDeltaX());
		case EMouseAxis::Y: return static_cast<float>(DeviceManager.GetKeyboardMouseDevice().MouseDeltaY());
		case EMouseAxis::Wheel: return DeviceManager.GetKeyboardMouseDevice().GetScrollNotches();
		default: return 0.0f;
		}
	case EInputKeyKind::GamepadAxis:
	{
		const int32 AxisIndex = Key.KeyCode;
		const FInputDeviceSnapshot& Gamepad = DeviceManager.GetGamepadDevice().GetSnapshot();
		return AxisIndex >= 0 && AxisIndex < static_cast<int32>(EGamepadAxis::Count) ? Gamepad.Axes[AxisIndex] : 0.0f;
	}
	default:
		return 0.0f;
	}
}

EInputDeviceClass InputSystem::GetHandleDeviceClass(const FInputKeyHandle& Key) const
{
	if (Key.Kind == EInputKeyKind::GamepadButton || Key.Kind == EInputKeyKind::GamepadAxis)
	{
		return EInputDeviceClass::Gamepad;
	}
	return EInputDeviceClass::KeyboardMouse;
}

void InputSystem::EnsureDefaultInputMappings()
{
	if (bInputMappingsInitialized)
	{
		return;
	}

	bInputMappingsInitialized = true;
	ActionMappings.clear();
	AxisMappings.clear();

	AddAxisMapping("MoveForward", "W", 1.0f);
	AddAxisMapping("MoveForward", "S", -1.0f);
	AddAxisMapping("MoveForward", "Gamepad_LeftStickY", -1.0f);
	AddAxisMapping("MoveRight", "D", 1.0f);
	AddAxisMapping("MoveRight", "A", -1.0f);
	AddAxisMapping("MoveRight", "Gamepad_LeftStickX", 1.0f);
	AddAxisMapping("Turn", "MouseX", 0.1f);
	AddAxisMapping("Turn", "Gamepad_RightStickX", 2.0f);
	AddAxisMapping("LookUp", "MouseY", 0.1f);
	AddAxisMapping("LookUp", "Gamepad_RightStickY", 2.0f);
	AddAxisMapping("VehicleThrottle", "W", 1.0f);
	AddAxisMapping("VehicleThrottle", "Gamepad_RightTrigger", 1.0f);
	AddAxisMapping("VehicleBrake", "S", 1.0f);
	AddAxisMapping("VehicleBrake", "Gamepad_LeftTrigger", 1.0f);
	AddAxisMapping("VehicleSteering", "A", -1.0f);
	AddAxisMapping("VehicleSteering", "D", 1.0f);
	AddAxisMapping("VehicleSteering", "Gamepad_LeftStickX", 1.0f);

	AddActionMapping("Jump", "Space");
	AddActionMapping("Jump", "Gamepad_FaceDown");
	AddActionMapping("Interact", "E");
	AddActionMapping("Interact", "Gamepad_FaceRight");
	AddActionMapping("Aim", "RightMouseButton");
	AddActionMapping("Aim", "Gamepad_LeftShoulder");
	AddActionMapping("DebugAnomalyOutline", "L");
	AddActionMapping("DebugAnomalyOutline", "Gamepad_LeftTrigger");
	AddActionMapping("Fire", "LeftMouseButton");
	AddActionMapping("Fire", "Gamepad_RightShoulder");
	AddActionMapping("VehicleHandbrake", "Space");
	AddActionMapping("VehicleHandbrake", "Gamepad_FaceDown");
}

void InputSystem::UpdateCurrentSnapshot()
{
	FInputSystemSnapshot Snapshot{};
	for (int VK = 0; VK < 256; ++VK)
	{
		Snapshot.KeyDown[VK] = DeviceManager.GetKeyboardMouseDevice().GetKey(VK);
		Snapshot.KeyPressed[VK] = DeviceManager.GetKeyboardMouseDevice().GetKeyDown(VK);
		Snapshot.KeyReleased[VK] = DeviceManager.GetKeyboardMouseDevice().GetKeyUp(VK);
	}

	Snapshot.bLeftMouseDown = Snapshot.KeyDown[VK_LBUTTON];
	Snapshot.bLeftMousePressed = Snapshot.KeyPressed[VK_LBUTTON];
	Snapshot.bLeftMouseReleased = Snapshot.KeyReleased[VK_LBUTTON];
	Snapshot.bRightMouseDown = Snapshot.KeyDown[VK_RBUTTON];
	Snapshot.bRightMousePressed = Snapshot.KeyPressed[VK_RBUTTON];
	Snapshot.bRightMouseReleased = Snapshot.KeyReleased[VK_RBUTTON];
	Snapshot.bMiddleMouseDown = Snapshot.KeyDown[VK_MBUTTON];
	Snapshot.bMiddleMousePressed = Snapshot.KeyPressed[VK_MBUTTON];
	Snapshot.bMiddleMouseReleased = Snapshot.KeyReleased[VK_MBUTTON];
	Snapshot.bXButton1Down = Snapshot.KeyDown[VK_XBUTTON1];
	Snapshot.bXButton1Pressed = Snapshot.KeyPressed[VK_XBUTTON1];
	Snapshot.bXButton1Released = Snapshot.KeyReleased[VK_XBUTTON1];
	Snapshot.bXButton2Down = Snapshot.KeyDown[VK_XBUTTON2];
	Snapshot.bXButton2Pressed = Snapshot.KeyPressed[VK_XBUTTON2];
	Snapshot.bXButton2Released = Snapshot.KeyReleased[VK_XBUTTON2];

	Snapshot.MousePos = DeviceManager.GetKeyboardMouseDevice().GetMousePos();
	Snapshot.MouseDeltaX = DeviceManager.GetKeyboardMouseDevice().MouseDeltaX();
	Snapshot.MouseDeltaY = DeviceManager.GetKeyboardMouseDevice().MouseDeltaY();
	Snapshot.ScrollDelta = DeviceManager.GetKeyboardMouseDevice().GetScrollDelta();
	Snapshot.bLeftDragStarted = DeviceManager.GetKeyboardMouseDevice().GetLeftDragStart();
	Snapshot.bLeftDragging = DeviceManager.GetKeyboardMouseDevice().GetLeftDragging();
	Snapshot.bLeftDragEnded = DeviceManager.GetKeyboardMouseDevice().GetLeftDragEnd();
	Snapshot.LeftDragVector = DeviceManager.GetKeyboardMouseDevice().GetLeftDragVector();
	Snapshot.bRightDragStarted = DeviceManager.GetKeyboardMouseDevice().GetRightDragStart();
	Snapshot.bRightDragging = DeviceManager.GetKeyboardMouseDevice().GetRightDragging();
	Snapshot.bRightDragEnded = DeviceManager.GetKeyboardMouseDevice().GetRightDragEnd();
	Snapshot.RightDragVector = DeviceManager.GetKeyboardMouseDevice().GetRightDragVector();
	Snapshot.bUsingRawMouse = DeviceManager.GetKeyboardMouseDevice().IsUsingRawMouse();
	Snapshot.bGuiUsingMouse = GuiState.bUsingMouse;
	Snapshot.bGuiUsingKeyboard = GuiState.bUsingKeyboard;
	Snapshot.bGuiUsingTextInput = GuiState.bUsingTextInput;
	Snapshot.bWindowFocused = DeviceManager.GetKeyboardMouseDevice().IsWindowFocused();
	Snapshot.PrimaryInputDevice = DeviceManager.GetPrimaryGameplayDeviceClass();
	Snapshot.GamepadSnapshot = DeviceManager.GetGamepadDevice().GetSnapshot();

	for (const FInputMapping& Mapping : ActionMappings)
	{
		FInputActionState& State = Snapshot.Actions[Mapping.Name];
		const FInputActionState MappingState = EvaluateActionHandle(Mapping.Key);
		State.bDown = State.bDown || MappingState.bDown;
		State.bPressed = State.bPressed || MappingState.bPressed;
		State.bReleased = State.bReleased || MappingState.bReleased;
	}

	TMap<FString, float> PrimaryAxisValues;
	TMap<FString, float> FallbackAxisValues;
	for (const FInputMapping& Mapping : AxisMappings)
	{
		const float Value = EvaluateAxisHandle(Mapping.Key) * Mapping.Scale;
		if (GetHandleDeviceClass(Mapping.Key) == Snapshot.PrimaryInputDevice)
		{
			PrimaryAxisValues[Mapping.Name] += Value;
		}
		else
		{
			FallbackAxisValues[Mapping.Name] += Value;
		}
	}

	for (const FInputMapping& Mapping : AxisMappings)
	{
		FInputAxisState& State = Snapshot.Axes[Mapping.Name];
		const float PrimaryValue = PrimaryAxisValues[Mapping.Name];
		const float FallbackValue = FallbackAxisValues[Mapping.Name];
		State.Value = std::abs(PrimaryValue) > AXIS_EPSILON ? PrimaryValue : FallbackValue;
	}

	CurrentSnapshot = Snapshot;
}
