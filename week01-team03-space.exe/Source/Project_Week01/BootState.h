#pragma once
#include "IState.h"

class BootState : public IState
{
public:
	BootState() = default;
	virtual ~BootState() = default;

	void OnEnter(class UManager* manager) override;
	IState* Update(float deltaTime, class UManager* manager) override;
	void Render(class URenderer* renderer, class UManager* manager) override;
	void OnExit(class UManager* manager) override;
};