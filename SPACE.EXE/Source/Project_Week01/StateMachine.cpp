#include "StateMachine.h"
#include "UManager.h"

#pragma region __DEBUG_CONSOLE__
#include <iostream>
#pragma endregion

StateMachine::StateMachine() : currentState(nullptr) {}

StateMachine::~StateMachine()
{
	if (currentState)
	{
		// 종료 시 메모리 누수 방지
		delete currentState;
		currentState = nullptr;
	}
}

void StateMachine::Initialize(IState* initialState, UManager* manager)
{
	currentState = initialState;
	if (currentState) currentState->OnEnter(manager);
}

void StateMachine::Update(float deltaTime, UManager* manager)
{
	if (currentState)
	{
		IState* next = currentState->Update(deltaTime, manager);

		if (next != currentState)
		{
			//std::cout << "State Transition: " << typeid(*currentState).name() << " -> " << typeid(*next).name() << std::endl;

			currentState->OnExit(manager);
			delete currentState;
			currentState = next;
			currentState->OnEnter(manager);
		}
	}
}

void StateMachine::Render(URenderer* renderer, UManager* manager)
{
	if (currentState)
	{
		currentState->Render(renderer, manager);
	}
}