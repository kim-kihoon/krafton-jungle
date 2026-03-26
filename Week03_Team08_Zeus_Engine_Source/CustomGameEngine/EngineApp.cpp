#include "EngineApp.h"
#include "Component/CameraComponent.h"
#include "Component/CubeComponent.h"
#include "Component/GizmoComponent.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderObject.h"
#include "Renderer/TextRenderObject.h"
#include "Renderer/Level.h"
#include "Editor/Editor.h"
#include "Editor/EditorConfig.h"
#include "Editor/Gizmo.h"
#include "Editor/Picker.h"
#include "ImGui/imgui.h"
#include "InputManager.h"
#include "Logger.h"
#include "Object.h"
#include "ObjectFactory.h"
#include "ResourceManager.h"
#include "Scene.h"
#include "TimerManager.h"
#include "World.h"
#include "Viewport.h"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK EngineApp::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
		return true;

	EngineApp* pApp = reinterpret_cast<EngineApp*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

	switch (message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	case WM_NCCREATE:
	{
		LPCREATESTRUCT pcs = reinterpret_cast<LPCREATESTRUCT>(lParam);
		pApp = reinterpret_cast<EngineApp*>(pcs->lpCreateParams);

		SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pApp));
		return 1;
	}
	case WM_SIZE:
		if (pApp)
		{
			UINT width = LOWORD(lParam);
			UINT height = HIWORD(lParam);
			
			pApp->Viewport->SetViewport(width, height);
		}
		break;
	case WM_SETFOCUS:
		InputManager::GetInstance().hWnd = (hWnd);
		break;

	case WM_KILLFOCUS:
		InputManager::GetInstance().hWnd = NULL;
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

EngineApp::EngineApp()
{
	Renderer = new URenderer();
	EditorInst = new Editor();
	Viewport = new FViewport(0, 0, 1920, 1080);
}

EngineApp::~EngineApp()
{
	delete Viewport;
	delete Picker;
	delete EditorInst;
	delete Renderer;
}

bool EngineApp::Initialize(HINSTANCE hInstance)
{
	CreateEngineWindow();
	Renderer->Create(HWnd, Viewport->Width, Viewport->Height);
	EditorInst->Initialize(HWnd, Renderer->Device.Get(), Renderer->DeviceContext.Get());

	// Init ResoureManager
	CreateDefaultResources();

	InputManager::GetInstance().hWnd = HWnd;
	// Init Camera
	auto Camera = Cast<UCameraComponent>(GetWorld().AddPermanentSceneComponent<UCameraComponent>());
	Camera->SetRelativeLocation(FVector(-20.f, -20.f, 20));
	Camera->SetRelativeRotation(FRotator(0, 45.0f, 45.0f));
	Renderer->SetRenderCamera(Camera);
	EditorInst->SetCamera(Camera);
	EditorInst->SetRenderer(Renderer);
	
	Gizmo::GetInstance().CreateGizmoController();

	//Init Picker
	Picker = Cast<UPicker>(FObjectFactory::ConstructObject(UPicker::GetClass()));
	Picker->SetEditor(EditorInst);
	Picker->SetCamera(Camera);

	//Init World & Scene
	GetWorld().InjectRenderer(Renderer);
	GetWorld().NewScene();

	// Init Timer
	TimerManager::GetInstance().CreateGlobalTimer();

	// Texture Atlas
	GetWorld().AddPermanentSceneComponent<UTextBatch>();

	EditorInst->LoadConfig();
	//EditorInst->GetConfig()->bShowUUID = false;

	FViewport::OnResizeDelegate.Broadcast(Viewport->Width, Viewport->Height);

	return true;
}

void EngineApp::Run()
{
	Timer* globalTimer = TimerManager::GetInstance().GetGlobalTimer();
	globalTimer->Reset();

	bool bIsExit = false;
	while (bIsExit == false)
	{
		MSG msg;
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			if (msg.message == WM_QUIT)
			{
				bIsExit = true;
				break;
			}
		}

		TimerManager::GetInstance().Tick();
		InputManager::GetInstance().Update();

		EditorInst->BeginFrame();

		float deltaTime = globalTimer->GetDeltaTime();

		FVector Mouse = InputManager::GetInstance().MousePos;
		
		if (InputManager::GetInstance().IsKeyDown('1')) Renderer->SetViewMode(0);
		if (InputManager::GetInstance().IsKeyDown('2')) Renderer->SetViewMode(1);
		if (InputManager::GetInstance().IsKeyDown('3')) Renderer->SetViewMode(2);
		if (InputManager::GetInstance().IsKeyDown('4')) Renderer->SetViewMode(3);
		if (InputManager::GetInstance().IsKeyDown('5')) Renderer->SetViewMode(4);
		if (InputManager::GetInstance().IsKeyDown(VK_DELETE))
		{
			// 여기가 맞나... 월드나 씬에서 관리하는게 맞지 않을까
			TArray<USceneComponent*> componentsToDelete = EditorInst->GetSelectedComponents();
			EditorInst->ClearSelection();

			for (USceneComponent* component : componentsToDelete)
			{
				GetWorld().RemoveSceneComponent(component);
			}
		}

		if (InputManager::GetInstance().IsMouseDown(VK_LBUTTON))
		{
			Picker->Pick(Mouse.x, Mouse.y, GetWorld().GetActiveScene());
		}

		// Update Game World
		GetWorld().Update(deltaTime);

		// Render
		GetWorld().OnBeforeRender();

		Renderer->Prepare();

		Renderer->Render();
		EditorInst->DrawUI();

		Renderer->SwapBuffer();
	}
}

