#pragma once

#include "CameraComponent.h"
#include "Core/EngineTypes.h"
#include "CineCameraComponent.generated.h"

USTRUCT()
struct FCineLetterboxSettings
{
	GENERATED_BODY(FCineLetterboxSettings)

	UPROPERTY(Edit, DisplayName="Enable Letterbox")
	bool bEnabled = false;

	UPROPERTY(Edit, DisplayName="Letterbox Amount", Min=0.0f, Max=1.0f, Speed=0.01f)
	float Amount = 1.0f;

	UPROPERTY(Edit, DisplayName="Letterbox Thickness", Min=0.0f, Max=0.5f, Speed=0.01f)
	float Thickness = 0.12f;

	UPROPERTY(Edit, DisplayName="Letterbox Color", Type=Color4)
	FLinearColor Color = FLinearColor::Black();
};

UCLASS()
class UCineCameraComponent : public UCameraComponent
{
public:
	GENERATED_BODY(UCineCameraComponent)
	UCineCameraComponent() = default;

	void SetLetterboxEnabled(bool bEnabled) { Letterbox.bEnabled = bEnabled; }
	void SetLetterboxAmount(float Amount) { Letterbox.Amount = Amount; }
	void SetLetterboxThickness(float Thickness) { Letterbox.Thickness = Thickness; }
	void SetLetterboxColor(FLinearColor Color) { Letterbox.Color = Color; }

	const FCineLetterboxSettings& GetLetterboxSettings() const { return Letterbox; }

private:
	UPROPERTY(Edit, Category="Cinematic", DisplayName="Letterbox")
	FCineLetterboxSettings Letterbox;
};
