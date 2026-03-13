#include "InGameReadyState.h"
#include "UManager.h" // 매니저의 자원 접근을 위해 포함

#include "MainState.h"
#include "InGameRunState.h"
#include "LoadingState.h"

#pragma region __DEBUG_CONSOLE__
#include <iostream>
#pragma endregion

void InGameReadyState::OnEnter(UManager* manager)
{
	/* Placement Manager */
	cachedManager = manager;
	PlanetPlacementManager = new UPlanetPlacementManager(manager);

	// 1. 기존 객체 제거 (ClearGameObjects는 public으로 변경 필요)
	manager->ClearGameObjects();


	// 2. 스테이지 정보 추출 (필요한 Getter 추가가 필수적입니다)
	// CurStage와 StageInfoList는 현재 private이므로 Getter를 통해 가져와야 합니다.
	int StageIdx = (int)manager->GetCurStage() - 1;
	const auto& stageInfoList = manager->GetStageInfoList();


	if (StageIdx < 0 || StageIdx >= (int)stageInfoList.size()) return; 

	const FStageInfo& stageInfo = (stageInfoList[StageIdx]);
	manager->SetRemainTimer(stageInfo.MaxTime); // 타이머 설정

	// 스테이지 번호에 따른 BGM 전환
	EStage curStage = manager->GetCurStage();
	
	switch (curStage)
	{
	case EStage::ES_Stage1:
		manager->PlayBGM(EBGM::EBGM_Level1);
		break;
	case EStage::ES_Stage2:
		manager->PlayBGM(EBGM::EBGM_Level2);
		break;
	case EStage::ES_Stage3:
		manager->PlayBGM(EBGM::EBGM_Level3);
		break;
	default:
		manager->PlayBGM(EBGM::EBGM_TitleScreen);
		break;
	}


	// 3. 장애물(행성) 배치 로직
	for (const auto& obstacle : stageInfo.ObstacleList)
	{
		// 1. 스택 메모리에 안전하게 생성
		USphere obstaclePlanet;

		// 2. 데이터 시트 설정
		obstaclePlanet.SetLocation(obstacle.second);
		obstaclePlanet.SetScale(0.05f);
		obstaclePlanet.SetMass(1.0f);
		obstaclePlanet.SetRadius(0.02f);

		// 3. 시각적 식별자 설정 (생성자의 자동 배정보다 명시적 설정 권장)
		// USphere 생성자에서 count 기반으로 이미지가 정해지지만, 
		// 장애물은 특정 이미지(예: SATURN)로 고정하면 구분이 쉽습니다.
		obstaclePlanet.SetImageName(ImageName::PLANET3);

		// 4. 매니저의 월드 리스트에 등록 (내부에서 복사본을 생성하므로 안전함)
		manager->CreateNewPlanetWorld(obstaclePlanet);

		//std::cout << "Obstacle deployed at: " << obstacle.second.x << ", " << obstacle.second.y << std::endl;
	}

	// 4. 플레이어 배치 로직
	Probe* player = manager->GetProbe(); // 기존 public 함수 사용
	if (player == nullptr) {
		player = new Probe();
		manager->SetPlayer(player); // Player 포인터 설정을 위한 Setter 추가 필요
	}
	// 5. Goal 생성 & 배치
	Goal* goal = manager->getGoal();
	if (goal == nullptr)
	{
		goal = new Goal();
		manager->SetGoal(goal);

	}
	// 플레이어의 위치는 레벨에 따라 세팅 


	switch (StageIdx)
	{
	case 0:
		player->SetLocation({ -0.7f, -0.7f, 0.0f });
		player->SetVelocity({ 0.22f, 0.0f, 0.0f });
		goal->SetLocation({ 0.3f , 0.5f , 0.0f }); 
		break;
	case 1:
		player->SetLocation({ -0.25f , 0.8f , 0.0f });
		player->SetVelocity({ 0.0f , -0.15f , 0.0f });
		goal->SetLocation({ -0.25f ,-0.8f , 0.0f });
		break;
	case 2:
		player->SetLocation({ 0.3f , -0.8f , 0.0f });
		player->SetVelocity({ -0.1f , 0.2f , 0.0f });
		goal->SetLocation({ 0.3f , 0.8f , 0.0f });
		break;
	default:
		player->SetLocation({ 0.7f, -0.7f, 0.0f });
		player->SetVelocity(FVector(0.0f, 1.0f, 0.0f) * 0.0f);
		goal->SetLocation({ 0.0f , 0.7f , 0.0f });
		break;
	}
	player->SetScale(0.1f);


	// 5. UI 초기화 (기존 로직 유지)
	uiManager = new UIManager();

	UIFrame& bgFrame = uiManager->CreateFrame("ReadyState_Background")
		.Position(ImVec2(0, 0))
		.Size(ImVec2(1400, 1050))
		.NoTitleBar(true)
		.BackgroundColor(ImVec4(0, 0, 0, 0));


	//ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	//	Background
	const int tileSize = 350;
	const int segX = WindowWidth / tileSize;   // 20
	const int segY = WindowHeight / tileSize;  // 15

	for (int y = 0; y < segY; y++)
	{
		for (int x = 0; x < segX; x++)
		{
			bgFrame.AddImage(
				manager->GetResourceManager()->GetTexture("Background"),
				ImVec2((float)(tileSize * x), (float)(tileSize * y)),
				ImVec2((float)tileSize, (float)tileSize)
			);
		}
	}


	UIFrame& HUDFrame = uiManager->CreateFrame("ReadyState_HUD")
		.Position(ImVec2(0, 0))
		.Size(ImVec2(WindowWidth, WindowHeight))
		.NoTitleBar(true)
		.BackgroundColor(ImVec4(0, 0, 0, 0));

	HUDFrame.AddBlocker(
		"HUD_Blocker",
		ImVec2(WindowWidth * 3 / 4.f, 0),
		ImVec2(WindowWidth / 4, WindowHeight)
	);

	HUDFrame.AddImage(manager->GetResourceManager()->SRVInGamePanel,
		ImVec2(WindowWidth * 3 / 4.f, 0),
		ImVec2(WindowWidth / 4, WindowHeight)
	);

	HUDFrame.AddText("Click!",
		ImVec2(WindowWidth * 3 / 4.f + 175, 240),
		manager->GetResourceManager()->FontInfoRegularSmall);

	HUDFrame.AddImageButton("Planet 1",
		manager->GetResourceManager()->GetTexture(static_cast<ImageName>(USphere::count)),
		ImVec2(WindowWidth * 3 / 4.f + 175, 350),
		ImVec2(150, 150),
		[&]() {
			USphere* newPlanet = new USphere();
			PlanetPlacementManager->SetSelectedPlanet(newPlanet);

			USphere::checkCount();
			selectButton->texture = cachedManager->GetResourceManager()->GetTexture(static_cast<ImageName>(USphere::count));
		}
	); 

	selectButton = HUDFrame.GetImageButton("Planet 1");

	HUDFrame.AddImageButton("Start",
		manager->GetResourceManager()->SRVLeaderBoardPanel,
		ImVec2(WindowWidth * 3 / 4.f + 175, 600),
		ImVec2(200, 70),
		[&]() {
			bGoToStart = true;
		}
	);
	HUDFrame.AddText("Launch",
		ImVec2(WindowWidth * 3 / 4.f + 175 + 5, 600 + 5),
		manager->GetResourceManager()->FontDefaultSmall);

	HUDFrame.AddImageButton("Retry",
		manager->GetResourceManager()->SRVLeaderBoardPanel,
		ImVec2(WindowWidth * 3 / 4.f + 175, 700),
		ImVec2(200, 70),
		[&]() {
			bGoToRetry = true;
		}
	);
	HUDFrame.AddText("Retry",
		ImVec2(WindowWidth * 3 / 4.f + 175 + 5, 700 + 5),
		manager->GetResourceManager()->FontDefaultSmall);


	HUDFrame.AddImageButton("Home",
		manager->GetResourceManager()->SRVLeaderBoardPanel,
		ImVec2(WindowWidth * 3 / 4.f + 175, 800),
		ImVec2(200, 70),
		[&]() {
			bGoToMain = true;
		}
	);
	HUDFrame.AddText("Home",
		ImVec2(WindowWidth * 3 / 4.f + 175 + 5, 800 + 5),
		manager->GetResourceManager()->FontDefaultSmall);

}

