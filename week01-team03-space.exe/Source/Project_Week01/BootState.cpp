#include "BootState.h"
#include "MainState.h"
#include "UManager.h"
#include "EndingState.h"

void BootState::OnEnter(UManager* manager)
{
	// 스테이지 정보 생성
	auto& stageList = const_cast<std::vector<FStageInfo>&>(manager->GetStageInfoList());
	if (stageList.empty()) {
		stageList.push_back({ EStage::ES_Stage1, 30.f });
		stageList.push_back({ EStage::ES_Stage2, 30.f });
		stageList.push_back({ EStage::ES_Stage3, 30.f });
	}

	//	Player 이름 정보
	manager->SetPlayerName(EndingState::RandomNameGenerator()); // 초기 이름 설정 (필요 시 변경 가능)

	// 기하 구조 생성 (삼각형, 구체 정점 데이터 준비)
	//manager->getProbeResource()->GenerateTriangle();
	//manager->getSphereResource()->GenerateSphere(1.0f);
	//manager->getGoalResource()->GenerateTriangle();
	// 4. 메모리 예약 (행성 리스트 공간 확보)
	// manager->ReservePlanetList(50); // 필요 시 추가
}

IState* BootState::Update(float deltaTime, UManager* manager)
{
	return new MainState();
}

void BootState::Render(URenderer* renderer, UManager* manager)
{
}

void BootState::OnExit(UManager* manager)
{
}