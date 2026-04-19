#include "EngineTypes.h"
#include "EditorControlPanel.h"
#include "Editor.h"
#include "Component/CameraComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderObject.h"
#include "PrimitiveFactory.h"
#include "TimerManager.h"
#include "World.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"
#include "ImGui/imgui.h"
#include "EditorConfig.h"

static EPrimitiveType primitiveType = EPrimitiveType::Triangle;
static int spawnCount = 0;
static wchar_t sceneName[128] = L"Default";
static bool orthogonal = false;
static float fov = 45.0f;
static FVector cameraLocation = { 0.0f, 0.0f, 0.0f };
static FVector cameraRotation = { 0.0f, 0.0f, 0.0f };

static float frameCount = 0;
static float elapsedTime = 0.0f;
static float displayFPS = 0.0f;
static float frameMilliseconds = 0.0f;
const float fpsUpdateInterval = 0.5f; // 초 단위

void WCharToUTF8(const wchar_t* src, char* dst, int dstSize) {
	WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, dstSize, NULL, NULL);
}

// 2. UTF-8을 wchar_t로 변환하는 유틸리티 (MultiByteToWideChar)
void UTF8ToWChar(const char* src, wchar_t* dst, int dstSize) {
	MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, dstSize);
}

EditorControlPanel::EditorControlPanel(Editor* parent) : editor(parent)
{
}

EditorControlPanel::~EditorControlPanel()
{
}

void UpdateFPS(float deltaTime)
{
	frameCount++;
	elapsedTime += deltaTime;

	if (elapsedTime >= fpsUpdateInterval)
	{
		displayFPS = frameCount / elapsedTime;
		frameMilliseconds = TimerManager::GetInstance().GetGlobalDeltaTime() * 1000;

		frameCount = 0;
		elapsedTime = 0.0f;
	}
}

