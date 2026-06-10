#pragma once

#include "Component/ActorComponent.h"
#include "Core/Types/CoreTypes.h"
#include "Input/InputTypes.h"

struct FInputSystemSnapshot;

// UE 의 EInputEvent 의 minimal subset — Repeat/DoubleClick 등은 후속.
UENUM()
enum class EInputEvent : uint8
{
	Pressed,
	Released,
};

UENUM()
enum class EInputAxisSourceType : uint8
{
	Key,
	MouseX,
	MouseY,
	MouseWheel,
};

// UE 의 UInputComponent 패턴 minimal:
//   - Axis mapping: 이름과 입력 source+scale 묶음. 같은 이름에 여러 source 가능.
//   - Action mapping: 이름과 키. 같은 이름에 여러 키 가능 (e.g. Jump = Space, Gamepad A).
//   - BindAxis: 매 frame 합산된 axis value(float) 로 callback.
//   - BindAction: 키 edge (Pressed/Released) 일 때만 callback.
//
// APawn 자식이 SetupInputComponent override 안에서 AddXxxMapping + BindXxx 호출하는 게 통상 패턴.
// 실제 입력 처리는 TickComponent 가 아니라 PlayerController → Possessed Pawn 경로에서 ProcessInput 으로만 수행한다.

#include "Source/Engine/Component/Input/InputComponent.generated.h"

UCLASS()
class UInputComponent : public UActorComponent
{
public:
	GENERATED_BODY()
	UInputComponent();
	~UInputComponent() override = default;

	// 매핑 — 코드 또는 ProjectSettings(.ini) 가 호출. 같은 이름에 여러 source 가능.
	void AddAxisMapping(const FString& Name, int VKey, float Scale = 1.0f);
	UFUNCTION(Callable, Category="Input|Mapping")
	void AddAxisMapping(const FString& Name, const FString& KeyName, float Scale = 1.0f);
	UFUNCTION(Callable, Category="Input|Mapping")
	void AddMouseAxisMapping(const FString& Name, EInputAxisSourceType Axis, float Scale = 1.0f);
	void AddActionMapping(const FString& Name, int VKey);
	UFUNCTION(Callable, Category="Input|Mapping")
	void AddActionMapping(const FString& Name, const FString& KeyName);

	// Runtime-owned mapping/binding. LuaBlueprintComponent 는 reload/endplay 시 자기 항목만 제거한다.
	void AddAxisMappingForOwner(const void* OwnerKey, const FString& Name, int VKey, float Scale = 1.0f);
	void AddAxisMappingForOwner(const void* OwnerKey, const FString& Name, const FString& KeyName, float Scale = 1.0f);
	void AddMouseAxisMappingForOwner(const void* OwnerKey, const FString& Name, EInputAxisSourceType Axis, float Scale = 1.0f);
	void AddActionMappingForOwner(const void* OwnerKey, const FString& Name, int VKey);
	void AddActionMappingForOwner(const void* OwnerKey, const FString& Name, const FString& KeyName);

	// Binding — Pawn 자식이 SetupInputComponent 안에서 호출.
	void BindAxis(const FString& Name, TFunction<void(float)> Callback);
	void BindAction(const FString& Name, EInputEvent Event, TFunction<void()> Callback);
	void BindAxisForOwner(const void* OwnerKey, const FString& Name, TFunction<void(float)> Callback);
	void BindActionForOwner(const void* OwnerKey, const FString& Name, EInputEvent Event, TFunction<void()> Callback);

	// 등록된 mapping/binding 전부 제거 — SetupInputComponent 재호출 전 호출.
	UFUNCTION(Callable, Category="Input|Binding")
	void ClearBindings();
	void RemoveBindingsForOwner(const void* OwnerKey);

	// PlayerController 가 Possessed Pawn 에 대해서만 호출하는 입력 처리 진입점.
	void ProcessInput(const FInputSystemSnapshot& Snapshot, float DeltaTime);

	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	struct FAxisMapping
	{
		FString Name;
		FInputKeyHandle Key;
		float Scale = 1.0f;
		const void* OwnerKey = nullptr;
	};
	struct FActionMapping { FString Name; FInputKeyHandle Key; const void* OwnerKey = nullptr; };
	struct FAxisBinding   { FString Name; const void* OwnerKey = nullptr; TFunction<void(float)> Callback; };
	struct FActionBinding { FString Name; EInputEvent Event = EInputEvent::Pressed; const void* OwnerKey = nullptr; TFunction<void()> Callback; };

	float EvaluateAxisMapping(const FAxisMapping& Mapping, const FInputSystemSnapshot& Snapshot) const;
	bool EvaluateActionMapping(const FActionMapping& Mapping, EInputEvent Event, const FInputSystemSnapshot& Snapshot) const;

	TArray<FAxisMapping>   AxisMappings;
	TArray<FActionMapping> ActionMappings;
	TArray<FAxisBinding>   AxisBindings;
	TArray<FActionBinding> ActionBindings;
};
