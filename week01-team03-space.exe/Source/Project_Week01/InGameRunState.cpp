#include "InGameRunState.h"
#include "UManager.h" // 매니저의 자원 접근을 위해 포함

#include "MainState.h"
#include "InGameReadyState.h"
#include "LoadingState.h"

#include<iostream>

void InGameRunState::OnEnter(UManager* manager)
{
	uiManager = new UIManager();

	UIFrame& bgFrame = uiManager->CreateFrame("RunState_MainState")
		.Position(ImVec2(0, 0))
		.Size(ImVec2(1400, 1050))
		.NoTitleBar(true)
		.BackgroundColor(ImVec4(0, 0, 0, 0));


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

	UIFrame& HUDFrame = uiManager->CreateFrame("RunState_Ready Phase")
		.Position(ImVec2(0, 0))
		.Size(ImVec2(WindowWidth, WindowHeight))
		.NoTitleBar(true)
		.BackgroundColor(ImVec4(0, 0, 0, 0));

	HUDFrame.AddImage(manager->GetResourceManager()->SRVInGamePanel,
		ImVec2(WindowWidth * 3 / 4.f, 0),
		ImVec2(WindowWidth / 4, WindowHeight)
	);

	HUDFrame.AddSelectableText("Simulation",
		"Simulating...",
		ImVec2(WindowWidth * 3 / 4.f + 175, 400),
		manager->GetResourceManager()->FontDefaultVerySmall);

	HUDFrame.AddSelectableText("Timer", "Remain Time : " + std::to_string(manager->GetRemainTimer()),
		ImVec2(WindowWidth * 3 / 4.f + 175, 500),
		manager->GetResourceManager()->FontInfoRegularSmall);


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

	hudFrame = &HUDFrame;

	manager->GetProbe()->ResetTrail();
}


bool InGameRunState::borderCheck(UManager* manager)
{
	Probe* player = manager->GetProbe();
	FVector location = player->GetLocation();
	float radius = player->GetRadius();

	if (-1.7f + radius > location.x or 1.2f - radius < location.x or -1.7f + radius > location.y or 1.7 - radius < location.y)
	{
		return true;
	}
	
	return false;
}

bool InGameRunState::goalCheck(UManager* manager)
{

	Probe* player = manager->GetProbe();
	Goal* goal = manager->getGoal();

	FVector playerLoc = player->GetLocation();
	FVector goalLoc = goal->GetLocation();
	float stand = (playerLoc - goalLoc).Size();
	if (stand - 0.1f <= player->GetRadius() + goal->GetRadius())
	{
		return true;
	}

	return false;
}

