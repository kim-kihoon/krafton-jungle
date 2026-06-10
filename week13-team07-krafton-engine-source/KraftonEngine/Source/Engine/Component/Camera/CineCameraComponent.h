#pragma once

#include "Component/Camera/CameraComponent.h"
#include "Core/Types/EngineTypes.h"

#include "Source/Engine/Component/Camera/CineCameraComponent.generated.h"
struct FCineLetterboxSettings
{
	bool bEnabled = false;
	float Amount = 1.0f;
	float Thickness = 0.12f;
	FLinearColor Color = FLinearColor::Black();
};

struct FCineDepthOfFieldSettings
{
	float FocalLength = 50.0f;
	float Aperture = 2.8f;
	float FocusDistance = 1000.0f;
};

UCLASS()
class UCineCameraComponent : public UCameraComponent
{
public:
	GENERATED_BODY()
	UCineCameraComponent() = default;

	UFUNCTION(Callable, Exec, Category="Cinematic")
	void SetLetterboxEnabled(bool bEnabled) { Letterbox.bEnabled = bEnabled; }
	UFUNCTION(Callable, Exec, Category="Cinematic")
	void SetLetterboxAmount(float Amount) { Letterbox.Amount = Amount; }
	UFUNCTION(Callable, Exec, Category="Cinematic")
	void SetLetterboxThickness(float Thickness) { Letterbox.Thickness = Thickness; }
	void SetLetterboxColor(FLinearColor Color) { Letterbox.Color = Color; }

	const FCineLetterboxSettings& GetLetterboxSettings() const { return Letterbox; }
	const FCineDepthOfFieldSettings& GetDepthOfFieldSettings() const { return DepthOfField; }

	UFUNCTION(Callable, Exec, Category="Cinematic")
	void SetFocalLength(float InFocalLength) { DepthOfField.FocalLength = InFocalLength; }
	UFUNCTION(Callable, Exec, Category="Cinematic")
	void SetAperture(float InAperture) { DepthOfField.Aperture = InAperture; }
	UFUNCTION(Callable, Exec, Category="Cinematic")
	void SetFocusDistance(float InFocusDistance) { DepthOfField.FocusDistance = InFocusDistance; }

private:
	UPROPERTY(Edit, Save, Category="Cinematic", DisplayName="Enable Letterbox", Member=Letterbox.bEnabled, Type=Bool);
	UPROPERTY(Edit, Save, Category="Cinematic", DisplayName="Letterbox Amount", Member=Letterbox.Amount, Type=Float, Min=0.0f, Max=1.0f, Speed=0.01f);
	UPROPERTY(Edit, Save, Category="Cinematic", DisplayName="Letterbox Thickness", Member=Letterbox.Thickness, Type=Float, Min=0.0f, Max=0.5f, Speed=0.01f);
	UPROPERTY(Edit, Save, Category="Cinematic", DisplayName="Letterbox Color", Member=Letterbox.Color, Type=Color4);
	FCineLetterboxSettings Letterbox;

	UPROPERTY(Edit, Save, Category="Cinematic", DisplayName="Focal Length", Member=DepthOfField.FocalLength, Type=Float, Min=0.001f, Max=10000.0f, Speed=0.1f);
	UPROPERTY(Edit, Save, Category="Cinematic", DisplayName="Aperture", Member=DepthOfField.Aperture, Type=Float, Min=0.001f, Max=128.0f, Speed=0.1f);
	UPROPERTY(Edit, Save, Category="Cinematic", DisplayName="Focus Distance (m)", Member=DepthOfField.FocusDistance, Type=Float, Min=0.001f, Max=1000.0f, Speed=0.1f);
	FCineDepthOfFieldSettings DepthOfField;
};
