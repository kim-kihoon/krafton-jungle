#include "InputComponent.h"

#include "Core/Logging/Log.h"
#include "Input/InputSystem.h"
#include "Input/InputKeyCodes.h"
#include "Object/Reflection/ObjectFactory.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float AXIS_EPSILON = 0.001f;

	FInputKeyHandle MakeKeyHandleFromVK(int VKey)
	{
		FInputKeyHandle Handle;
		Handle.Kind = (VKey == VK_LBUTTON || VKey == VK_RBUTTON || VKey == VK_MBUTTON || VKey == VK_XBUTTON1 || VKey == VK_XBUTTON2)
			? EInputKeyKind::MouseButton
			: EInputKeyKind::Keyboard;
		Handle.KeyCode = VKey;
		return Handle;
	}

	FInputKeyHandle MakeMouseAxisHandle(EInputAxisSourceType Axis)
	{
		FInputKeyHandle Handle;
		Handle.Kind = EInputKeyKind::MouseAxis;
		switch (Axis)
		{
		case EInputAxisSourceType::MouseX:
			Handle.KeyCode = static_cast<int32>(EMouseAxis::X);
			break;
		case EInputAxisSourceType::MouseY:
			Handle.KeyCode = static_cast<int32>(EMouseAxis::Y);
			break;
		case EInputAxisSourceType::MouseWheel:
			Handle.KeyCode = static_cast<int32>(EMouseAxis::Wheel);
			break;
		default:
			Handle.Kind = EInputKeyKind::None;
			break;
		}
		return Handle;
	}
}

UInputComponent::UInputComponent()
{
	bTickEnable = false;
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.SetTickEnabled(false);
}

void UInputComponent::AddAxisMapping(const FString& Name, int VKey, float Scale)
{
	AddAxisMappingForOwner(nullptr, Name, VKey, Scale);
}

void UInputComponent::AddAxisMapping(const FString& Name, const FString& KeyName, float Scale)
{
	AddAxisMappingForOwner(nullptr, Name, KeyName, Scale);
}

void UInputComponent::AddAxisMappingForOwner(const void* OwnerKey, const FString& Name, const FString& KeyName, float Scale)
{
	FAxisMapping M;
	M.Name = Name;
	M.Key = ResolveInputKeyHandle(KeyName);
	M.Scale = Scale;
	M.OwnerKey = OwnerKey;
	if (M.Key.IsValid())
	{
		AxisMappings.push_back(std::move(M));
	}
}

void UInputComponent::AddAxisMappingForOwner(const void* OwnerKey, const FString& Name, int VKey, float Scale)
{
	FAxisMapping M;
	M.Name = Name;
	M.Key = MakeKeyHandleFromVK(VKey);
	M.Scale = Scale;
	M.OwnerKey = OwnerKey;
	AxisMappings.push_back(std::move(M));
}

void UInputComponent::AddMouseAxisMapping(const FString& Name, EInputAxisSourceType Axis, float Scale)
{
	AddMouseAxisMappingForOwner(nullptr, Name, Axis, Scale);
}

void UInputComponent::AddMouseAxisMappingForOwner(const void* OwnerKey, const FString& Name, EInputAxisSourceType Axis, float Scale)
{
	if (Axis == EInputAxisSourceType::Key)
	{
		return;
	}

	FAxisMapping M;
	M.Name = Name;
	M.Key = MakeMouseAxisHandle(Axis);
	M.Scale = Scale;
	M.OwnerKey = OwnerKey;
	if (M.Key.IsValid())
	{
		AxisMappings.push_back(std::move(M));
	}
}

void UInputComponent::AddActionMapping(const FString& Name, int VKey)
{
	AddActionMappingForOwner(nullptr, Name, VKey);
}

void UInputComponent::AddActionMapping(const FString& Name, const FString& KeyName)
{
	AddActionMappingForOwner(nullptr, Name, KeyName);
}

