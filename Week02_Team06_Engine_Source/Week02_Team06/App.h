#pragma once

class UWindow;
class URenderer;
class UGraphics;
class USceneManager;
class UImGuiDrawer;
class UResourceManager;
class UWorld;
class FEditorViewportClient;

class UApp
{
public:
	UApp() = default;
	~UApp() = default;

public:
	bool Initialize(HINSTANCE hInstance);
	void Run();
	void Release();

	URenderer* GetRenderer() const { return Renderer; }
	UResourceManager* GetResourceManager() const { return ResourceManager; }
	FEditorViewportClient* GetViewportClient() const { return ViewportClient; }

private:
	const int				   TargetFrame = { 60 };
	float					   DeltaTime = { 1.f / TargetFrame };

	UWindow* Window     = { nullptr };
	UGraphics* Graphics = { nullptr };

	URenderer* Renderer = { nullptr };
	UWorld* World = { nullptr };

	FEditorViewportClient* ViewportClient;

	UImGuiDrawer* ImGuiDrawer = { nullptr };

	UResourceManager* ResourceManager = { nullptr };
};
