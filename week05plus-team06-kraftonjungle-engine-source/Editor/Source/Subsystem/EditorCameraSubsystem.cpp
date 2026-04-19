#include "Subsystem/EditorCameraSubsystem.h"

#include "Component/CameraComponent.h"
#include "Input/EnhancedInputManager.h"
#include "Input/InputManager.h"
#include "Object/ObjectFactory.h"
#include "Pawn/EditorCameraPawn.h"
#include "Scene/Level.h"
#include "World/World.h"

FEditorCameraSubsystem::~FEditorCameraSubsystem()
{
	Shutdown();
}

bool FEditorCameraSubsystem::Initialize(UWorld* ActiveWorld, FInputManager* InInputManager, FEnhancedInputManager* InEnhancedInputManager)
{
	Shutdown();

	if (ActiveWorld == nullptr)
	{
		return false;
	}

	EditorPawn = FObjectFactory::ConstructObject<AEditorCameraPawn>(nullptr, "EditorCameraPawn");
	if (EditorPawn == nullptr)
	{
		return false;
	}

	ActiveWorld->SetActiveCameraComponent(EditorPawn->GetCameraComponent());
	ViewportController.Initialize(
		EditorPawn->GetCameraComponent(),
		InInputManager,
		InEnhancedInputManager);
	return true;
}

void FEditorCameraSubsystem::Shutdown()
{
	if (EditorPawn)
	{
		EditorPawn->Destroy();
		EditorPawn = nullptr;
	}

	ViewportController.Cleanup();
}

void FEditorCameraSubsystem::PrepareFrame(UWorld* ActiveWorld, ULevel* ActiveLevel, float DeltaTime)
{
	SyncActiveCamera(ActiveWorld, ActiveLevel);
	ViewportController.SetFrameDeltaTime(DeltaTime);
}

FEditorViewportController* FEditorCameraSubsystem::GetViewportController()
{
	return &ViewportController;
}

void FEditorCameraSubsystem::SyncActiveCamera(UWorld* ActiveWorld, ULevel* ActiveLevel)
{
	if (EditorPawn == nullptr || ActiveWorld == nullptr || ActiveLevel == nullptr || !ActiveLevel->IsEditorLevel())
	{
		return;
	}

	UCameraComponent* EditorCamera = EditorPawn->GetCameraComponent();
	if (ActiveWorld->GetActiveCameraComponent() != EditorCamera)
	{
		ActiveWorld->SetActiveCameraComponent(EditorCamera);
	}
}