IState* InGameReadyState::Update(float deltaTime, UManager* manager)
{
	nextState = this;
	// 준비 단계에서는 마우스 클릭으로 행성을 배치하는 로직 등을 추가할 수 있습니다.

	if(bGoToMain)
	{
		nextState = new MainState();
	}
	else if (bGoToRetry)
	{
		nextState = new LoadingState();
	}
	else if (bGoToStart)
	{
		nextState = new InGameRunState();
	}


	return nextState;
}


// texture 렌더링
void InGameReadyState::Render(URenderer* renderer, UManager* manager)
{
	PlanetPlacementManager->Update(0.f);


	
	if (uiManager) 
	{
		uiManager->Render();
	}

	// 1. 플레이어 렌더링
	Probe* pPlayer = manager->GetProbe();

	renderer->UpdateConstant(manager->getGoal()->GetTransformMatrix());
	MeshResource* goal = manager->getGoalResource();
	if (goal != nullptr) {
		renderer->textureRenderPrimitive(goal->VB, goal->IB, goal->IndexCount, manager->GetResourceManager()->GetTexture(ImageName::GOAL));
	}

	if (pPlayer) {
		renderer->UpdateConstant(pPlayer->GetTransformMatrix());
		MeshResource* res = manager->getProbeResource();
		renderer->textureRenderPrimitive(res->VB, res->IB, res->IndexCount, manager->GetResourceManager()->GetTexture(ImageName::ROCKET));
	}

	// 2. 행성들 렌더링
	MeshResource* sphereRes = manager->getSphereResource();
	for (auto& planet : manager->GetPlanetList()) {
		renderer->UpdateConstant(planet.GetTransformMatrix());
		renderer->textureRenderPrimitive(sphereRes->VB, sphereRes->IB, sphereRes->IndexCount, manager->GetResourceManager()->GetTexture(planet.getImageName()));
	}

	// 클릭하면 그림
	if (PlanetPlacementManager->IsPlacable())
	{
		USphere* selectedPlanet = PlanetPlacementManager->GetSelectedPlanet();
		if (selectedPlanet)
		{
			renderer->UpdateConstant(selectedPlanet->GetTransformMatrix());
			renderer->textureRenderPrimitive(sphereRes->VB, sphereRes->IB, sphereRes->IndexCount, manager->GetResourceManager()->GetTexture(selectedPlanet->getImageName()));
			
		}
	}


}

void InGameReadyState::OnExit(UManager* manager)
{
	if (uiManager) {
		delete uiManager;
		uiManager = nullptr;
	}
}