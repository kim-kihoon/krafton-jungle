#pragma once
#include "IState.h"
#include "Probe.h"
#include "USphere.h"
#include "UManager.h" //FStageInfo, EStage
#include "EndingState.h"

class InGameRunState : public IState
{
public:
	InGameRunState() = default;
	virtual ~InGameRunState() = default;

	void OnEnter(class UManager* manager) override;
	IState* Update(float deltaTime, class UManager* manager) override;
	void Render(class URenderer* renderer, class UManager* manager) override;
	void OnExit(class UManager* manager) override;

	UIFrame* hudFrame;
	bool bGoToStart = false;
	bool bGoToRetry = false;
	bool bGoToMain = false;
	bool borderCheck(UManager* manager);
	bool goalCheck(UManager* manager);

	float SimulationTextCounter = 0.f;
	const float SimulationTextDuration = 0.3f;
};

