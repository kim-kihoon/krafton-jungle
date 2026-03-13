#pragma once
#include "IState.h"

class UManager;

class StateMachine
{
private:
	IState* currentState = nullptr;

public:
	StateMachine();
	~StateMachine();

	void Initialize(IState* initialState, UManager* manager);

	// 매초 공정을 체크하고, 상태 전환을 감시함
	void Update(float deltaTime, UManager* manager);

	// 시각화 공정을 현재 상태에게 위임함
	void Render(URenderer* renderer, UManager* manager);
};

