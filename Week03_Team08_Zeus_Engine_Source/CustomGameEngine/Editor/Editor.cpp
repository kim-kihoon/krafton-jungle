#include "Editor.h"
#include "EditorConfig.h"
#include "EditorConsole.h"
#include "EditorControlPanel.h"
#include "EditorPropertyPanel.h"
#include "EditorStatPanel.h"
#include "EditorToolbar.h"
#include "EditorSceneManager.h"
#include "Gizmo.h"
#include "Component/GizmoComponent.h"
#include "Component/SceneComponent.h"
#include "Component/PrimitiveComponent.h"
#include "SerializeHelper.h"
#include "World.h"
#include "Renderer/Renderer.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"

#include <algorithm>

FOnEditorConfigLoaded Editor::OnEditorConfigLoaded;
FOnEditorConfigSaveReady Editor::OnEditorConfigSaveReady;

Editor::Editor()
{
	Config = new EditorConfig();
	ControlPanel = new EditorControlPanel(this);
	Console = new EditorConsole();
	PropertyPanel = new EditorPropertyPanel(this);
	StatPanel = new EditorStatPanel();
	SceneManager = new EditorSceneManager(this);
}

Editor::~Editor()
{
	delete Config;
	delete ControlPanel;
	delete Console;
	delete PropertyPanel;
	delete StatPanel;
	delete SceneManager;
}

bool Editor::Initialize(HWND hWnd, ID3D11Device* device, ID3D11DeviceContext* deviceContext)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	ImGui_ImplWin32_Init(hWnd);
	ImGui_ImplDX11_Init(device, deviceContext);

	io.Fonts->AddFontFromFileTTF("Asset/Font/Pretendard-Medium.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesKorean());

	return true;
}

void Editor::BeginFrame()
{
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}

void Editor::DrawUI()
{
	DrawControlPanel();
	DrawConsole();
	DrawPropertyPanel();
	DrawStatPanel();
	DrawToolbar();
	DrawSceneManager();

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void Editor::Finalize()
{
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void Editor::SaveEditorConfig()
{
	EditorConfig config;
	OnEditorConfigSaveReady.Broadcast(config);
	SerializeHelper::SaveAsJson(&config, L"editor.ini");
}

void Editor::LoadConfig()
{
	EditorConfig* config = SerializeHelper::LoadFromJson<EditorConfig>(L"editor.ini");

	if (config)
	{
		OnEditorConfigLoaded.Broadcast(*config);
		delete config;
	}
	else
	{
		CreateNewEditorConfig();
	}
}

void Editor::SetComponentSelectedState(USceneComponent* component, bool bSelected)
{
	if (!component)
	{
		return;
	}

	component->bIsSelected = bSelected;

	if (UPrimitiveComponent* prim = Cast<UPrimitiveComponent>(component))
	{
		prim->MarkRenderStateDirty();
	}
}

bool Editor::IsComponentSelected(const USceneComponent* component) const
{
	return std::find(SelectedComponents.begin(), SelectedComponents.end(), component) != SelectedComponents.end();
}

void Editor::UpdateGizmoAttachment()
{
	UGizmoComponent* gizmoController = Gizmo::GetInstance().GetController();
	if (!gizmoController)
	{
		return;
	}

	gizmoController->AttachTo(SelectedComponent, SelectedComponents);
}

void Editor::ClearSelection()
{
	for (USceneComponent* component : SelectedComponents)
	{
		SetComponentSelectedState(component, false);
	}

	SelectedComponents.clear();
	SelectedComponent = nullptr;
	UpdateGizmoAttachment();
}

void Editor::AddSelection(USceneComponent* component, bool bMakePrimary)
{
	if (!component)
	{
		return;
	}

	if (!IsComponentSelected(component))
	{
		SelectedComponents.push_back(component);
		SetComponentSelectedState(component, true);
	}

	if (bMakePrimary)
	{
		SelectedComponent = component;
	}
	else if (!SelectedComponent)
	{
		SelectedComponent = component;
	}

	UpdateGizmoAttachment();
}

void Editor::RemoveSelection(USceneComponent* component)
{
	if (!component)
	{
		return;
	}

	auto it = std::find(SelectedComponents.begin(), SelectedComponents.end(), component);
	if (it == SelectedComponents.end())
	{
		return;
	}

	SetComponentSelectedState(component, false);
	SelectedComponents.erase(it);

	if (SelectedComponent == component)
	{
		SelectedComponent = SelectedComponents.empty() ? nullptr : SelectedComponents.back();
	}

	UpdateGizmoAttachment();
}

void Editor::SelectComponent(USceneComponent* component, bool bToggle, bool bAppend)
{
	if (!component)
	{
		if (!bToggle && !bAppend)
		{
			ClearSelection();
		}
		return;
	}

	if (bToggle)
	{
		if (IsComponentSelected(component))
		{
			RemoveSelection(component);
		}
		else
		{
			AddSelection(component, true);
		}
		return;
	}

	if (bAppend)
	{
		AddSelection(component, true);
		return;
	}

	ClearSelection();
	AddSelection(component, true);
}

void Editor::DrawControlPanel()
{
	ControlPanel->Draw(Camera);
}

void Editor::DrawConsole()
{
	EditorPanelVisibility& visibility = GetEditorPanelVisibility();
	Console->Draw("Example: Console", &visibility.bShowConsole);
}

void Editor::DrawPropertyPanel()
{
	PropertyPanel->Draw(SelectedComponent, SelectedComponents);
}

void Editor::DrawStatPanel()
{
	StatPanel->Draw();
}

void Editor::DrawToolbar()
{
	Toolbar->Draw();
}

void Editor::DrawSceneManager()
{
	SceneManager->Draw();
}

void Editor::CreateNewEditorConfig()
{
	EditorConfig config;

	SerializeHelper::SaveAsJson(&config, L"editor.ini");

	OnEditorConfigLoaded.Broadcast(config);
}
