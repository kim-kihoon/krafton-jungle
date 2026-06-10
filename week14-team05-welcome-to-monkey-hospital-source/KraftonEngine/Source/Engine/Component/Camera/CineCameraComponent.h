#pragma once

#include "Component/Camera/CameraComponent.h"
#include "Core/Types/EngineTypes.h"

#include "Source/Engine/Component/Camera/CineCameraComponent.generated.h"

UCLASS()
class UCineCameraComponent : public UCameraComponent
{
public:
	GENERATED_BODY()
	UCineCameraComponent() = default;

	UFUNCTION(Callable, Exec, Category="Cinematic")
	void SetLetterboxEnabled(bool bEnabled) { UCameraComponent::SetLetterboxEnabled(bEnabled); }
	UFUNCTION(Callable, Exec, Category="Cinematic")
	void SetLetterboxAmount(float Amount) { UCameraComponent::SetLetterboxAmount(Amount); }
	UFUNCTION(Callable, Exec, Category="Cinematic")
	void SetLetterboxThickness(float Thickness) { UCameraComponent::SetLetterboxThickness(Thickness); }
	UFUNCTION(Callable, Exec, Category="Cinematic")
	void SetLetterboxColor(FLinearColor Color) { UCameraComponent::SetLetterboxColor(Color); }

	const FCameraLetterboxState& GetLetterboxSettings() const { return UCameraComponent::GetLetterboxSettings(); }
};
