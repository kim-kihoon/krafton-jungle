#include <windows.h>

#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"

#include "URenderer.h"
#include "UManager.h"
#include "UImanager.h"
#include "UResourceManager.h"
#include "StateMachine.h"
#include "BootState.h"
#include "EndingState.h"

//#pragma region __DEBUG_CONSOLE__
//
//#include <iostream>
//
////void CreateConsole()
////{
////	AllocConsole();
////
////	FILE* fp;
////	freopen_s(&fp, "CONOUT$", "w", stdout);
////	freopen_s(&fp, "CONOUT$", "w", stderr);
////	freopen_s(&fp, "CONIN$", "r", stdin);
////
////	std::cout.clear();
////	std::cerr.clear();
////	std::cin.clear();
////}
//
//#pragma endregion

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

inline void createBuffer(UManager* manager, URenderer* renderer)
{
	MeshResource* probe = manager->getProbeResource();
	MeshResource* sphere = manager->getSphereResource();
	MeshResource* goal = manager->getGoalResource();

	renderer->CreateVertexBuffer(probe->VB, probe->Vertices.data(), probe->Vertices.size() * sizeof(FTextureVertex));
	renderer->CreateVertexBuffer(sphere->VB, sphere->Vertices.data(), sphere->Vertices.size() * sizeof(FTextureVertex));
	renderer->CreateVertexBuffer(goal->VB, goal->Vertices.data(), goal->Vertices.size() * sizeof(FTextureVertex));


	renderer->CreateIndexBuffer(probe->IB, probe->Indexes.data(), probe->IndexCount);
	renderer->CreateIndexBuffer(sphere->IB, sphere->Indexes.data(), sphere->IndexCount);
	renderer->CreateIndexBuffer(goal->IB, goal->Indexes.data(), goal->IndexCount);


	manager->setProbeResource(*probe);
	manager->setSphereResource(*sphere);
	manager->setGoalResource(*goal);
}



static UManager* g_Manager = nullptr;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
		return true;

	switch (message)
	{
	case WM_LBUTTONDOWN:
		if (g_Manager) g_Manager->OnMouseClick();
		break;

		 
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	WCHAR WindowClass[] = L"JungleWindowClass";
	WCHAR Title[] = L"Team3_week1";

	WNDCLASSW wndclass = { 0, WndProc, 0, 0, 0, 0, 0, 0, 0, WindowClass };
	RegisterClassW(&wndclass);

	//	4 * 3 (1400 * 1050)
	DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

	RECT rc = { 0, 0, WindowWidth, WindowHeight };
	AdjustWindowRect(&rc, style, FALSE);

	HWND hWnd = CreateWindowExW(
		0,
		WindowClass,
		Title,
		style | WS_VISIBLE,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		rc.right - rc.left,
		rc.bottom - rc.top,
		nullptr,
		nullptr,
		hInstance,
		nullptr
	);

#pragma region __DEBUG_CONSOLE__
	
	//CreateConsole();
	//std::cout << "Debug Console Initialized!" << std::endl;

#pragma endregion

	// 1. 매니저 및 렌더러 생성

	URenderer* renderer = new URenderer();
	renderer->Create(hWnd);
	renderer->CreateShader();
	renderer->CreateConstantBuffer();
	//////////////////////1.Imgui 초기화//////////////////////
	UIManager::InitImGui(hWnd, renderer);
	///////////////////////////////////////////////////////


	UManager* manager = new UManager(renderer->Device, renderer->DeviceContext);
	g_Manager = manager;
	manager->Initialize(hWnd); // 사운드 여기서 시작!
	
	createBuffer(manager, renderer);

	StateMachine stateMachine;
	//stateMachine.Initialize(new BootState(), manager);
	stateMachine.Initialize(new BootState(), manager);

	// 타이머 설정
	LARGE_INTEGER freq, prevTime, currTime;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&prevTime);
	float deltaTime = 0.0f;


	// frame 제한
	const int targetFPS = 60;
	const double targetFrameTime = 1000.0 / targetFPS;

	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);

	LARGE_INTEGER startTime, endTime;
	double elapsedTime = 0.0;

	float fixedTimeScale = 0.02f;
	float timer = 0.f;

	bool bIsExit = false;
	while (!bIsExit)
	{

		QueryPerformanceCounter(&startTime);
		MSG msg;
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			if (msg.message == WM_QUIT) bIsExit = true;
		}
		if (bIsExit) break;

		QueryPerformanceCounter(&currTime);
		deltaTime = static_cast<float>(currTime.QuadPart - prevTime.QuadPart) / static_cast<float>(freq.QuadPart);
		prevTime = currTime;

		// --- Rendering ---
		renderer->Prepare();

		renderer->PrepareShader();

		timer += deltaTime;
		if (timer >= fixedTimeScale) 
		{
			stateMachine.Update(fixedTimeScale, manager);
			timer -= fixedTimeScale;
		}

		stateMachine.Render(renderer, manager);

		renderer->SwapBuffer();
	}

	
	manager->Release(); // 사운드 해제 포함
	delete manager;

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	
	renderer->Release();
	delete renderer;

	return 0;
}