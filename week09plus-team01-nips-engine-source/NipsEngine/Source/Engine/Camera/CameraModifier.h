#pragma once

#include "Core/CoreMinimal.h"
#include "Object/Object.h"

struct FCameraViewInfo;
struct FPostProcessSettings;
struct FCameraOverlaySettings;
class APlayerCameraManager;

class UCameraModifier : public UObject
{
public:
	DECLARE_CLASS(UCameraModifier, UObject)

	UCameraModifier() = default;
	~UCameraModifier() override = default;

	virtual bool ModifyCamera(float DeltaTime, FCameraViewInfo& InOutView);
	virtual bool ModifyPostProcess(float DeltaTime, FPostProcessSettings& InOutSettings);
	virtual bool ModifyOverlay(float DeltaTime, FCameraOverlaySettings& InOutOverlay);

	virtual void AddedToCamera(APlayerCameraManager* Camera);
	virtual void RemovedFromCamera(APlayerCameraManager* Camera);

	virtual void EnableModifier();
	virtual void DisableModifier();

	bool IsEnabled() const { return bEnabled; }
	void SetPriority(int32 InPriority) { Priority = InPriority; }
	int32 GetPriority() const { return Priority; }

	bool ShouldAutoRemove() const { return bAutoRemoveModifiers; }

private:
	bool bEnabled = true;
	bool bAutoRemoveModifiers = true;
	int32 Priority = 0;
};
