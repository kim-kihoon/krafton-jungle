#include "EndingState.h"
#include "UManager.h"

#include "LoadingState.h"
#include "MainState.h"

#include <iostream>

//EndingState::EndingState() : FileName("ranking.txt")
//{
//	nextState = this;
//}

void EndingState::OnEnter(UManager* manager)
{
	Manager = manager;
	OnStageResult(manager->GetSuccess(), manager->GetRemainTimer(), manager->GetCurStageInt());


	LoadScore();

	ResourceManager = manager->GetResourceManager();
	
	uiManager = new UIManager();

	UIFrame& bgFrame = uiManager->CreateFrame("EndingState_Background")
		.Position(ImVec2(0, 0))
		.Size(ImVec2(1400, 1050))
		.NoTitleBar(true)
		.BackgroundColor(ImVec4(0, 0, 0, 0))
		.NoBringToFrontOnFocus(true);
	bgFrame.SetLayer(0);


	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
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

	UIFrame& scoreFrame = uiManager->CreateFrame("EndingState_Score")
		.Position(ImVec2(175, 100))
		.Size(ImVec2(1050, 750))
		.NoTitleBar(true)
		.BackgroundColor(ImVec4(0.f, 0.f, 0.f, 0.f))
		.BorderLineTransparency(0.f);
	scoreFrame.SetLayer(10);
	//scoreFrame.NoBringToFrontOnFocus(true);

	scoreFrame.AddImage(manager->GetResourceManager()->SRVLeaderBoardPanel,
		ImVec2(0, 0),
		ImVec2(1050, 850)
	);

	scoreFrame.AddText("Ranking", ImVec2(1050/2.f, 95), ResourceManager->FontLogoSmall);

	scoreFrame.AddText("Player Name", ImVec2(1050 * 0.3f, 200), ResourceManager->FontInfoRegular);
	scoreFrame.AddText("Score", ImVec2(1050 * 0.7f, 200), ResourceManager->FontInfoRegular);

	const int displayCount = 10;
	int displayed = 0;
	int curStage = manager->GetCurStageInt();

	for (size_t i = 0; i < ScoreList.size(); i++)
	{
		if (std::get<0>(ScoreList[i]) != curStage)
			continue;

		auto name = std::get<1>(ScoreList[i]);
		auto score = std::get<2>(ScoreList[i]);

		//std::cout
		//	<< "Stage: " << std::get<0>(ScoreList[i])
		//	<< " Name: " << name
		//	<< " Score: " << score
		//	<< std::endl;

		scoreFrame.AddText(name, ImVec2(1050 * 0.3f, 250 + displayed * 35.f), ResourceManager->FontInfoLight);
		scoreFrame.AddText(std::to_string(score), ImVec2(1050 * 0.7f, 250 + displayed * 35.f), ResourceManager->FontInfoLight);

		displayed++;

		if (displayed >= displayCount)
			break;
	}

	scoreFrame.AddText("Your Name : " + manager->GetPlayerName() +"    |    Score : " + std::to_string(manager->GetScore()),
		ImVec2(1050 / 2.f, 650),
		ResourceManager->FontInfoLight);

	scoreFrame.AddText("Thank you for playing!",
		ImVec2(1050 / 2.f, 700),
		ResourceManager->FontInfoRegular);

	auto buttonFrameWidth = 1050;
	auto buttonFrameHeight = 1050;
	UIFrame& buttonFrame = uiManager->CreateFrame("EndingState_Button")
		.Position(ImVec2(175, 830))
		.Size(ImVec2(buttonFrameWidth, buttonFrameHeight))
		.NoTitleBar(true)
		.BackgroundColor(ImVec4(0.f, 0.f, 0.f, 0.f))
		.BorderLineTransparency(0.f);
	buttonFrame.SetLayer(10);

	auto buttonSize = 70.f;
	auto buttonYPos = 90.f;
	auto gap = 120;

	buttonFrame.AddSpriteButton("GoToMain", 
		ResourceManager->SRVButtonSprite, 
		ImVec2(buttonFrameWidth * 0.5f - gap, buttonYPos), 
		ImVec2(buttonSize, buttonSize), 
		17, 
		[&] {goToMain = true; }
	);
	buttonFrame.AddSpriteButton("Retry", 
		ResourceManager->SRVButtonSprite, 
		ImVec2(buttonFrameWidth * 0.5f, buttonYPos), 
		ImVec2(buttonSize, buttonSize), 
		11, 
		[&] {retry = true; }
	);
	buttonFrame.AddSpriteButton("StageMenu", 
		ResourceManager->SRVButtonSprite, 
		ImVec2(buttonFrameWidth * 0.5f + gap, buttonYPos), 
		ImVec2(buttonSize, buttonSize), 
		12, 
		[&] {stageSelect = true; }
	);
}

IState* EndingState::Update(float deltaTime, UManager* manager)
{
	if (goToMain)
	{
		return new MainState();
	}
	else if (retry)
	{
		return new LoadingState();
	}
	else if (stageSelect)
	{
		return new StageSelectionState();
	}

	return this;
}

void EndingState::Render(URenderer* renderer, UManager* manager)
{
	// 엔딩 화면 UI 출력
	if (uiManager) uiManager->Render();
}

void EndingState::OnExit(UManager* manager)
{
	SaveScore();

	if (uiManager) {
		delete uiManager;
		uiManager = nullptr;
	}
}


