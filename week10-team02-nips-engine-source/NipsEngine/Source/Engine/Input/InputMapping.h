#pragma once

#include "Core/CoreMinimal.h"
#include "Engine/Input/InputSystem.h"
#include "Object/FName.h"

// Input Action은 "키를 눌렀다/뗐다"처럼 한 번 발생하는 입력입니다.
// 예: ToggleInputCapture = F4, Interact = E, Jump = Space
struct FInputActionMapping
{
	FName ActionName = FName::None;
	int VK = 0;
};

// Input Axis는 매 프레임 읽는 연속 값입니다.
// 같은 AxisName에 여러 키를 묶고 Scale로 방향을 정합니다.
// 예: MoveForward = W(+1) + S(-1), MoveRight = D(+1) + A(-1)
struct FInputAxisMapping
{
	FName AxisName = FName::None;
	int VK = 0;
	float Scale = 0.0f;
};

// ────── [중요] 원하는 키를 매핑하는 순서 ────────────────────────────────────────────────────────────
// 1. 컨트롤러에서 입력 이름을 FName으로 정합니다. 예: "Interact", "MoveForward"
// 2. AddActionMapping 또는 AddAxisMapping으로 원하는 VK 키를 연결합니다.
// 3. 입력 처리 코드(GamePlayerController 등)에서 IsActionKey 또는 GetAxisValue로 의미 입력을 읽습니다.
// ──────────────────────────────────────────────────────────────────────────────────────────────────

class FInputMappingContext
{
public:
	void Clear()
	{
		ActionMappings.clear();
		AxisMappings.clear();
	}

	void AddActionMapping(const FName& ActionName, int VK)
	{
		ActionMappings.push_back(FInputActionMapping{ ActionName, VK });
	}

	// Scale은 이 키가 값에 더할 숫자입니다. 반대 방향 키는 -1을 씁니다.
	// 한 Axis에 여러 키를 연결할 수 있습니다. 예: W(+1), S(-1), GamepadUp(+1)
	void AddAxisMapping(const FName& AxisName, int VK, float Scale)
	{
		AxisMappings.push_back(FInputAxisMapping{ AxisName, VK, Scale });
	}

	// 라우터가 알려준 VK가 특정 Action에 연결된 키인지 확인합니다.
	bool IsActionKey(const FName& ActionName, int VK) const
	{
		for (const FInputActionMapping& Mapping : ActionMappings)
		{
			if (Mapping.ActionName == ActionName && Mapping.VK == VK)
			{
				return true;
			}
		}
		return false;
	}

	// 현재 눌려 있는 모든 키의 Scale을 더해 Axis 값을 만듭니다.
	// W(+1)와 S(-1)를 동시에 누르면 0이 되어 앞으로도 뒤로도 가지 않습니다.
	float GetAxisValue(const FName& AxisName) const
	{
		float Value = 0.0f;
		const InputSystem& Input = InputSystem::Get();
		for (const FInputAxisMapping& Mapping : AxisMappings)
		{
			if (Mapping.AxisName == AxisName && Input.GetKey(Mapping.VK))
			{
				Value += Mapping.Scale;
			}
		}
		return Value;
	}

private:
	TArray<FInputActionMapping> ActionMappings;
	TArray<FInputAxisMapping> AxisMappings;
};
