#pragma once
#include "IState.h"

#include "UPlanetPlacementManager.h"

class InGameReadyState : public IState
{
public:
	InGameReadyState() = default;
	virtual ~InGameReadyState() = default;

	void OnEnter(class UManager* manager) override;
	IState* Update(float deltaTime, class UManager* manager) override;
	void Render(class URenderer* renderer, class UManager* manager) override;
	void OnExit(class UManager* manager) override;

	UPlanetPlacementManager* PlanetPlacementManager;
	ImageButtonInfo* selectButton;
	UManager* cachedManager;

	bool bGoToStart = false;
	bool bGoToRetry = false;
	bool bGoToMain = false;
};

