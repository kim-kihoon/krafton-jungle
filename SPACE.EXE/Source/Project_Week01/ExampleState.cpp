//#include "ExampleState.h"
//#include "MainState.h"
//
//void ExampleState::OnEnter()
//{
//	uiManager = new UIManager();
//
//
//	UIFrame& frame1 = uiManager->CreateFrame("ExampleState")
//		.Position(ImVec2(100, 100))
//		.Size(ImVec2(200, 300));
//
//	frame1.AddButton("BackToMainState", ImVec2(20, 20), ImVec2(20, 20), [&] {
//		nextState = this->BackToMainState();
//		});
//
//}
//
//IState* ExampleState::Update(URenderer* renderer)
//{
//	nextState = this;
//
//
//	//조건 분기
//	//다음 State를 return 하는 함수를 작성. uiFrame 버튼에 람다로 등록.
//
//
//	if (uiManager)
//		uiManager->Render();
//	return nextState;
//}
//
//void ExampleState::OnExit()
//{
//	delete uiManager;
//}
//
//IState* ExampleState::BackToMainState()
//{
//	return new MainState();
//}