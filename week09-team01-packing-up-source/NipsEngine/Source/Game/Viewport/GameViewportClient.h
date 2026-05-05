#pragma once

#include "Engine/Input/InputRouter.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "Engine/Viewport/ViewportClient.h"
#include "Game/Input/GamePlayerController.h"

class FWindowsWindow;
class UCameraComponent;
class UWorld;

class FGameViewportClient : public FViewportClient
{
public:
	~FGameViewportClient() override;

	void Initialize(FWindowsWindow* InWindow) override;
	void SetViewportSize(float InWidth, float InHeight) override;
	void Tick(float DeltaTime) override;
	void LateTick(float DeltaTime);
	void BuildSceneView(FSceneView& OutView) const override;

	void SetWorld(UWorld* InWorld);
	UWorld* GetFocusedWorld() const { return World; }

	void SetCamera(UCameraComponent* InCamera);
	UCameraComponent* GetCamera() const { return ActiveCamera; }

	FViewportCamera& GetFreeCamera() { return FreeCamera; }
	const FViewportCamera& GetFreeCamera() const { return FreeCamera; }

	FInputRouter& GetInputRouter() { return InputRouter; }
	const FInputRouter& GetInputRouter() const { return InputRouter; }
	FGamePlayerController& GetPlayerController() { return PlayerController; }
	const FGamePlayerController& GetPlayerController() const { return PlayerController; }

private:
	void ToggleInteractionMode();

private:
	UWorld* World = nullptr;
	UCameraComponent* ActiveCamera = nullptr;
	FViewportCamera FreeCamera;
	FGamePlayerController PlayerController;
	FInputRouter InputRouter;
	bool bInputActive = true;
};
