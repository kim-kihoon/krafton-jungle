#include "pch.h"
#include "App.h"

#include "Window.h"
#include "Renderer.h"
#include "Input.h"
#include "Graphics.h"
#include "AppConsole.h"
#include "ImGuiDrawer.h"
#include "ResourceManager.h"
#include "FEditorViewportClient.h"
#include "World.h"
#include "ObjectFactory.h"
#include "Actor.h"
#include "SceneSerializer.h"
#include "LocationGizmoActor.h"

bool UApp::Initialize(HINSTANCE hInstance)
{
	Window = new UWindow;
	if (!Window->Initialize(hInstance, WindowSizeWidth, WindowSizeHeight))
	{
		return false;
	}

	Graphics = new UGraphics;
	Graphics->Initialize(Window->GetHWnd());

	float AspectRatio = static_cast<float>(WindowSizeWidth) / WindowSizeHeight;
	ViewportClient = new FEditorViewportClient({ 10.0f, 10.0f, -10.0f }, { 45.0f, -45.0f, 0.0f }, AspectRatio, 60.f);

	ViewportClient->SetAspectRatio(static_cast<float>(WindowSizeWidth) / WindowSizeHeight);

	Renderer = new URenderer(Graphics->GetDevice(), Graphics->GetDeviceContext(), Graphics->GetSwapChain(), *ViewportClient);
	Renderer->Initialize();

	ResourceManager = new UResourceManager;
	ResourceManager->Initialize(*Graphics->GetDevice());

	World = UObjectFactory::NewObject<UWorld>();

	World->InitWorld(*ResourceManager, ViewportClient);

	ImGuiDrawer = new UImGuiDrawer;
	ImGuiDrawer->Initialize(Window->GetHWnd(), Graphics->GetDevice(), Graphics->GetDeviceContext(), World);

	USceneSerializer::LoadEditorConfig();

	// console Test Code
	UE_LOG("Hello World %d", 2025);

	return true;
}

void UApp::Run()
{
	MSG msg = {};

	LARGE_INTEGER StartTime, EndTime, Frequency;

	//double		  TargetFrameMilliSecond = 1.f / TargetFrame ;
	//double		  ElapsedMilliSecond = 0.f;
	
	QueryPerformanceFrequency(&Frequency);
	QueryPerformanceCounter(&StartTime);

	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}


		QueryPerformanceCounter(&EndTime);
		double CounterInterval = static_cast<double>(EndTime.QuadPart - StartTime.QuadPart);

		//ElapsedMilliSecond = CounterInterval / Frequency.QuadPart * 1000.f;
		DeltaTime = static_cast<float>(CounterInterval / Frequency.QuadPart);

		//if(ElapsedMilliSecond >= TargetFrameMilliSecond)
		if (true)
		{
			StartTime = EndTime;
			//60프레인 제한시
			//DeltaTime = static_cast<float>(ElapsedMilliSecond / 1000.f); // 초단위로
			DeltaTime = static_cast<float>(CounterInterval / Frequency.QuadPart);

			if (bResized)
			{
				Renderer->OnResize(g_width, g_height);
				bResized = false;
			}

			//input
			UInput::GetInstance().Update();

			//Viewport 정보 갱신
			ViewportClient->Tick(DeltaTime);

			//GameLogic
			World->Tick(DeltaTime);

			//Render
			Renderer->BeginScene();
			Renderer->Render(World);

			//ImGui
			ImGuiDrawer->BeginFrame();
			ImGuiDrawer->UpdateUI(ViewportClient);
			ImGuiDrawer->EndFrame();

			Renderer->EndScene();
		}
		/*else
		{
			Sleep(0);
		}*/

	}
}

void UApp::Release()
{
	if (ImGuiDrawer)
	{
		ImGuiDrawer->Release();
		delete ImGuiDrawer;
		ImGuiDrawer = nullptr;
	}

	if (Renderer)
	{
		Renderer->Release();
		delete Renderer;
	}

	UObjectFactory::DestroyAllObjects();

	if (ViewportClient)
	{
		delete ViewportClient;
	}

	if (Graphics)
	{
		Graphics->Release();
		delete Graphics;
	}

	if (ResourceManager)
	{
		ResourceManager->Release();
		delete ResourceManager;
	}
	if (Window)
	{
		Window->Release();
		delete Window;
	}

	USceneSerializer::SaveEditorConfig();
}