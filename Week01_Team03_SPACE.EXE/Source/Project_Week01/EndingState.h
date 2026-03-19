#pragma once
#include <fstream>
#include <string>
#include <sstream>


#include "IState.h"
#include "InGameReadyState.h"
#include "StageSelectionState.h"

class UManager;

class EndingState : public IState
{
private:
	const std::string FileName = "ranking.txt";
	std::vector<std::tuple<unsigned int, std::string, unsigned int>> ScoreList;
	UResourceManager* ResourceManager;

	
	//void EndingInit(bool bIsClear, unsigned int score, int);
	//void DisplayScore(std::string name, unsigned int score, int);
	void LoadScore();
	void SaveScore();
	void ShutDownGame();
	
	//void ClearGameObjects();
	//void ProgressStage();

	bool goToMain = false;
	bool retry = false;
	bool stageSelect = false;

	bool bIsClear = false;
	unsigned int finalScore = 0;
	int currentStage = 0;

	UManager* Manager = nullptr;

public:
	EndingState() = default;
	virtual ~EndingState() = default;
	void OnEnter(UManager* manager) override;
	IState* Update(float deltaTime, UManager* manager) override;
	void Render(URenderer* renderer, UManager* manager) override;
	void OnExit(UManager* manager) override;

	void OnStageResult(bool bSuccess, float remainTime, int currentStage);

	static std::string RandomNameGenerator();
};