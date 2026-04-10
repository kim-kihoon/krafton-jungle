#include "EditorViewportController.h"
#include "Core/Engine.h"
#include "EditorEngine.h"
#include "Slate/SlateApplication.h"
#include "Component/CameraComponent.h"
#include "Input/InputManager.h"
#include "Input/EnhancedInputManager.h"
#include "Input/InputTrigger.h"
#include "Input/InputModifier.h"
#include "Input/InputMappingContext.h"
#include "Camera/Camera.h"
#include "Viewport/ViewportTypes.h"
FEditorViewportController::~FEditorViewportController()
{
	Cleanup();
}

void FEditorViewportController::Cleanup()
{
	if (EnhancedInput && CameraContext)
		EnhancedInput->RemoveMappingContext(CameraContext);
	delete CameraContext;
	CameraContext = nullptr;
	EnhancedInput = nullptr;
}

void FEditorViewportController::Initialize(UCameraComponent* InCameraComp, FInputManager* InInput, FEnhancedInputManager* InEnhancedInput)
{
	CameraComponent = InCameraComp;
	InputManager = InInput;
	EnhancedInput = InEnhancedInput;
	SetupInputBindings();
}

void FEditorViewportController::SetFrameDeltaTime(float DeltaTime)
{
	CurrentDeltaTime = DeltaTime;
}

void FEditorViewportController::SetActiveLocalState(FViewportLocalState* InLocalState)
{
	ActiveLocalState = InLocalState;
}


void FEditorViewportController::SetupInputBindings()
{
	CameraContext = new FInputMappingContext();


	auto& W = CameraContext->AddMapping(&MoveForwardAction, 'W');
	W.Triggers.push_back(new FTriggerDown());

	auto& S = CameraContext->AddMapping(&MoveForwardAction, 'S');
	S.Triggers.push_back(new FTriggerDown());
	S.Modifiers.push_back(new FModifierNegative()); // -1.0f

	auto& D = CameraContext->AddMapping(&MoveRightAction, 'D');
	D.Triggers.push_back(new FTriggerDown());

	auto& A = CameraContext->AddMapping(&MoveRightAction, 'A');
	A.Triggers.push_back(new FTriggerDown());
	A.Modifiers.push_back(new FModifierNegative());

	auto& E = CameraContext->AddMapping(&MoveUpAction, 'E');
	E.Triggers.push_back(new FTriggerDown());

	auto& Q = CameraContext->AddMapping(&MoveUpAction, 'Q');
	Q.Triggers.push_back(new FTriggerDown());
	Q.Modifiers.push_back(new FModifierNegative());


	EnhancedInput->AddMappingContext(CameraContext, 0);


	EnhancedInput->BindAction(&MoveForwardAction, ETriggerEvent::Triggered,
		[this](const FInputActionValue& Value) {
		if (!ActiveLocalState) return;
		
		FInputManager* Input = GEngine ? GEngine->GetInputManager() : nullptr;
		bool bIsCaptured = Input ? Input->IsMouseCaptured() : false;

		// 캡처되지 않은 상태라면 반드시 [우클릭 중] 이어야 이동 가능
		if (!bIsCaptured && (!Input || !Input->IsMouseButtonDown(FInputManager::MOUSE_RIGHT)))
		{
			return;
		}

		{
			const float Speed = CameraComponent ? CameraComponent->GetCamera()->GetSpeed() : 5.0f;
			const FVector Forward = ActiveLocalState->Rotation.Vector().GetSafeNormal();
			ActiveLocalState->Position += Forward * (Value.Get() * Speed * CurrentDeltaTime);
		}
	});

	EnhancedInput->BindAction(&MoveRightAction, ETriggerEvent::Triggered,
		[this](const FInputActionValue& Value) {
		if (!ActiveLocalState) return;
		
		FInputManager* Input = GEngine ? GEngine->GetInputManager() : nullptr;
		bool bIsCaptured = Input ? Input->IsMouseCaptured() : false;

		// 캡처되지 않은 상태라면 반드시 [우클릭 중] 이어야 이동 가능
		if (!bIsCaptured && (!Input || !Input->IsMouseButtonDown(FInputManager::MOUSE_RIGHT)))
		{
			return;
		}

		{
			const float Speed = CameraComponent ? CameraComponent->GetCamera()->GetSpeed() : 5.0f;
			const FVector Forward = ActiveLocalState->Rotation.Vector().GetSafeNormal();
			const FVector Right = FVector::CrossProduct(FVector(0.f, 0.f, 1.f), Forward).GetSafeNormal();
			ActiveLocalState->Position += Right * (Value.Get() * Speed * CurrentDeltaTime);
		}
	});

	EnhancedInput->BindAction(&MoveUpAction, ETriggerEvent::Triggered,
		[this](const FInputActionValue& Value) {
		if (!ActiveLocalState) return;
		
		FInputManager* Input = GEngine ? GEngine->GetInputManager() : nullptr;
		bool bIsCaptured = Input ? Input->IsMouseCaptured() : false;

		// 캡처되지 않은 상태라면 반드시 [우클릭 중] 이어야 이동 가능
		if (!bIsCaptured && (!Input || !Input->IsMouseButtonDown(FInputManager::MOUSE_RIGHT)))
		{
			return;
		}

		{
			const float Speed = CameraComponent ? CameraComponent->GetCamera()->GetSpeed() : 5.0f;
			ActiveLocalState->Position += FVector(0.f, 0.f, 1.f) * (Value.Get() * Speed * CurrentDeltaTime);
		}
	});


}