std::string EndingState::RandomNameGenerator()
{
	const std::string charSet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	const int appendLength = 5;

	std::random_device rd;
	std::mt19937 mt(rd());
	std::uniform_int_distribution<int> dist(0, charSet.size() - 1);

	std::string name = "User_";

	for (int i = 0; i < appendLength; i++)
	{
		name += charSet[dist(mt)];
	}

	return name;
}

//void EndingState::ClearGameObjects()
//{
//	if (Player)
//	{
//		delete Player;
//		Player = nullptr;
//	}
//
//	PlanetList.clear();
//
//	//	Reserve size
//	PlanetList.reserve(PlanetListReservedSize);
//}
//
//void EndingState::ProgressStage()
//{
//	//switch (CurAvailableStage)
//	//{
//	//case EStage::ES_Stage1:
//	//	CurAvailableStage = EStage::ES_Stage2;
//	//	break;
//
//	//case EStage::ES_Stage2:
//	//	CurAvailableStage = EStage::ES_Stage3;
//	//	break;
//
//	//case EStage::ES_Stage3:
//	//	//	Do nothing
//	//	break;
//	//}
//
//	ClearGameObjects();
//}

// 결과 화면 시 호출될 함수
void EndingState::OnStageResult(bool bSuccess, float remainTime, int currentStage)
{
	//ProgressStage();

	unsigned int finalScore = 0;

	if (bSuccess) 
		finalScore = (unsigned int)(remainTime * remainTime);
	else 
		finalScore = 0;

	//EndingInit(bSuccess, finalScore, currentStage);

	bIsClear = bSuccess;
	this->finalScore = finalScore;
	this->currentStage = currentStage;

	if(bIsClear)
	{		
		switch (Manager->GetCurAvailableStage())
		{
			case EStage::ES_Stage1:
				Manager->SetCurAvailableStage(EStage::ES_Stage2);
				break;
			case EStage::ES_Stage2:
				Manager->SetCurAvailableStage(EStage::ES_Stage3);
				break;
			case EStage::ES_Stage3:
				//	Do nothing
				break;
		}
	}
	else
	{
		//Manager->PlaySFX(ESFX::ESFX_Fail);
	}
}

//void EndingState::EndingInit(bool bIsClear, unsigned int score, int currentStage)
//{
//	//ClearGameObjects(); InGameRunOnExit에서
//
//	//if (bIsClear)
//	//{
//	//	//m_SoundMgr.PlaySFX(ESFX::ESFX_Clear);
//	//}
//	//else
//	//{
//	//	//m_SoundMgr.PlaySFX(ESFX::ESFX_Fail);
//	//}
//
//
//
//	//	Score Display
//	DisplayScore(name, score, currentStage);
//}

//void EndingState::DisplayScore(std::string name, unsigned int score, int currentStage)
//{
//	//	List에 포함해서 보여주기 (포함 후 정렬 -> 보여주기)
//	LoadScore();
//
//
//
//	//	TODO : Imgui를 통해 display
//}

void EndingState::LoadScore()
{
	ScoreList.clear();

	std::ifstream ifs(FileName);
	std::string line;


	while (std::getline(ifs, line))
	{
		std::stringstream ss(line);

		unsigned int stage;
		std::string name;
		unsigned int score;

		char dummy;

		//	구분자 : ,
		ss >> stage;
		ss >> dummy;	//	첫 번째 ',' 가짐
		std::getline(ss, name, ',');
		ss >> score;

		ScoreList.push_back({ stage, name, score });
	}

	ifs.close();

	//	Name이 없는 경우 랜덤으로 생성
	if(Manager->GetPlayerName()=="")
		Manager->SetPlayerName(RandomNameGenerator());

	//std::cout << "Current Stage: " << currentStage << " Player Name: " << Manager->GetPlayerName() << " Score: " << finalScore << std::endl;

	ScoreList.push_back({ currentStage, Manager->GetPlayerName(), finalScore});

	std::sort(ScoreList.begin(), ScoreList.end(), [](const auto& a, const auto& b)
		{
			//	다른 스테이지일 경우 Sort하지 않음
			//	Display 시에도 Stage로 filtering하면 됨
			if (std::get<0>(a) != std::get<0>(b)) return false;

			return std::get<2>(a) > std::get<2>(b);
		}
	);

	//ScoreList.push_back({ stageInfo, name, score });

	//std::sort(ScoreList.begin(), ScoreList.end(), [](const auto& a, const auto& b)
	//	{
	//		//	다른 스테이지일 경우 Sort하지 않음
	//		//	Display 시에도 Stage로 filtering하면 됨
	//		if (std::get<0>(a) != std::get<0>(b)) return false;

	//		return std::get<2>(a) < std::get<2>(b);
	//	}
	//);
}


void EndingState::ShutDownGame()
{
	SaveScore();



	//	1. Heap 해제
	ResourceManager->Release();
}

void EndingState::SaveScore()
{
	std::ofstream ofs(FileName);
	// ScoreList 튜플 구조 <Stage, Name, Score>에 맞춰 저장합니다.
	for (const auto& s : ScoreList)
	{
		ofs << std::get<0>(s) << "," << std::get<1>(s) << "," << std::get<2>(s) << "\n";
	}
	ofs.close();
}