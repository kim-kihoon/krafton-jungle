#pragma once
#include "IState.h"

class StageSelectionState : public IState
{
public:
	StageSelectionState() = default;
	virtual ~StageSelectionState() = default;

	void OnEnter(class UManager* manager) override;
	IState* Update(float deltaTime, class UManager* manager) override;
	void Render(class URenderer* renderer, class UManager* manager) override;
	void OnExit(class UManager* manager) override;

	bool bIsStageSelected = false; // 스테이지 선택 여부를 추적하는 변수
	bool bGoToMain = false; // 메인 메뉴로 돌아갈 때 상태 전환 여부를 추적하는 변수

	UManager* Manager = nullptr; // UManager에 대한 포인터 추가
};
