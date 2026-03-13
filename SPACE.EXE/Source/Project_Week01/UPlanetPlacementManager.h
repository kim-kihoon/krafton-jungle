#pragma once

/*
	반드시 UManager의 멤버로 두고 사용해야 합니다.
	- Manager를 Interface로 두고 UIManager, Renderer와 소통해야 함
	- 설치 자체는 UManager에서 처리해야 함 (Manager에 vector에 Selected를 push)
	- Renderer에서 그릴 때 EPS_Select 상태라면, Manager planet list에 있지 않아도 Selected를 Render
	-> 단, Opacity와 Color variation 부여 (이는 Static object들과의 Collision + Border 감지)
*/

#include "USphere.h"

class UManager;

//#include "UManager.h"

enum class EPlacementState
{
	EPS_None,
	EPS_Select
};

class UPlanetPlacementManager
{
private:
	UManager* Manager;
	USphere* SelectedPlanet;
	EPlacementState CurState;
	
public:
	

private:

public:
	UPlanetPlacementManager(UManager * manager) : SelectedPlanet(nullptr), Manager(manager), 
		CurState(EPlacementState::EPS_None)
	{
		
	}
	~UPlanetPlacementManager() = default;

	void OnPlanetButtonClick(USphere * in);
	void SetSelectedPlanet(USphere* in);
	void ResetSelectedPlanet();

	USphere* GetSelectedPlanet() const;

	//	함수 명 마음에 안 듦 -> 나중에 바꾸기
	bool IsPlacable();

	/* Game Logic */
	void Update(float deltaTime);
};

