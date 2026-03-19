#pragma once
#include "IState.h"

class MainState : public IState
{
public:
	MainState() = default;
	~MainState() = default;
	void OnEnter(UManager * manager) override;
	IState* Update(float deltaTime, UManager* manager) override;
	void Render(URenderer* renderer, UManager* manager) override;
	void OnExit(UManager* manager) override;

	//	LOGO Animation
	const float TopOffsetY = -25.f;
	const float BottomOffsetY = 25.f;
	float OffsetY = 0.f;
	float DY = -0.5f;

	UIFrame* LogoFrame;

	//	Contributors
	UIFrame* InfoFrame;
	//bool bInfoVisible = false;

	//	Game Start Flag
	bool bIsGameStart = false;
};