void EngineApp::Finalize()
{
	if (EditorInst)
	{
		EditorInst->Finalize();
	}

	if (Renderer)
	{
		Renderer->Release();
	}

	if (HWnd)
	{
		ReleaseEngineWindow();
	}
	ResourceManager::GetInstance()->Release();
}

void EngineApp::CreateEngineWindow()
{
	WCHAR windowClass[] = L"JungleWindowClass";
	WCHAR title[] = L"Custom Engine";
	WNDCLASSW wndClass = { 0, WndProc, 0, 0, 0, 0, 0, 0, 0, windowClass };

	RegisterClass(&wndClass);

	HWnd = CreateWindowExW(0, windowClass, title,
		WS_POPUP | WS_VISIBLE | WS_OVERLAPPEDWINDOW,
		Viewport->X, Viewport->Y, Viewport->Width, Viewport->Height, nullptr,
		nullptr, nullptr, this);//lpParam에 this 넣어서 WndProc에서 접근 가능하도록
}

void EngineApp::ReleaseEngineWindow()
{
	if (HWnd)
	{
		DestroyWindow(HWnd);
		HWnd = nullptr;
	}
}

void EngineApp::CreateDefaultResources()
{
	D3D11_INPUT_ELEMENT_DESC defaultLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};
	D3D11_INPUT_ELEMENT_DESC FontLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	D3D11_INPUT_ELEMENT_DESC SubUVLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	D3D11_INPUT_ELEMENT_DESC LineLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};

	ResourceManager::GetInstance()->Initialize(Renderer);
	ResourceManager::GetInstance()->CreateMaterial(L"Asset/Shader/ShaderW0.hlsl", L"Asset/Shader/ShaderW0.hlsl", Renderer->Device.Get(), defaultLayout, ARRAYSIZE(defaultLayout));
	ResourceManager::GetInstance()->CreateMaterial(L"Asset/Shader/Grid.hlsl", L"Asset/Shader/Grid.hlsl", Renderer->Device.Get(), nullptr, 0);
	ResourceManager::GetInstance()->CreateMaterial(L"Asset/Shader/ShaderLine.hlsl", L"Asset/Shader/ShaderLine.hlsl", Renderer->Device.Get(), LineLayout, ARRAYSIZE(LineLayout));
	ResourceManager::GetInstance()->CreateMaterial(L"Asset/Shader/ShaderFont.hlsl", L"Asset/Shader/ShaderFont.hlsl", Renderer->Device.Get(), FontLayout, ARRAYSIZE(FontLayout));
	ResourceManager::GetInstance()->CreateMaterial(L"Asset/Shader/ShaderFontFixed.hlsl", L"Asset/Shader/ShaderFontFixed.hlsl", Renderer->Device.Get(), FontLayout, ARRAYSIZE(FontLayout));
	ResourceManager::GetInstance()->CreateMaterial(L"Asset/Shader/ShaderSubUV.hlsl", L"Asset/Shader/ShaderSubUV.hlsl", Renderer->Device.Get(), SubUVLayout, ARRAYSIZE(SubUVLayout));
	ResourceManager::GetInstance()->CreateDynamicFontAtlas(L"Asset/Font/Pretendard-Medium.ttf", Renderer->Device.Get(), Renderer->DeviceContext.Get());
	ResourceManager::GetInstance()->CreateStaticFontAtlas(L"Asset/Font/ChosunCentennial_ttf.png", Renderer->Device.Get(), Renderer->DeviceContext.Get());
	ResourceManager::GetInstance()->AddCubeMesh(Renderer->Device.Get());
	ResourceManager::GetInstance()->AddSphereMesh(Renderer->Device.Get());
	ResourceManager::GetInstance()->AddTriangleMesh(Renderer->Device.Get());
	ResourceManager::GetInstance()->AddPlaneMesh(Renderer->Device.Get());
	ResourceManager::GetInstance()->AddGizmoTranslationMesh(Renderer->Device.Get());
	ResourceManager::GetInstance()->AddGizmoRotationMesh(Renderer->Device.Get());
	ResourceManager::GetInstance()->AddGizmoScaleMesh(Renderer->Device.Get());
	ResourceManager::GetInstance()->AddLineMesh(Renderer->Device.Get());
	ResourceManager::GetInstance()->AddTextBatchMesh(Renderer->Device.Get());
	ResourceManager::GetInstance()->AddParticleSubUVMesh(Renderer->Device.Get());
}

