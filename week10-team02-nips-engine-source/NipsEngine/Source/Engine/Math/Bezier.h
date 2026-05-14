#pragma once

namespace Bezier
{
    float EvaluateCubic(float T, float P0, float P1, float P2, float P3);
	float EvaluateCubicDerivative(float T, float P1, float P2);
	float SolveTForX(float X, float P1, float P2);
	float EvaluateCubicEasing(float X, const float ControlPoints[6]);
	float BezierValue(float T, const float ControlPoints[6]);
}