void UInputComponent::AddActionMappingForOwner(const void* OwnerKey, const FString& Name, const FString& KeyName)
{
	FActionMapping M;
	M.Name = Name;
	M.Key = ResolveInputKeyHandle(KeyName);
	M.OwnerKey = OwnerKey;
	if (M.Key.IsButton() || M.Key.Kind == EInputKeyKind::GamepadAxis)
	{
		ActionMappings.push_back(std::move(M));
	}
}

void UInputComponent::AddActionMappingForOwner(const void* OwnerKey, const FString& Name, int VKey)
{
	FActionMapping M;
	M.Name = Name;
	M.Key = MakeKeyHandleFromVK(VKey);
	M.OwnerKey = OwnerKey;
	ActionMappings.push_back(std::move(M));
}

void UInputComponent::BindAxis(const FString& Name, TFunction<void(float)> Callback)
{
	BindAxisForOwner(nullptr, Name, std::move(Callback));
}

void UInputComponent::BindAxisForOwner(const void* OwnerKey, const FString& Name, TFunction<void(float)> Callback)
{
	FAxisBinding B;
	B.Name = Name;
	B.OwnerKey = OwnerKey;
	B.Callback = std::move(Callback);
	AxisBindings.push_back(std::move(B));
}

void UInputComponent::BindAction(const FString& Name, EInputEvent Event, TFunction<void()> Callback)
{
	BindActionForOwner(nullptr, Name, Event, std::move(Callback));
}

void UInputComponent::BindActionForOwner(const void* OwnerKey, const FString& Name, EInputEvent Event, TFunction<void()> Callback)
{
	FActionBinding B;
	B.Name = Name;
	B.Event = Event;
	B.OwnerKey = OwnerKey;
	B.Callback = std::move(Callback);
	ActionBindings.push_back(std::move(B));
}

void UInputComponent::ClearBindings()
{
	AxisMappings.clear();
	ActionMappings.clear();
	AxisBindings.clear();
	ActionBindings.clear();
}

void UInputComponent::RemoveBindingsForOwner(const void* OwnerKey)
{
	if (!OwnerKey)
	{
		return;
	}

	AxisMappings.erase(
		std::remove_if(AxisMappings.begin(), AxisMappings.end(), [OwnerKey](const FAxisMapping& M) { return M.OwnerKey == OwnerKey; }),
		AxisMappings.end());
	ActionMappings.erase(
		std::remove_if(ActionMappings.begin(), ActionMappings.end(), [OwnerKey](const FActionMapping& M) { return M.OwnerKey == OwnerKey; }),
		ActionMappings.end());
	AxisBindings.erase(
		std::remove_if(AxisBindings.begin(), AxisBindings.end(), [OwnerKey](const FAxisBinding& B) { return B.OwnerKey == OwnerKey; }),
		AxisBindings.end());
	ActionBindings.erase(
		std::remove_if(ActionBindings.begin(), ActionBindings.end(), [OwnerKey](const FActionBinding& B) { return B.OwnerKey == OwnerKey; }),
		ActionBindings.end());
}

float UInputComponent::EvaluateAxisMapping(const FAxisMapping& Mapping, const FInputSystemSnapshot& Snapshot) const
{
	switch (Mapping.Key.Kind)
	{
	case EInputKeyKind::Keyboard:
	case EInputKeyKind::MouseButton:
		return Snapshot.IsDown(Mapping.Key.KeyCode) ? Mapping.Scale : 0.0f;
	case EInputKeyKind::MouseAxis:
		switch (static_cast<EMouseAxis>(Mapping.Key.KeyCode))
		{
		case EMouseAxis::X:
			return static_cast<float>(Snapshot.MouseDeltaX) * Mapping.Scale;
		case EMouseAxis::Y:
			return static_cast<float>(Snapshot.MouseDeltaY) * Mapping.Scale;
		case EMouseAxis::Wheel:
			return static_cast<float>(Snapshot.ScrollDelta) * Mapping.Scale;
		default:
			return 0.0f;
		}
	case EInputKeyKind::GamepadAxis:
	{
		const int32 AxisIndex = Mapping.Key.KeyCode;
		return AxisIndex >= 0 && AxisIndex < static_cast<int32>(EGamepadAxis::Count)
			? Snapshot.GamepadSnapshot.Axes[AxisIndex] * Mapping.Scale
			: 0.0f;
	}
	default:
		return 0.0f;
	}
}

