#include "DistributionFloatUniformCurve.h"

#include <algorithm>
#include <cstdlib>

namespace
{
	float RandomFloat()
	{
		return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
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

	void GetCurveValueRange(const FFloatCurve& Curve, bool& bHasRange, float& OutMin, float& OutMax)
	{
		if (Curve.Keys.empty())
		{
			const float Value = Curve.DefaultValue;
			if (!bHasRange)
			{
				OutMin = Value;
				OutMax = Value;
				bHasRange = true;
			}
			else
			{
				OutMin = (std::min)(OutMin, Value);
				OutMax = (std::max)(OutMax, Value);
			}
			return;
		}

		for (const FCurveKey& Key : Curve.Keys)
		{
			if (!bHasRange)
			{
				OutMin = Key.Value;
				OutMax = Key.Value;
				bHasRange = true;
			}
			else
			{
				OutMin = (std::min)(OutMin, Key.Value);
				OutMax = (std::max)(OutMax, Key.Value);
			}
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

UDistributionFloatUniformCurve::UDistributionFloatUniformCurve()
{
	SetConstant(0.0f, 1.0f);
}

const char* UDistributionFloatUniformCurve::GetDistributionDisplayName() const
{
	return "Distribution Float Uniform Curve";
}

float UDistributionFloatUniformCurve::GetValue(float Time, UObject* Data) const
{
	(void)Data;

	const float A = MinCurve.Evaluate(Time);
	const float B = MaxCurve.Evaluate(Time);
	const float SafeMin = (std::min)(A, B);
	const float SafeMax = (std::max)(A, B);
	const float Alpha = RandomFloat();
	return SafeMin + (SafeMax - SafeMin) * Alpha;
}

void UDistributionFloatUniformCurve::GetInRange(float& OutMin, float& OutMax) const
{
	bool bHasRange = false;
	GetCurveTimeRange(MinCurve, bHasRange, OutMin, OutMax);
	GetCurveTimeRange(MaxCurve, bHasRange, OutMin, OutMax);

	if (!bHasRange)
	{
		OutMin = 0.0f;
		OutMax = 1.0f;
	}
}

void UDistributionFloatUniformCurve::GetOutRange(float& OutMin, float& OutMax) const
{
	bool bHasRange = false;
	GetCurveValueRange(MinCurve, bHasRange, OutMin, OutMax);
	GetCurveValueRange(MaxCurve, bHasRange, OutMin, OutMax);

	if (!bHasRange)
	{
		OutMin = 0.0f;
		OutMax = 1.0f;
	}
}

void UDistributionFloatUniformCurve::SetConstant(float InMin, float InMax)
{
	SetCurveConstant(MinCurve, InMin);
	SetCurveConstant(MaxCurve, InMax);
}

void UDistributionFloatUniformCurve::SetLinear(float StartTime, float StartMin, float StartMax, float EndTime, float EndMin, float EndMax)
{
	SetCurveLinear(MinCurve, StartTime, StartMin, EndTime, EndMin);
	SetCurveLinear(MaxCurve, StartTime, StartMax, EndTime, EndMax);
}