IState* InGameRunState::Update(float deltaTime, UManager* manager)
{
	nextState = this;

	float remainTime = manager->GetRemainTimer(); // Getter 필요

	remainTime = remainTime - deltaTime;
	remainTime = max(0.f, remainTime);
	manager->SetRemainTimer(remainTime); // Setter 필요

	auto textInfo = hudFrame->GetSelectableText("Timer");
	auto remainTimestr = std::to_string(manager->GetRemainTimer()).substr(0, 4); //문자열에서 2자리수.소수점 1자리 => 4자리
	textInfo->text = "Remain Time : " + remainTimestr;

	if (remainTime <= 0.f)
	{
		manager->SetSuccess(false);
		manager->PlaySFX(ESFX::ESFX_Fail);

		//std::cout << "Time out" << std::endl;

		EndingState* endingState = new EndingState();
		//endingState->OnStageResult(false, manager->GetRemainTimer(), manager->GetCurStageInt());
		return endingState;
	}

	auto simulationTextInfo = hudFrame->GetSelectableText("Simulation");
	SimulationTextCounter += deltaTime;
	if (SimulationTextCounter >= SimulationTextDuration)
	{
		if(simulationTextInfo->text == "Simulating...")
			simulationTextInfo->text = "Simulating";
		else if (simulationTextInfo->text == "Simulating")
			simulationTextInfo->text = "Simulating.";
		else if (simulationTextInfo->text == "Simulating.")
			simulationTextInfo->text = "Simulating..";
		else
			simulationTextInfo->text = "Simulating...";
		SimulationTextCounter = 0.f;
	}

	auto planetList = manager->GetPlanetList();
	auto player = manager->GetProbe();
	auto stageInfoList = manager->GetStageInfoList();   
	auto curStage = manager->GetCurStage();

	// 0. 완화 상수 설정
	const float epsilon = 0.2f;
	const float epsilonSq = epsilon * epsilon;

	for (auto p : planetList)
	{
		// 1. 방향 벡터 계산
		FVector direction = p.GetLocation() - player->GetLocation();

		// 2. SizeSquared 직접 계산 (x*x + y*y + z*z)
		// sqrt 연산을 거치지 않으므로 훨씬 빠르고 정밀합니다.
		float distSq = (direction.x * direction.x) + (direction.y * direction.y) + (direction.z * direction.z);

		// 0으로 나누기 방지 가드
		if (distSq < 1e-6f) continue;

		// 3. 가속도 계산 (소프닝 팩터 적용)
		float accMag = (GravitationalConstant * p.GetMass()) / (distSq + epsilonSq);

		// 4. 방향 벡터 적용을 위해 실제 거리 계산 (여기서만 sqrt 사용)
		float dist = sqrtf(distSq);
		FVector unitDir = direction / dist;
		FVector accVec = unitDir * accMag;

		// 3. 속도 업데이트
		player->SetVelocity(player->GetVelocity() + accVec * deltaTime);
	}

	auto pos = player->GetLocation();
	auto vel = player->GetVelocity();

	auto curLocation = player->GetLocation();
	player->SetLocation(curLocation+ player->GetVelocity() * deltaTime);
	//manager->GetProbe()->TryAddTrail(vel.Size() * vel.Size() * 2);
	manager->GetProbe()->TryAddTrail();

	FVector pLoc = player->GetLocation();

	//	Collision between Probe and Planets
	for (const auto& p : planetList)
	{
		float dist = (p.GetLocation() - pLoc).Size();
		if (dist < p.GetRadius() + player->GetRadius())
		{
			player->SetColliding(true);
			manager->SetSuccess(false);

			EndingState* endingState = new EndingState();
			//endingState->OnStageResult(false, manager->GetRemainTimer(), manager->GetCurStageInt());

			manager->PlaySFX(ESFX::ESFX_Fail);
			return new EndingState();
		}
	}

	//collision border with rocket
	if (borderCheck(manager))
	{
		EndingState* endingState = new EndingState();
		//endingState->OnStageResult(false, manager->GetRemainTimer(), manager->GetCurStageInt());

		manager->SetSuccess(false);
		manager->PlaySFX(ESFX::ESFX_Fail); 
		return endingState;
	}


	if (bGoToMain)
	{
		return new MainState();
		//return new EndingState();
	}
	else if (bGoToRetry)
	{

		//std::cout << "bGoToRetry" << std::endl;
		return new LoadingState();
	}

	if (goalCheck(manager))
	{
		manager->SetSuccess(true);
		manager->PlaySFX(ESFX::ESFX_Clear);
		return new EndingState();
	}

	return nextState;

}

void InGameRunState::Render(URenderer* renderer, UManager* manager)
{
	if (uiManager) uiManager->Render();

	renderer->UpdateConstant(manager->getGoal()->GetTransformMatrix());
	MeshResource* goal = manager->getGoalResource();
	if (goal != nullptr) {
		renderer->textureRenderPrimitive(goal->VB, goal->IB, goal->IndexCount, manager->GetResourceManager()->GetTexture(ImageName::GOAL));
	}
	// (1) 플레이어(Probe) 렌더링

	MeshResource* sphereRes = manager->getSphereResource();
	if (sphereRes->VB != nullptr)
	{
		for ( auto& planet : manager->GetPlanetList())
		{
			renderer->UpdateConstant(planet.GetTransformMatrix());
			//renderer->indexRenderPrimitive(sphereRes->VB, sphereRes->IB, sphereRes->IndexCount);
			renderer->textureRenderPrimitive(sphereRes->VB, sphereRes->IB, sphereRes->IndexCount, manager->GetResourceManager()->GetTexture(planet.getImageName()));
		}

		for (auto& trail : manager->GetProbe()->GetTrails()) 
		{
			renderer->UpdateConstant(trail.GetTransformMatrix());
			renderer->textureRenderPrimitive(sphereRes->VB, sphereRes->IB, sphereRes->IndexCount, manager->GetResourceManager()->GetTexture("JustWhite"));
		}
	}

	Probe* pPlayer = manager->GetProbe();
	if (pPlayer != nullptr)
	{
		renderer->UpdateConstant(pPlayer->GetTransformMatrix());
		MeshResource* probeRes = manager->getProbeResource();
		if (probeRes->VB != nullptr)
		{
			//renderer->indexRenderPrimitive(probeRes->VB, probeRes->IB, probeRes->IndexCount);
			renderer->textureRenderPrimitive(probeRes->VB, probeRes->IB, probeRes->IndexCount, manager->GetResourceManager()->GetTexture(ImageName::ROCKET));
		}
	}
	
}

void InGameRunState::OnExit(UManager* manager)
{
	if (uiManager) {
		delete uiManager;
		uiManager = nullptr;
	}
}