bool UInputComponent::EvaluateActionMapping(const FActionMapping& Mapping, EInputEvent Event, const FInputSystemSnapshot& Snapshot) const
{
	if (Mapping.Key.Kind == EInputKeyKind::Keyboard || Mapping.Key.Kind == EInputKeyKind::MouseButton)
	{
		return Event == EInputEvent::Pressed
			? Snapshot.WasPressed(Mapping.Key.KeyCode)
			: Snapshot.WasReleased(Mapping.Key.KeyCode);
	}

	if (Mapping.Key.Kind == EInputKeyKind::GamepadButton)
	{
		const int32 ButtonIndex = Mapping.Key.KeyCode;
		if (ButtonIndex < 0 || ButtonIndex >= static_cast<int32>(EGamepadButton::Count))
		{
			return false;
		}

		const bool bDown = Snapshot.GamepadSnapshot.Buttons[ButtonIndex];
		const bool bWasDown = Snapshot.GamepadSnapshot.PrevButtons[ButtonIndex];
		return Event == EInputEvent::Pressed ? (bDown && !bWasDown) : (!bDown && bWasDown);
	}

	if (Mapping.Key.Kind == EInputKeyKind::GamepadAxis)
	{
		constexpr float TriggerActionThreshold = 0.5f;
		const int32 AxisIndex = Mapping.Key.KeyCode;
		if (AxisIndex < 0 || AxisIndex >= static_cast<int32>(EGamepadAxis::Count))
		{
			return false;
		}

		const bool bDown = Snapshot.GamepadSnapshot.Axes[AxisIndex] >= TriggerActionThreshold;
		const bool bWasDown = Snapshot.GamepadSnapshot.PrevAxes[AxisIndex] >= TriggerActionThreshold;
		return Event == EInputEvent::Pressed ? (bDown && !bWasDown) : (!bDown && bWasDown);
	}

	return false;
}

void UInputComponent::ProcessInput(const FInputSystemSnapshot& Snapshot, float /*DeltaTime*/)
{
	// Axis: 매핑 평가 → name 별 합산 → 매칭 binding 호출.
	// UE 와 동일 — 매 frame 호출 (value=0 도 호출됨, 자식이 0 분기 처리).
	for (const FAxisBinding& B : AxisBindings)
	{
		float Value = Snapshot.GetAxis(B.Name);
		if (std::abs(Value) <= AXIS_EPSILON)
		{
			Value = 0.0f;
			for (const FAxisMapping& M : AxisMappings)
			{
				if (M.Name == B.Name)
				{
					Value += EvaluateAxisMapping(M, Snapshot);
				}
			}
		}
		if (B.Callback) B.Callback(Value);
	}

	// Action: edge 감지 (Pressed = KeyDown, Released = KeyUp).
	for (const FActionBinding& B : ActionBindings)
	{
		const bool bLogicalFired = (B.Event == EInputEvent::Pressed)
			? Snapshot.WasActionPressed(B.Name)
			: Snapshot.WasActionReleased(B.Name);
		if (bLogicalFired && B.Callback)
		{
			B.Callback();
			continue;
		}

		for (const FActionMapping& M : ActionMappings)
		{
			if (M.Name != B.Name) continue;
			const bool bFired = EvaluateActionMapping(M, B.Event, Snapshot);
			if (bFired && B.Callback)
			{
				B.Callback();
				break;  // 같은 action 의 여러 매핑이 같은 frame 발화해도 1회만.
			}
		}
	}
}

void UInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// 입력 처리는 PlayerController → Possessed Pawn → ProcessInput 경로에서만 수행한다.
}
