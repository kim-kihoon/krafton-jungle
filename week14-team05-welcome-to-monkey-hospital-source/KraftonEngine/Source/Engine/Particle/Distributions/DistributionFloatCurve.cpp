#include "DistributionFloatCurve.h"

#include <algorithm>

UDistributionFloatCurve::UDistributionFloatCurve()
{
	SetLinear(0.0f, 0.0f, 1.0f, 1.0f);
}

const char* UDistributionFloatCurve::GetDistributionDisplayName() const
{
	return "Distribution Float Constant Curve";
}

float UDistributionFloatCurve::GetValue(float Time, UObject* Data) const
{
	(void)Data;
	return Curve.Evaluate(Time);
}

void UDistributionFloatCurve::GetInRange(float& OutMin, float& OutMax) const
{
	if (Curve.Keys.empty())
	{
		OutMin = 0.0f;
		OutMax = 1.0f;
		return;
	}

	OutMin = Curve.Keys.front().Time;
	OutMax = Curve.Keys.front().Time;
	for (const FCurveKey& Key : Curve.Keys)
	{
		OutMin = (std::min)(OutMin, Key.Time);
		OutMax = (std::max)(OutMax, Key.Time);
	}
}

void UDistributionFloatCurve::GetOutRange(float& OutMin, float& OutMax) const
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

void UDistributionFloatCurve::SetConstant(float Value)
{
	Curve.Reset();
	Curve.DefaultValue = Value;
	Curve.AddKey(0.0f, Value, ECurveInterpMode::Linear);
	Curve.AddKey(1.0f, Value, ECurveInterpMode::Linear);
	Curve.SortKeys();
}

void UDistributionFloatCurve::SetLinear(float StartTime, float StartValue, float EndTime, float EndValue)
{
	Curve.Reset();
	Curve.DefaultValue = StartValue;
	Curve.AddKey(StartTime, StartValue, ECurveInterpMode::Linear);
	Curve.AddKey(EndTime, EndValue, ECurveInterpMode::Linear);
	Curve.SortKeys();
	Curve.AutoSetTangents();
}