void EditorControlPanel::Draw(UCameraComponent* camera)
{
	EditorPanelVisibility& visibility = GetEditorPanelVisibility();
	if (!visibility.bShowControlPanel)
		return;

	UpdateFPS(TimerManager::GetInstance().GetGlobalDeltaTime());

	const EditorLayout layout = GetEditorLayout(visibility);
	const ImGuiWindowFlags windowFlags = ApplyFixedWindow(layout.ControlPanel);
	ImGui::Begin("Jungle Control Panel", nullptr, windowFlags);
	ImGui::Text("Hello Jungle World!");
	ImGui::Text("FPS %.0f (%.2f ms)",
		displayFPS,
		frameMilliseconds);
	ImGui::Separator();

	ImGui::Combo("Primitive", (int*)&primitiveType, "Sphere\0Cube\0Triangle\0Plane\0Text\0ParticleSubUV\0");

	if (ImGui::Button("Spawn"))
	{
		for (int i = 0; i < spawnCount; i++)
		{
			// TODO: Apply primitive type to spawn
			auto p = PrimitiveFactory::AddPrimitive(primitiveType);
		}
	}
	ImGui::SameLine();
	ImGui::DragInt("Number of spawn", &spawnCount, 1, 0, INT_MAX, "%d", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SameLine();
	ImGui::Separator();

	char utf8_sceneName[128];
	WCharToUTF8(sceneName, utf8_sceneName, 128);

	if (ImGui::InputText("Scene Name", utf8_sceneName, 128))
	{
		UTF8ToWChar(utf8_sceneName, sceneName, 128);
	}
	if (ImGui::Button("New Scene"))
	{
		editor->SelectComponent(nullptr);
		GetWorld().NewScene();
	}
	ImGui::SameLine();
	if (ImGui::Button("Save Scene"))
	{
		GetWorld().SaveScene(utf8_sceneName);
	}
	ImGui::SameLine();
	if (ImGui::Button("Load Scene"))
	{
		FWString wSceneName;

		editor->SelectComponent(nullptr);
		GetWorld().LoadScene(wSceneName);

		if (wSceneName.size() > 0)
		{
			WCharToUTF8(wSceneName.c_str(), utf8_sceneName, 128);
			UTF8ToWChar(utf8_sceneName, sceneName, 128);
		}
	}
	ImGui::Separator();

	int projectionMode = camera->IsOrthographic ? 1 : 0;
	bool wasOrthographic = camera->IsOrthographic;

	ImGui::RadioButton("Perspective", &projectionMode, 0);
	ImGui::SameLine();
	ImGui::RadioButton("Orthographic", &projectionMode, 1);

	camera->IsOrthographic = (projectionMode == 1);
	if (wasOrthographic != camera->IsOrthographic)
		editor->SaveEditorConfig();

	if (ImGui::DragFloat("FOV", &camera->Fov, 0.1f, 10.0f, 170.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp))
		editor->SaveEditorConfig();

	FVector camPosition = camera->GetRelativeLocation();
	FRotator camRotation = camera->GetRelativeRotation();
	ImGui::DragFloat3("Camera Location", &camPosition.x, 0.1f);
	ImGui::DragFloat3("Camera Rotation", &camRotation.Roll, 0.1f);
	camera->SetRelativeLocation(camPosition);
	camera->SetRelativeRotation(camRotation);

	if (ImGui::DragFloat("Camera Speed", &camera->MoveSpeed, 0.1f, 0.0f, 10000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp))
		editor->SaveEditorConfig();
	if (ImGui::DragFloat("Camera Rotation", &camera->RotateSensitivity, 0.1f, 0.0f, 10000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp))
		editor->SaveEditorConfig();

	ImGui::Separator();

	if (ImGui::CollapsingHeader("View Mode", ImGuiTreeNodeFlags_DefaultOpen))
	{
		int CurrentMode = editor->GetRenderer()->GetViewMode();

		ImGui::RadioButton("Lit Mode (1)", &CurrentMode, static_cast<uint32>(EViewMode::Lit));
		ImGui::RadioButton("Unlit Mode (2)", &CurrentMode, static_cast<uint32>(EViewMode::Unlit));
		ImGui::RadioButton("Wireframe Mode (3)", &CurrentMode, static_cast<uint32>(EViewMode::Wireframe));
		ImGui::RadioButton("Normals Mode (4)", &CurrentMode, static_cast<uint32>(EViewMode::Normals));
		ImGui::RadioButton("Depth Mode (5)", &CurrentMode, static_cast<uint32>(EViewMode::Depth));

		if (CurrentMode != editor->GetRenderer()->GetViewMode())
		{
			editor->GetRenderer()->SetViewMode(CurrentMode);
			editor->SaveEditorConfig();
		}
	}
	
	ImGui::Separator();

	if (ImGui::CollapsingHeader("Rendering Filters", ImGuiTreeNodeFlags_DefaultOpen))
	{
		FRenderState& renderState = URenderer::GetRenderState();
		uint32 showFlags = renderState.ShowFlags;

		ImGui::CheckboxFlags("Static Meshes", &renderState.ShowFlags, (uint32)EShowFlag::StaticMesh);
		ImGui::CheckboxFlags("Gizmos", &renderState.ShowFlags, (uint32)EShowFlag::Gizmo);
		ImGui::CheckboxFlags("Grid", &renderState.ShowFlags, (uint32)EShowFlag::Grid);
		ImGui::CheckboxFlags("World Axis", &renderState.ShowFlags, (uint32)EShowFlag::Axis);
		ImGui::CheckboxFlags("Camera Icons", &renderState.ShowFlags, (uint32)EShowFlag::Camera);
		ImGui::CheckboxFlags("Debug Lines", &renderState.ShowFlags, (uint32)EShowFlag::Debug);
		ImGui::CheckboxFlags("Text", &renderState.ShowFlags, (uint32)EShowFlag::Text);
		ImGui::CheckboxFlags("UUID", &renderState.ShowFlags, (uint32)EShowFlag::UUID);

		if (ImGui::Button("Reset All Filters"))
		{
			renderState.ShowFlags = (uint32)EShowFlag::All & ~(uint32)EShowFlag::Debug & ~(uint32)EShowFlag::UUID;
		}

		if (showFlags != renderState.ShowFlags)
		{
			editor->SaveEditorConfig();
		}
	}

	if (ImGui::CollapsingHeader("Grid Setting", ImGuiTreeNodeFlags_DefaultOpen))
	{
		float gridSize = editor->GetRenderer()->gridSize;

		ImGui::DragFloat("Grid Size", &editor->GetRenderer()->gridSize, 0.1f, 5.0f, 50.0f);

		if (gridSize != editor->GetRenderer()->gridSize)
		{
			editor->SaveEditorConfig();
		}
	}

	ImGui::End();
}
