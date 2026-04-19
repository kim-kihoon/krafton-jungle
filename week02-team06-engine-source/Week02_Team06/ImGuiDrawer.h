#pragma once

class UScene;
class FEditorViewportClient;
class UWorld;
class AActor;

struct FSpawnParameters
{
	bool bOverrideUUID = false;
	int Count;
	uint32 UUID;
	FVector Location;
	FVector Rotation;
	FVector Scale;
	FString PrimitiveType;
};

class UImGuiDrawer
{
public:
	bool bShowConsole = true;

	FSpawnParameters actorParameters;

	FVector ActorLocation;
	FVector ActorRotation;
	FVector ActorScale;

	UWorld* World;

	AActor* SelectedTarget;

	UImGuiDrawer();
	~UImGuiDrawer() {};

	void Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context, UWorld* world);

	void BeginFrame();
	void EndFrame();

	void UpdateUI(FEditorViewportClient* ViewportClient);

	void Release();

private:
	void DrawSpawnPanel();
	void DrawSceneControlPanel();
	void DrawPrimitiveDataPanel();

	void DrawCameraPanel(FEditorViewportClient* ViewportClient);
};