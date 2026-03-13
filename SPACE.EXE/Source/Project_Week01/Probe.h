#pragma once
#include "Uprimitive.h"
#include "Trail.h"

class Probe : public UPrimitive
{
private:
	std::vector<Trail> trails;
	float trailGap;
	float trailInterval;
	float trainIntervalCounter;

public:
	Probe();
	virtual ~Probe();
	virtual DirectX::XMMATRIX GetTransformMatrix();

	void SetTrailInterbal(int interval) { trailInterval = interval <= 0 ? 1 : interval; }
	void TryAddTrail();
	void ResetTrail() { trails.clear(); }
	std::vector<Trail> GetTrails() { return trails; }
};
