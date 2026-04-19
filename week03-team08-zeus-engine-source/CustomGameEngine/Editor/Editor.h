#pragma once

#include <windows.h>
#include <d3d11.h>
#include "EngineTypes.h"
#include "MulticastDelegate.h"

struct EditorControlPanel;
struct EditorConsole;
struct EditorPropertyPanel;
struct EditorStatPanel;
struct EditorToolbar;
struct EditorSceneManager;
class USceneComponent;
class UCameraComponent;
class URenderer;
class EditorConfig;

DECLARE_MULTICAST_DELEGATE(FOnEditorConfigLoaded, const EditorConfig&);
DECLARE_MULTICAST_DELEGATE(FOnEditorConfigSaveReady, EditorConfig&);

class Editor
{
public:
	Editor();
	~Editor();

public:
	bool Initialize(HWND hWnd, ID3D11Device* device, ID3D11DeviceContext* deviceContext);
	void BeginFrame();
	void DrawUI();
	void Finalize();

	void SaveEditorConfig();
	void LoadConfig();

	void SetRenderer(URenderer* renderer) { Renderer = renderer; }
	void SetCamera(UCameraComponent* camera) { Camera = camera; }

	void SelectComponent(USceneComponent* component, bool bToggle = false, bool bAppend = false);
	void ClearSelection();
	void AddSelection(USceneComponent* component, bool bMakePrimary = true);
	void RemoveSelection(USceneComponent* component);
	bool IsComponentSelected(const USceneComponent* component) const;

	URenderer* GetRenderer() const { return Renderer; }
	USceneComponent* GetSelectedComponent() const { return SelectedComponent; }
	const TArray<USceneComponent*>& GetSelectedComponents() const { return SelectedComponents; }
	int32 GetSelectedComponentCount() const { return static_cast<int32>(SelectedComponents.size()); }
	bool HasSelection() const { return !SelectedComponents.empty(); }
	EditorConfig* GetConfig() const { return Config; }

private:
	void DrawControlPanel();
	void DrawConsole();
	void DrawPropertyPanel();
	void DrawStatPanel();
	void DrawToolbar();
	void DrawSceneManager();

	void CreateNewEditorConfig();
	void UpdateGizmoAttachment();
	void SetComponentSelectedState(USceneComponent* component, bool bSelected);

private:
	EditorControlPanel* ControlPanel = nullptr;
	EditorConsole* Console = nullptr;
	EditorPropertyPanel* PropertyPanel = nullptr;
	EditorStatPanel* StatPanel = nullptr;
	EditorToolbar* Toolbar = nullptr;
	EditorSceneManager* SceneManager = nullptr;

	URenderer* Renderer = nullptr;
	UCameraComponent* Camera = nullptr;
	USceneComponent* SelectedComponent = nullptr;
	TArray<USceneComponent*> SelectedComponents;

	EditorConfig* Config = nullptr;

public:
	static FOnEditorConfigLoaded OnEditorConfigLoaded;
	static FOnEditorConfigSaveReady OnEditorConfigSaveReady;
};
