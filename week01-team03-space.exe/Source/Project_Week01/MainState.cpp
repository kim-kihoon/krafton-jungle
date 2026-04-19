#include "MainState.h"
#include "ExampleState.h"
#include "UManager.h"

#include "StageSelectionState.h"

#include <iostream>

void MainState::OnEnter(UManager* manager)
{
	//	Current State Initialization
	manager->SetCurStage(EStage::ES_None);	//	초기 스테이지 설정 (필요에 따라 변경 가능)

	manager->PlayBGM(EBGM::EBGM_TitleScreen);
	
	uiManager = new UIManager();

	UIFrame& bgFrame = uiManager->CreateFrame("Main_Background")
		.Position(ImVec2(0, 0))
		.Size(ImVec2(1400, 1050))
		.NoTitleBar(true)
		.BackgroundColor(ImVec4(0, 0, 0, 0));
	bgFrame.SetLayer(-10);

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

	bgFrame.AddText("v.1.0.0 by GameTechLab Team 3", ImVec2(133, WindowHeight - 17), manager->GetResourceManager()->FontInfoLight);

	std::cout << "Logo and BG done" << std::endl;

	//ImGui::PopStyleVar();
	
	UIFrame& logoFrame = uiManager->CreateFrame("Main_Logo")
		.Position(ImVec2(0, 0))
		.Size(ImVec2(1400, 1050))
		.NoTitleBar(true)
		.BackgroundColor(ImVec4(0, 0, 0, 0))
		.BorderLineTransparency(0.f);

	//	For Animation
	LogoFrame = &logoFrame;

	//	Logo
	logoFrame.AddText("SPACE.EXE",
		ImVec2(WindowWidth / 2, WindowHeight / 3),
		manager->GetResourceManager()->FontLogo
		);
	
	UIFrame& btnFrame = uiManager->CreateFrame("Main_Button")
		.Position(ImVec2(0, 0))
		.Size(ImVec2(1400, 1050))
		.NoTitleBar(true)
		.BackgroundColor(ImVec4(0, 0, 0, 0)
		);

	//	Button Set
	btnFrame.AddSpriteButton9("Game Start",
		manager->GetResourceManager()->SRVButtonSprite,
		ImVec2(WindowWidth / 2, WindowHeight / 2 + 80),
		ImVec2(400, 90),
		2,
		6.0f,
		[&]() {
			bIsGameStart = true;
		}
		);

	btnFrame.AddText("Game Start",
		ImVec2(WindowWidth / 2 + 10, WindowHeight / 2 + 80),
		manager->GetResourceManager()->FontDefault
	);

	btnFrame.AddSpriteButton9("Exit",
		manager->GetResourceManager()->SRVButtonSprite,
		ImVec2(WindowWidth / 2, WindowHeight / 2 + 200),
		ImVec2(400, 90),
		2,
		6.0f,
		[]()
		{
			PostQuitMessage(0);
		}
	);

	btnFrame.AddText("Exit",
		ImVec2(WindowWidth / 2 + 10, WindowHeight / 2 + 200),
		manager->GetResourceManager()->FontDefault
	);

	btnFrame.AddSpriteButton("Info",
		manager->GetResourceManager()->SRVButtonSprite,
		ImVec2(WindowWidth - 50, WindowHeight - 50),
		ImVec2(60,60),
		27,
		//	Toggle
		[&]() {
			InfoFrame->bVisibility = !InfoFrame->bVisibility;
		}
		);

	UIFrame& infoFrame = uiManager->CreateFrame("Main_Information")
		.Position(ImVec2(0, 0))
		.Size(ImVec2(1400, 1050))
		.NoTitleBar(true)
		.BackgroundColor(ImVec4(0, 0, 0, 0)
		);

	InfoFrame = &infoFrame;
	infoFrame.bVisibility = false;

	infoFrame.AddImage(manager->GetResourceManager()->SRVLeaderBoardPanel,
		ImVec2(WindowWidth / 2 - 470, WindowHeight / 2 - 375),
		ImVec2(940, 750)
	);

	infoFrame.AddBlocker("InfoBlocker",
		ImVec2(WindowWidth / 2 - 470, WindowHeight / 2 - 375),
		ImVec2(500, 500)
	);

	infoFrame.AddSpriteButton("Info",
		manager->GetResourceManager()->SRVButtonSprite,
		ImVec2(WindowWidth - 50, WindowHeight - 50),
		ImVec2(60, 60),
		27,
		//	Toggle
		[&]() {
			InfoFrame->bVisibility = !InfoFrame->bVisibility;
		}
	);

	infoFrame.AddText("Contributors",
		ImVec2(WindowWidth / 2, WindowHeight / 2 - 290),
		manager->GetResourceManager()->FontInfoBold
	);

	infoFrame.AddText("KIM KiHoon (PM)",
		ImVec2(WindowWidth / 2, WindowHeight / 2 - 190),
		manager->GetResourceManager()->FontInfoRegular);

	infoFrame.AddText("PARK SangHyeok",
		ImVec2(WindowWidth / 2, WindowHeight / 2 - 110),
		manager->GetResourceManager()->FontInfoRegular);

	infoFrame.AddText("YANG HyunSeok",
		ImVec2(WindowWidth / 2, WindowHeight / 2 - 30),
		manager->GetResourceManager()->FontInfoRegular);

	infoFrame.AddText("HAN YoungSeo",
		ImVec2(WindowWidth / 2, WindowHeight / 2 + 50),
		manager->GetResourceManager()->FontInfoRegular);

	////여기서부터 init코드 작성
	//// 테스트용 행성(원) 생성 및 규격 설정
	//USphere testPlanet;   
	//testPlanet.SetLocation({ 0.5f, 0.0f, 0.0f });
	//testPlanet.SetScale(0.1f);
	//manager->CreateNewPlanetWorld(testPlanet);

	std::cout << "All UIs done" << std::endl;
}

IState* MainState::Update(float deltaTime, UManager* manager)
{
	nextState = this;

	// 메인 화면에서는 별다른 물리 로직이 필요 없으므로 업데이트를 생략하거나
	// 필요한 애니메이션 로직만 넣습니다.

	if (bIsGameStart) 
	{
		nextState = new StageSelectionState();
	}

	return nextState;
}

void MainState::Render(URenderer* renderer, UManager* manager)
{
	Probe* pPlayer = manager->GetProbe();
	if (pPlayer) {
		renderer->UpdateConstant(pPlayer->GetTransformMatrix());
		MeshResource* probeRes = manager->getProbeResource();
		if (probeRes->VB) {
			renderer->textureRenderPrimitive(probeRes->VB, probeRes->IB, probeRes->IndexCount, manager->GetResourceManager()->GetTexture(ImageName::ROCKET));
		}
	}

	//	Logo Animation
	if (LogoFrame)
	{
		if (OffsetY <= TopOffsetY || OffsetY >= BottomOffsetY)
		{
			DY *= -1;
		}

		OffsetY += DY;
		LogoFrame->SetPosition(ImVec2(0, OffsetY));
	}


	if (uiManager) uiManager->Render();
}

void MainState::OnExit(UManager* manager)
{
	if (uiManager) {
		delete uiManager;
		uiManager = nullptr;
	}
}