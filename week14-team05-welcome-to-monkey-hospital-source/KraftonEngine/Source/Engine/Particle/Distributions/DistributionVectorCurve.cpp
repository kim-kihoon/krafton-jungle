#include "DistributionVectorCurve.h"

#include <algorithm>

namespace
{
	static void GetCurveTimeRange(const FFloatCurve& Curve, bool& bHasRange, float& OutMin, float& OutMax)
	{
		for (const FCurveKey& Key : Curve.Keys)
		{
			if (!bHasRange)
			{
				OutMin = Key.Time;
				OutMax = Key.Time;
				bHasRange = true;
			}
			else
			{
				OutMin = (std::min)(OutMin, Key.Time);
				OutMax = (std::max)(OutMax, Key.Time);
			}
		}
	}

	static void GetCurveValueRange(const FFloatCurve& Curve, float& OutMin, float& OutMax)
	{
		if (Curve.Keys.empty())
		{
			OutMin = Curve.DefaultValue;
			OutMax = Curve.DefaultValue;
			return;
		}

		OutMin = Curve.Keys.front().Value;
		OutMax = Curve.Keys.front().Value;
		for (const FCurveKey& Key : Curve.Keys)
		{
			OutMin = (std::min)(OutMin, Key.Value);
			OutMax = (std::max)(OutMax, Key.Value);
		}
	}

	static void SetCurveConstant(FFloatCurve& Curve, float Value)
	{
		Curve.Reset();
		Curve.DefaultValue = Value;
		Curve.AddKey(0.0f, Value, ECurveInterpMode::Linear);
		Curve.AddKey(1.0f, Value, ECurveInterpMode::Linear);
		Curve.SortKeys();
	}

	static void SetCurveLinear(FFloatCurve& Curve, float StartTime, float StartValue, float EndTime, float EndValue)
	{
		Curve.Reset();
		Curve.DefaultValue = StartValue;
		Curve.AddKey(StartTime, StartValue, ECurveInterpMode::Linear);
		Curve.AddKey(EndTime, EndValue, ECurveInterpMode::Linear);
		Curve.SortKeys();
		Curve.AutoSetTangents();
	}
}

UDistributionVectorCurve::UDistributionVectorCurve()
{
	SetConstant(FVector(0.0f, 0.0f, 0.0f));
}

const char* UDistributionVectorCurve::GetDistributionDisplayName() const
{
	return "Distribution Vector Constant Curve";
}

FVector UDistributionVectorCurve::GetValue(float Time, UObject* Data) const
{
	(void)Data;
	return FVector(
		XCurve.Evaluate(Time),
		YCurve.Evaluate(Time),
		ZCurve.Evaluate(Time));
}

void UDistributionVectorCurve::GetInRange(float& OutMin, float& OutMax) const
{
	bool bHasRange = false;
	GetCurveTimeRange(XCurve, bHasRange, OutMin, OutMax);
	GetCurveTimeRange(YCurve, bHasRange, OutMin, OutMax);
	GetCurveTimeRange(ZCurve, bHasRange, OutMin, OutMax);

	if (!bHasRange)
	{
		OutMin = 0.0f;
		OutMax = 1.0f;
	}
}

void UDistributionVectorCurve::GetRange(FVector& OutMin, FVector& OutMax) const
{
	float MinX = 0.0f, MaxX = 0.0f;
	float MinY = 0.0f, MaxY = 0.0f;
	float MinZ = 0.0f, MaxZ = 0.0f;
	GetCurveValueRange(XCurve, MinX, MaxX);
	GetCurveValueRange(YCurve, MinY, MaxY);
	GetCurveValueRange(ZCurve, MinZ, MaxZ);
	OutMin = FVector(MinX, MinY, MinZ);
	OutMax = FVector(MaxX, MaxY, MaxZ);
}

void UDistributionVectorCurve::SetConstant(const FVector& Value)
{
	SetCurveConstant(XCurve, Value.X);
	SetCurveConstant(YCurve, Value.Y);
	SetCurveConstant(ZCurve, Value.Z);
}

void UDistributionVectorCurve::SetLinear(float StartTime, const FVector& StartValue, float EndTime, const FVector& EndValue)
{
	SetCurveLinear(XCurve, StartTime, StartValue.X, EndTime, EndValue.X);
	SetCurveLinear(YCurve, StartTime, StartValue.Y, EndTime, EndValue.Y);
	SetCurveLinear(ZCurve, StartTime, StartValue.Z, EndTime, EndValue.Z);
}
