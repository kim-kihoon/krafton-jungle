#include "SlateApplication.h"

#include "SWindow.h"
#include "Input/InputSystem.h"
#include "ImGui/imgui.h"

void FSlateApplication::RegisterViewport(SWindow* Window, FViewportClient* Client)
{
	if (!Window || !Client) return;

	for (FViewportInfo& Info : RegisteredViewports)
	{
		if (Info.Client == Client)
		{
			Info.Window = Window;
			return;
		}
	}

	RegisteredViewports.push_back({ Window, Client });
}

void FSlateApplication::UnregisterViewport(FViewportClient* Client)
{
	if (!Client) return;

	RegisteredViewports.erase(
		std::remove_if(RegisteredViewports.begin(), RegisteredViewports.end(),
			[Client](const FViewportInfo& Info) { return Info.Client == Client; }),
		RegisteredViewports.end());

	if (FocusedClient == Client)
	{
		FocusedClient = nullptr;
	}
	if (CapturedClient == Client)
	{
		CapturedClient = nullptr;
	}
	if (HoveredClient == Client)
	{
		HoveredClient = nullptr;
	}
}

void FSlateApplication::UpdateInputOwner()
{
	InputSystem& Input = InputSystem::Get();
	const FInputSystemSnapshot Snapshot = Input.MakeSnapshot();

	if (!Snapshot.bWindowFocused)
	{
		HoveredClient = nullptr;
		FocusedClient = nullptr;
		CapturedClient = nullptr;
		return;
	}

	FPoint MousePoint;
	const bool bUseScreenSpace =
		(ImGui::GetCurrentContext() != nullptr) &&
		((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0);
	POINT Mouse = bUseScreenSpace ? Input.GetMousePos() : Input.GetMouseClientPos();
	MousePoint.X = static_cast<float>(Mouse.x);
	MousePoint.Y = static_cast<float>(Mouse.y);

	HoveredClient = nullptr;

	for (auto It = RegisteredViewports.rbegin(); It != RegisteredViewports.rend(); ++It)
	{
		if (It->Window && It->Client && It->Window->IsHover(MousePoint))
		{
			HoveredClient = It->Client;
			break;
		}
	}

	const bool bMousePressed =
		Snapshot.bLeftMousePressed ||
		Snapshot.bRightMousePressed ||
		Snapshot.bMiddleMousePressed;

	if (bMousePressed && HoveredClient)
	{
		FocusedClient = HoveredClient;
		CapturedClient = HoveredClient;
	}

	const bool bAnyMouseDown =
		Snapshot.bLeftMouseDown ||
		Snapshot.bRightMouseDown ||
		Snapshot.bMiddleMouseDown;

	if (!bAnyMouseDown)
	{
		CapturedClient = nullptr;
	}

	if (FocusedClient && !IsViewportRegistered(FocusedClient))
	{
		FocusedClient = nullptr;
	}

	if (CapturedClient && !IsViewportRegistered(CapturedClient))
	{
		CapturedClient = nullptr;
	}
}

void FSlateApplication::BringViewportToFront(FViewportClient* Client)
{
	if (!Client) return;

	auto It = std::find_if(RegisteredViewports.begin(), RegisteredViewports.end(),
		[Client](const FViewportInfo& Info) { return Info.Client == Client; });

	if (It == RegisteredViewports.end()) return;

	FViewportInfo Info = *It;
	RegisteredViewports.erase(It);
	RegisteredViewports.push_back(Info);
}

bool FSlateApplication::DoesClientOwnMouseInput(FViewportClient* Client) const
{
	if (!Client) return false;

	// 누군가 마우스를 꽉 붙잡고(Capture) 있다면, 그 녀석만 권한을 가짐
	if (CapturedClient)
	{
		return Client == CapturedClient;
	}

	// 붙잡고 있는 녀석이 없을 때만 마우스가 올라가 있는(Hover) 녀석에게 권한을 줌
	return Client == HoveredClient;
}

bool FSlateApplication::DoesClientOwnKeyboardInput(FViewportClient* Client) const
{
	return Client && (Client == FocusedClient);
}

void FSlateApplication::CaptureMouse(FViewportClient* Client)
{
	CapturedClient = Client;
}

void FSlateApplication::ReleaseMouse(FViewportClient* Client)
{
	if (CapturedClient == Client)
	{
		CapturedClient = nullptr;
	}
}

bool FSlateApplication::IsViewportRegistered(FViewportClient* Client) const
{
	for (const FViewportInfo& Info : RegisteredViewports)
	{
		if (Info.Client == Client)
		{
			return true;
		}
	}
	return false;
}
