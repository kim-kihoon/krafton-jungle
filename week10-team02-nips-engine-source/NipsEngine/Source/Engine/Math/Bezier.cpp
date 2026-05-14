#include "Bezier.h"

#include "Core/CoreTypes.h"
#include "Utils.h"
#include <cmath>


float Bezier::EvaluateCubic(float T, float P0, float P1, float P2, float P3)
{
	const float ClampedT = MathUtil::Clamp(T, 0.0f, 1.0f);
	const float U = 1.0f - ClampedT;

	return (U * U * U * P0)
		+ (3.0f * U * U * ClampedT * P1)
		+ (3.0f * U * ClampedT * ClampedT * P2)
		+ (ClampedT * ClampedT * ClampedT * P3);
}

float Bezier::EvaluateCubicDerivative(float T, float P1, float P2)
{
	const float ClampedT = MathUtil::Clamp(T, 0.0f, 1.0f);
	const float U = 1.0f - ClampedT;

	return (3.0f * U * U * P1) 
		+ (6.0f * U * ClampedT * (P2 - P1))
		+ (3.0f * ClampedT * ClampedT * (1.0f - P2));
}

// 목표 시간 진행도(X)가 주어졌을 때, 그에 대응하는 Bezier Curve의 T를 계산합니다.
float Bezier::SolveTForX(float X, float P1, float P2)
{
	const float ClampedX = MathUtil::Clamp(X, 0.0f, 1.0f);
    const float ClampedA = MathUtil::Clamp(P1, 0.0f, 1.0f);
    const float ClampedB = MathUtil::Clamp(P2, 0.0f, 1.0f);

	float T = ClampedX;
	for (int32 Iteration = 0; Iteration < 5; ++Iteration)
	{
		const float CurrentX = EvaluateCubic(T, 0.0f, ClampedA, ClampedB, 1.0f);
		const float Slope = EvaluateCubicDerivative(T, ClampedA, ClampedB);
		if (std::abs(Slope) < 1.0e-5f)
		{
			break;
		}

		T = MathUtil::Clamp(T - (CurrentX - ClampedX) / Slope, 0.0f, 1.0f);
	}

	float MinT = 0.0f;
	float MaxT = 1.0f;
	for (int32 Iteration = 0; Iteration < 8; ++Iteration)
	{
		const float CurrentX = EvaluateCubic(T, 0.0f, ClampedA, ClampedB, 1.0f);
		if (std::abs(CurrentX - ClampedX) < 1.0e-5f)
		{
			break;
		}

		if (CurrentX < ClampedX)
		{
			MinT = T;
		}
		else
		{
			MaxT = T;
		}

		T = 0.5f * (MinT + MaxT);
	}

	return T;
}

// 정규화 시간 X에 대한 cubic-bezier(x1, y1, x2, y2) easing(가감속) 값을 계산합니다.
float Bezier::EvaluateCubicEasing(float X, const float ControlPoints[6])
{
	if (ControlPoints == nullptr)
	{
		return MathUtil::Clamp(X, 0.0f, 1.0f);
	}

	const float ControlPointAX = MathUtil::Clamp(ControlPoints[0], 0.0f, 1.0f);
	const float ControlPointBX = MathUtil::Clamp(ControlPoints[2], 0.0f, 1.0f);
	const float T = SolveTForX(X, ControlPointAX, ControlPointBX);

	// cp[4] = P0 Y (시작값), cp[5] = P3 Y (끝값) — BezierUI와 동일한 레이아웃
	const float P0Y = ControlPoints[4];
	const float P3Y = ControlPoints[5];
	return EvaluateCubic(T, P0Y, ControlPoints[1], ControlPoints[3], P3Y);
}

// cp 레이아웃: [0]=P1x [1]=P1y [2]=P2x [3]=P2y [4]=P0y(시작) [5]=P3y(끝)
// t는 커브 파라미터(0~1)를 직접 받아 Y값을 반환합니다.
float Bezier::BezierValue(float T, const float ControlPoints[6])
{
	const float ClampedT = MathUtil::Clamp(T, 0.0f, 1.0f);
	return EvaluateCubic(ClampedT, ControlPoints[4], ControlPoints[1], ControlPoints[3], ControlPoints[5]);
}
