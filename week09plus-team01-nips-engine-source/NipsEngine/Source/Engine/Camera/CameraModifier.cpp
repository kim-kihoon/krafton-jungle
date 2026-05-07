#include "Engine/Camera/CameraModifier.h"

#include "Engine/Camera/PlayerCameraManager.h"

DEFINE_CLASS(UCameraModifier, UObject)

bool UCameraModifier::ModifyCamera(float DeltaTime, FCameraViewInfo& InOutView)
{
	(void)DeltaTime;
	(void)InOutView;
	return false;
}

bool UCameraModifier::ModifyPostProcess(float DeltaTime, FPostProcessSettings& InOutSettings)
{
	(void)DeltaTime;
	(void)InOutSettings;
	return false;
}

bool UCameraModifier::ModifyOverlay(float DeltaTime, FCameraOverlaySettings& InOutOverlay)
{
	(void)DeltaTime;
	(void)InOutOverlay;
	return false;
}

void UCameraModifier::AddedToCamera(APlayerCameraManager* Camera)
{
	(void)Camera;
}

void UCameraModifier::RemovedFromCamera(APlayerCameraManager* Camera)
{
	(void)Camera;
}

void UCameraModifier::EnableModifier()
{
	bEnabled = true;
}

void UCameraModifier::DisableModifier()
{
	bEnabled = false;
}
