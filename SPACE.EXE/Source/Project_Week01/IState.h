#pragma once
#include "URenderer.h"
#include "UIManager.h"

// 전방 선언: UManager와의 순환 참조 방지
class UManager;

class IState
{
protected:
	UIManager* uiManager = nullptr;
	IState* nextState = nullptr;

public:
	virtual ~IState()
	{
		if (uiManager)
		{
			delete uiManager;
			uiManager = nullptr;
		}
	}

	virtual void OnEnter(UManager* manager) = 0;

	virtual void OnExit(UManager* manager) = 0;

	virtual IState* Update(float deltaTime, UManager* manager) = 0;

	virtual void Render(URenderer* renderer, UManager* manager) = 0;

	virtual void OnMouseClick(UManager* manager) {}
};