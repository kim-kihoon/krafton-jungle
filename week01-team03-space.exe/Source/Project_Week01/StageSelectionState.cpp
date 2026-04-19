#include "StageSelectionState.h"
#include "UManager.h"

#include "LoadingState.h"
#include "MainState.h"

void StageSelectionState::OnEnter(UManager* manager)
{
	Manager = manager;

	// Stage Selection State Initialization
	manager->ClearGameObjects();

	// UI 관리자 생성 및 스테이지 선택 버튼 배치
	uiManager = new UIManager();

	//	Background
	UIFrame& bgFrame = uiManager->CreateFrame("MainState")
		.Position(ImVec2(0, 0))
		.Size(ImVec2(1400, 1050))
		.NoTitleBar(true)
		.BackgroundColor(ImVec4(0, 0, 0, 0));

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

	UIFrame& btnFrame = uiManager->CreateFrame("MainState")
		.Position(ImVec2(0, 0))
		.Size(ImVec2(1400, 1050))
		.NoTitleBar(true)
		.BackgroundColor(ImVec4(0, 0, 0, 0)
		);
	
	btnFrame.AddSpriteButton9("Stage 1",
		manager->GetResourceManager()->SRVButtonSprite,
		ImVec2(WindowWidth / 2 - 300, WindowHeight / 2 - 100),
		ImVec2(120, 120),
		2,
		6.f,
		[&]() {
			Manager->SetCurStage(EStage::ES_Stage1);
			bIsStageSelected = true;
		}
	);

	btnFrame.AddText("1",
		ImVec2(WindowWidth / 2 - 300, WindowHeight / 2 - 100),
		manager->GetResourceManager()->FontDefault
	);

	ImGui::BeginDisabled(manager->GetCurAvailableStage() < EStage::ES_Stage2); // Stage 2가 선택 가능하지 않으면 버튼 비활성화
	btnFrame.AddSpriteButton9("Stage 2",
		manager->GetResourceManager()->SRVButtonSprite,
		ImVec2(WindowWidth / 2, WindowHeight / 2 - 100),
		ImVec2(120, 120),
		2,
		6.f,
		[&]() {
			Manager->SetCurStage(EStage::ES_Stage2);
			bIsStageSelected = true;
		},
		Manager->GetCurAvailableStage() >= EStage::ES_Stage2 // Stage 2가 선택 가능할 때만 버튼 활성화
	);

	btnFrame.AddText("2",
		ImVec2(WindowWidth / 2, WindowHeight / 2 - 100),
		manager->GetResourceManager()->FontDefault
	);

	ImGui::EndDisabled();

	ImGui::BeginDisabled(manager->GetCurAvailableStage() < EStage::ES_Stage3); // Stage 3이 선택 가능하지 않으면 버튼 비활성화
	btnFrame.AddSpriteButton9("Stage 3",
		manager->GetResourceManager()->SRVButtonSprite,
		ImVec2(WindowWidth / 2 + 300, WindowHeight / 2 - 100),
		ImVec2(120, 120),
		2,
		6.f,
		[&]() {
			Manager->SetCurStage(EStage::ES_Stage3);
			bIsStageSelected = true;
		},
		Manager->GetCurAvailableStage() >= EStage::ES_Stage3 // Stage 3이 선택 가능할 때만 버튼 활성화
	);

	btnFrame.AddText("3",
		ImVec2(WindowWidth / 2 + 300, WindowHeight / 2 -100),
		manager->GetResourceManager()->FontDefault
	);

	ImGui::EndDisabled();

	btnFrame.AddSpriteButton("Go to main",
		manager->GetResourceManager()->SRVButtonSprite,
		ImVec2(50, WindowHeight - 50),
		ImVec2(60, 60),
		17,
		//	Toggle
		[&]() {
			bGoToMain = true;
		}
	);
}

IState* StageSelectionState::Update(float deltaTime, UManager* manager)
{
	nextState = this;

	if(bIsStageSelected)
	{
		// 스테이지가 선택되면 다음 상태로 전환
		nextState = new LoadingState();
	}
	if(bGoToMain)
	{
		// 메인 메뉴로 돌아갈 때 상태 전환
		nextState = new MainState();
	}

	// [참고] 스테이지가 선택되어 manager의 상태가 Loading으로 바뀌면,
	// 여기서 상태 전환(LoadingState로 이동 등)을 판단할 수 있습니다.

	return nextState;
}


void StageSelectionState::Render(URenderer* renderer, UManager* manager)
{
	// 스테이지 선택 화면 UI 출력
	if (uiManager) uiManager->Render();
}

void StageSelectionState::OnExit(UManager* manager)
{
	if (uiManager) {
		delete uiManager;
		uiManager = nullptr;
	}
}