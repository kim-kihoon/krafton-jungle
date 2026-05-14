#pragma once

#include "Engine/Runtime/Engine.h"

#include <memory>

class FGameViewportClient;

class UGameEngine : public UEngine
{
public:
	DECLARE_CLASS(UGameEngine, UEngine)

	UGameEngine();
	~UGameEngine() override;

	void Init(FWindowsWindow* InWindow) override;
	void Shutdown() override;
	void Tick(float DeltaTime) override;
	void OnWindowResized(uint32 Width, uint32 Height) override;

	FGameViewportClient* GetGameViewport() const { return GameViewport.get(); }

private:
	void LoadStartupScene();
	bool LoadGameScene(const FString& ScenePath);
	void StartMainGame();
	void ExitToTitle();

private:
	std::unique_ptr<FGameViewportClient> GameViewport;
};
