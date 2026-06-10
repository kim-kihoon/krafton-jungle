#include "DistributionVectorUniformCurve.h"

#include <algorithm>
#include <cstdlib>

namespace
{
	float RandomFloat()
	{
		return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
	}

	float RandomRange(float InA, float InB)
	{
		const float SafeMin = (std::min)(InA, InB);
		const float SafeMax = (std::max)(InA, InB);
		const float Alpha = RandomFloat();
		return SafeMin + (SafeMax - SafeMin) * Alpha;
	}

	void GetCurveTimeRange(const FFloatCurve& Curve, bool& bHasRange, float& OutMin, float& OutMax)
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

	void GetCurveValueRange(const FFloatCurve& Curve, float& OutMin, float& OutMax)
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

	void SetCurveConstant(FFloatCurve& Curve, float Value)
	{
		Curve.Reset();
		Curve.DefaultValue = Value;
		Curve.AddKey(0.0f, Value, ECurveInterpMode::Linear);
		Curve.AddKey(1.0f, Value, ECurveInterpMode::Linear);
		Curve.SortKeys();
	}

	void SetCurveLinear(FFloatCurve& Curve, float StartTime, float StartValue, float EndTime, float EndValue)
	{
		Curve.Reset();
		Curve.DefaultValue = StartValue;
		Curve.AddKey(StartTime, StartValue, ECurveInterpMode::Linear);
		Curve.AddKey(EndTime, EndValue, ECurveInterpMode::Linear);
		Curve.SortKeys();
		Curve.AutoSetTangents();
	}
}

UDistributionVectorUniformCurve::UDistributionVectorUniformCurve()
{
	SetConstant(FVector(0.0f, 0.0f, 0.0f), FVector(1.0f, 1.0f, 1.0f));
}

const char* UDistributionVectorUniformCurve::GetDistributionDisplayName() const
{
	return "Distribution Vector Uniform Curve";
}

FVector UDistributionVectorUniformCurve::GetValue(float Time, UObject* Data) const
{
	(void)Data;

	return FVector(
		RandomRange(MinXCurve.Evaluate(Time), MaxXCurve.Evaluate(Time)),
		RandomRange(MinYCurve.Evaluate(Time), MaxYCurve.Evaluate(Time)),
		RandomRange(MinZCurve.Evaluate(Time), MaxZCurve.Evaluate(Time))
	);
}

void UDistributionVectorUniformCurve::GetInRange(float& OutMin, float& OutMax) const
{
	bool bHasRange = false;
	GetCurveTimeRange(MinXCurve, bHasRange, OutMin, OutMax);
	GetCurveTimeRange(MinYCurve, bHasRange, OutMin, OutMax);
	GetCurveTimeRange(MinZCurve, bHasRange, OutMin, OutMax);
	GetCurveTimeRange(MaxXCurve, bHasRange, OutMin, OutMax);
	GetCurveTimeRange(MaxYCurve, bHasRange, OutMin, OutMax);
	GetCurveTimeRange(MaxZCurve, bHasRange, OutMin, OutMax);

	if (!bHasRange)
	{
		OutMin = 0.0f;
		OutMax = 1.0f;
	}
}

void UDistributionVectorUniformCurve::GetRange(FVector& OutMin, FVector& OutMax) const
{
	float MinX0 = 0.0f, MinX1 = 0.0f;
	float MinY0 = 0.0f, MinY1 = 0.0f;
	float MinZ0 = 0.0f, MinZ1 = 0.0f;
	float MaxX0 = 0.0f, MaxX1 = 0.0f;
	float MaxY0 = 0.0f, MaxY1 = 0.0f;
	float MaxZ0 = 0.0f, MaxZ1 = 0.0f;

	GetCurveValueRange(MinXCurve, MinX0, MinX1);
	GetCurveValueRange(MinYCurve, MinY0, MinY1);
	GetCurveValueRange(MinZCurve, MinZ0, MinZ1);
	GetCurveValueRange(MaxXCurve, MaxX0, MaxX1);
	GetCurveValueRange(MaxYCurve, MaxY0, MaxY1);
	GetCurveValueRange(MaxZCurve, MaxZ0, MaxZ1);

	OutMin = FVector(
		(std::min)(MinX0, MaxX0),
		(std::min)(MinY0, MaxY0),
		(std::min)(MinZ0, MaxZ0)
	);
	OutMax = FVector(
		(std::max)(MinX1, MaxX1),
		(std::max)(MinY1, MaxY1),
		(std::max)(MinZ1, MaxZ1)
	);
}

void UDistributionVectorUniformCurve::SetConstant(const FVector& InMin, const FVector& InMax)
{
	SetCurveConstant(MinXCurve, InMin.X);
	SetCurveConstant(MinYCurve, InMin.Y);
	SetCurveConstant(MinZCurve, InMin.Z);
	SetCurveConstant(MaxXCurve, InMax.X);
	SetCurveConstant(MaxYCurve, InMax.Y);
	SetCurveConstant(MaxZCurve, InMax.Z);
}

void UDistributionVectorUniformCurve::SetLinear(float StartTime, const FVector& StartMin, const FVector& StartMax,
	float EndTime, const FVector& EndMin, const FVector& EndMax)
{
	SetCurveLinear(MinXCurve, StartTime, StartMin.X, EndTime, EndMin.X);
	SetCurveLinear(MinYCurve, StartTime, StartMin.Y, EndTime, EndMin.Y);
	SetCurveLinear(MinZCurve, StartTime, StartMin.Z, EndTime, EndMin.Z);
	SetCurveLinear(MaxXCurve, StartTime, StartMax.X, EndTime, EndMax.X);
	SetCurveLinear(MaxYCurve, StartTime, StartMax.Y, EndTime, EndMax.Y);
	SetCurveLinear(MaxZCurve, StartTime, StartMax.Z, EndTime, EndMax.Z);
}
