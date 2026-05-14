#pragma once

#include "Engine/Camera/CameraModifier.h"

#ifndef WITH_LUA
#define WITH_LUA 0
#endif

#if WITH_LUA
#include <memory>
#include <sol/sol.hpp>
#endif

class APlayerCameraManager;

class ULuaCameraModifier : public UCameraModifier
{
public:
	DECLARE_CLASS(ULuaCameraModifier, UCameraModifier)

	~ULuaCameraModifier() override;

	void SetScriptPath(const FString& InScriptPath);
	const FString& GetScriptPath() const { return ScriptPath; }
	void SetActionSourceName(const FString& InSourceName) { ActionSourceName = InSourceName; }
	const FString& GetActionSourceName() const { return ActionSourceName; }
	bool ReloadScript();
	void UnloadScript();
	bool IsScriptLoaded() const { return bScriptLoaded; }
	const FString& GetLastScriptError() const { return LastScriptError; }

	void AddedToCamera(APlayerCameraManager* Camera) override;
	void RemovedFromCamera(APlayerCameraManager* Camera) override;

	bool ModifyCamera(float DeltaTime, FCameraViewInfo& InOutView) override;
	bool ModifyPostProcess(float DeltaTime, FPostProcessSettings& InOutSettings) override;
	bool ModifyOverlay(float DeltaTime, FCameraOverlaySettings& InOutOverlay) override;

private:
	bool ApplyDataDrivenCamera(float DeltaTime, FCameraViewInfo& InOutView);
	bool ApplyDataDrivenPostProcess(float DeltaTime, FPostProcessSettings& InOutSettings);
	bool ApplyDataDrivenOverlay(float DeltaTime, FCameraOverlaySettings& InOutOverlay);
	bool ProcessDataDrivenActions();
	bool CallModifierFunction(const char* FunctionName, float DeltaTime, FCameraViewInfo& InOutView);
	bool CallModifierFunction(const char* FunctionName, float DeltaTime, FPostProcessSettings& InOutSettings);
	bool CallModifierFunction(const char* FunctionName, float DeltaTime, FCameraOverlaySettings& InOutOverlay);
	void SetLastScriptError(const FString& Error);

private:
	FString ScriptPath;
	FString ActionSourceName;
	FString LastScriptError;
	APlayerCameraManager* CameraOwner = nullptr;
	bool bScriptLoaded = false;

#if WITH_LUA
	std::unique_ptr<sol::state> LuaState;
	sol::table ModifierDataTable;
#endif
	bool bHasModifierDataTable = false;
	float ElapsedTime = 0.0f;
	TArray<FString> TriggeredActionKeys;
};
