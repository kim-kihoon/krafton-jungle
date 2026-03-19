#pragma once
#include "IState.h"

class LoadingState : public IState
{
private:
    float m_currentTimer = 0.0f;
    const float TOTAL_LOADING_TIME = 3.0f;

	float m_displayTimer = 0.0f;

	UIFrame* LoadingFrame = nullptr;

public:
    LoadingState() = default;
    virtual ~LoadingState() = default;

    void OnEnter(class UManager* manager) override;
    IState* Update(float deltaTime, class UManager* manager) override;
    void Render(class URenderer* renderer, class UManager* manager) override;
    void OnExit(class UManager* manager) override;

};

