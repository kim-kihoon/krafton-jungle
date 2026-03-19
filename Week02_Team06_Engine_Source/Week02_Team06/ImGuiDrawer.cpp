#include "pch.h"
#include "ImGuiDrawer.h"
#include "SceneSerializer.h"
#include "PrimitiveComponent.h"
#include "FEditorViewportClient.h"
#include "World.h"
#include "StaticMeshActor.h"
#include "EngineStatics.h"
#include "App.h"

UImGuiDrawer::UImGuiDrawer()
{
	CreateAppConsole();
}

void UImGuiDrawer::Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context, UWorld* world)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui_ImplWin32_Init((void*)hwnd);
	ImGui_ImplDX11_Init(device, context);
	World = world;
}

void UImGuiDrawer::BeginFrame()
{
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}

void UImGuiDrawer::EndFrame()
{
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void UImGuiDrawer::UpdateUI(FEditorViewportClient* ViewportClient)
{
    ImGui::Begin("Jungle Control Panel");
    ImGui::Text("Hello Jungle World!");
	float fps = ImGui::GetIO().Framerate;
	float ms = (fps > 0.0f) ? (1000.0f / fps) : 0.0f;
	ImGui::Separator();
	ImGui::Text("TotalAllocationBytes: %d", FMemory::TotalAllocationBytes);
	ImGui::Text("TotalAllocationCount: %d", FMemory::TotalAllocationCount);
	ImGui::Text("FPS %.0f (%.0fms)", fps, ms);
	ImGui::Separator();

	DrawSpawnPanel();
	ImGui::Separator();
	DrawSceneControlPanel();
	ImGui::Separator();
	DrawCameraPanel(ViewportClient);

	ImGui::End();

	DrawPrimitiveDataPanel();

	if(bShowConsole)
		DrawAppConsole("Jungle Tech Lab", &bShowConsole);
}

void UImGuiDrawer::Release()
{
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();

	ImGui::DestroyContext();
    DestroyAppConsole();
}

void UImGuiDrawer::DrawSpawnPanel()
{
	static int primitiveType = 0;
	const char* items[] = { "Cube", "Sphere", "Triangle" };
	ImGui::Combo("Primitive", &primitiveType, items, IM_ARRAYSIZE(items));

	actorParameters.PrimitiveType = items[primitiveType];
	actorParameters.Location = World->ViewPort->GetViewTransform().GetPivotLocation();
	actorParameters.Rotation = {0.0f, 0.0f, 0.0f};
	actorParameters.Scale = {1.0f, 1.0f, 1.0f};

	if (ImGui::Button("Spawn"))
	{
		World->SpawnActorFromEditor(actorParameters);
	}
	ImGui::SameLine();
	static int count = 1;
	ImGui::InputInt("Count", &count);
	actorParameters.Count = count;

	//if (ImGui::Button("Delete"))
	//{
	//	UObjectFactory::DestroyObject(SelectedTarget);
	//}
}

void UImGuiDrawer::DrawSceneControlPanel()
{
	static char sceneName[128] = "Default";
	ImGui::InputText("Scene Name", sceneName, IM_COUNTOF(sceneName));

	if (ImGui::Button("New Scene")) 
	{ 
		World->ClearScene();
		ClearAppConsole();
		strncpy_s(sceneName, "Default", IM_COUNTOF(sceneName));
	}

	if (ImGui::Button("Save Scene"))
	{
		USceneSerializer::SaveScene(sceneName, World);
		USceneSerializer::SaveEditorConfig();
	}

	if (ImGui::Button("Load Scene"))
	{
		USceneSerializer::LoadScene(sceneName, World);
	}
}

void UImGuiDrawer::DrawCameraPanel(FEditorViewportClient* ViewportClient)
{
	bool bIsOrthogonal = !ViewportClient->IsPerspective();
	ImGui::Checkbox("Orthogonal", &bIsOrthogonal);
	ViewportClient->SetPerspective(!bIsOrthogonal);

	float FOV = ViewportClient->GetFOVAngle();
	if (bIsOrthogonal)
	{
		ImGui::BeginDisabled();
	}
	ImGui::SliderFloat("FOV", &FOV, 10.0f, 120.0f);
	ViewportClient->SetFOVAngle(FOV);
	if (bIsOrthogonal)
	{
		ImGui::EndDisabled();
	}

	std::array<float, 3> Position = {
			ViewportClient->GetViewLocation().X,
			ViewportClient->GetViewLocation().Y,
			ViewportClient->GetViewLocation().Z
	};
	ImGui::DragFloat3("Camera Location", Position.data(), 0.1f, -10.f, 10.0f);
	ViewportClient->SetViewLocation({ Position[0], Position[1], Position[2] });

	std::array<float, 3> Rotation = {
		ViewportClient->GetViewRotation().X,
		ViewportClient->GetViewRotation().Y,
		ViewportClient->GetViewRotation().Z
	};
	ImGui::DragFloat3("Camera Rotation", Rotation.data(), 0.5f);
	ViewportClient->SetViewRotation({ Rotation[0], Rotation[1], Rotation[2] });
}

void UImGuiDrawer::DrawPrimitiveDataPanel()
{
	ImGui::Begin("Jungle Property Window");

	SelectedTarget = World->SelectedActor;

	if (SelectedTarget)
	{
		ActorLocation = SelectedTarget->RootComponent->GetPosition();
		ActorRotation = SelectedTarget->RootComponent->GetRotation();
		ActorScale = SelectedTarget->RootComponent->GetScale();

		if (ImGui::DragFloat3("Translation", &ActorLocation.X, 0.1f))
		{
			if (SelectedTarget) SelectedTarget->RootComponent->SetPosition(ActorLocation);
		}


		if (ImGui::DragFloat3("Rotation", &ActorRotation.X, 0.1f))
		{
			if (SelectedTarget) SelectedTarget->RootComponent->SetRotation(ActorRotation);
		}


		if (ImGui::DragFloat3("Scale", &ActorScale.X, 0.1f))
		{
			if (SelectedTarget) SelectedTarget->RootComponent->SetScale(ActorScale);
		}
	}

	ImGui::End();
}
