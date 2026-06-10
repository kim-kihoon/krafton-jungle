#pragma once

#include "Core/Types/CoreTypes.h"
#include "Object/Reflection/ObjectMacros.h"
#include "Object/Reflection/UStruct.h"

#include "Source/Engine/Math/FloatCurve.generated.h"

UENUM()
enum class ECurveInterpMode : uint8
{
	Constant,
	Linear,
	Cubic,
};

UENUM()
enum class ECurveExtrapMode : uint8
{
	Clamp,
	Linear,
	Loop,
};

UENUM()
enum class ECurveTangentMode : uint8
{
	Auto,
	User,
	Break,
};

USTRUCT()
struct FCurveKey
{
	GENERATED_BODY()

	UPROPERTY(Save)
	float Time;

	UPROPERTY(Save)
	float Value;

	UPROPERTY(Save)
	float ArriveTangent = 0.0f;

	UPROPERTY(Save)
	float LeaveTangent = 0.0f;

	UPROPERTY(Save, Enum=ECurveInterpMode)
	ECurveInterpMode InterpMode = ECurveInterpMode::Linear;

	UPROPERTY(Save, Enum=ECurveTangentMode)
	ECurveTangentMode TangentMode = ECurveTangentMode::Auto;
};

USTRUCT()
struct FFloatCurve
{
	GENERATED_BODY()

	UPROPERTY(Save, Type=Array, Struct=FCurveKey)
	TArray<FCurveKey> Keys;

	UPROPERTY(Save, Enum=ECurveExtrapMode)
	ECurveExtrapMode PreExtrapMode = ECurveExtrapMode::Clamp;

	UPROPERTY(Save, Enum=ECurveExtrapMode)
	ECurveExtrapMode PostExtrapMode = ECurveExtrapMode::Clamp;

	UPROPERTY(Save)
	float DefaultValue = 0.0f;

	bool IsEmpty() const;
	void Reset();

	void AddKey(float Time, float Value, ECurveInterpMode InterpMode = ECurveInterpMode::Linear);
	void SortKeys();
	void AutoSetTangents();

	float Evaluate(float Time) const;

private:
	int32 FindKeyIndexBefore(float Time) const;
	float EvaluateSegment(const FCurveKey& A, const FCurveKey& B, float Time) const;
};
