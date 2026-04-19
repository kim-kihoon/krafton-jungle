#include "UPlanetPlacementManager.h"

#include "UManager.h"

#include <iostream>

void UPlanetPlacementManager::OnPlanetButtonClick(USphere* in)
{
	SetSelectedPlanet(in);
}

void UPlanetPlacementManager::SetSelectedPlanet(USphere* in)
{
	SelectedPlanet = in;
	CurState = EPlacementState::EPS_Select;
}

void UPlanetPlacementManager::ResetSelectedPlanet()
{
	SelectedPlanet = nullptr;
	CurState = EPlacementState::EPS_None;
}

USphere* UPlanetPlacementManager::GetSelectedPlanet() const
{
	return SelectedPlanet;
}


// true면
bool UPlanetPlacementManager::IsPlacable()
{
	return (CurState == EPlacementState::EPS_Select) && !(Manager->GetInputManager()->IsMouseHoveringUI());
}

void UPlanetPlacementManager::Update(float deltaTime)
{
	//	RunState 검사는 UManager에서 수행

	switch (CurState)
	{
	//	 아무것도 선택 안 됨
	case EPlacementState::EPS_None:

		break;
	//	Planet 선택 상태
	case EPlacementState::EPS_Select:
		//	Set

		SelectedPlanet->SetLocation(Manager->GetInputManager()->GetMousePosition());

		//std::cout << "Selected Planet Location: (" << SelectedPlanet->GetLocation().x << ", "
		//	<< SelectedPlanet->GetLocation().y << ", "
		//	<< SelectedPlanet->GetLocation().z << ")" << std::endl;

		if (Manager->GetInputManager()->IsMouseClicked(0))
		{
			Manager->CreateNewPlanetWorld(*SelectedPlanet);
			ResetSelectedPlanet();
		}
		//	Reset
		else if (Manager->GetInputManager()->IsMouseClicked(1))
		{
			ResetSelectedPlanet();
		}
		break;
	default:
		break;
	}
}