#include "Viewport/Services/EditorViewportInputService.h"

#include "EditorEngine.h"
#include "Viewport/EditorViewportRegistry.h"
#include "Actor/Actor.h"
#include "Camera/Camera.h"
#include "Core/Engine.h"
#include "Debug/EngineLog.h"
#include "Gizmo/Gizmo.h"
#include "imgui.h"
#include "Input/InputManager.h"
#include "Picking/Picker.h"
#include "Scene/Level.h"
#include "Slate/SlateApplication.h"
#include "Viewport/Viewport.h"

void FEditorViewportInputService::TickCameraNavigation(
	FEngine* Engine,
	FEditorEngine* EditorEngine,
	FEditorViewportRegistry& ViewportRegistry,
	const FGizmo& Gizmo)
{
	if (!Engine || !EditorEngine)
	{
		return;
	}

	FSlateApplication* Slate = EditorEngine->GetSlateApplication();
	if (!Slate || Slate->GetFocusedViewportId() == INVALID_VIEWPORT_ID)
	{
		return;
	}

	FInputManager* Input = Engine->GetInputManager();
	if (!Input || Gizmo.IsDragging()) return;

	bool bIsCaptured = Input->IsMouseCaptured();
	bool bRightMouseDown = Input->IsMouseButtonDown(FInputManager::MOUSE_RIGHT);

	// 1. Possessed 상태(마우스 캡처 중)라면 에디터 회전 로직은 작동하지 않음 (TickPIECamera가 처리)
	if (bIsCaptured) return;

	// 2. Ejected 상태(마우스 해제)라면 반드시 뷰포트 영역 내부에 마우스가 있고 우클릭 중일 때만 허용
	if (!Slate->GetIsCoursorInArea() || !bRightMouseDown) return;

	// 3. ImGui 점유 체크 (우클릭 중일 때는 조작권을 우선함)
	if (ImGui::GetCurrentContext() && !bRightMouseDown)
	{
		const ImGuiIO& IO = ImGui::GetIO();
		if (IO.WantCaptureKeyboard || IO.WantCaptureMouse) return;
	}

	FViewportEntry* FocusedEntry = ViewportRegistry.FindEntryByViewportID(Slate->GetFocusedViewportId());
	if (!FocusedEntry)
	{
		return;
	}

	const float DeltaX = Input->GetMouseDeltaX();
	const float DeltaY = Input->GetMouseDeltaY();

	// [수정]: PIE 캡처가 일어난 직후 프레임에는 튐 방지를 위해 델타값을 무시한다.
	if (EditorEngine->IsPIEJustCaptured())
	{
		return;
	}

	if (FocusedEntry->LocalState.ProjectionType == EViewportType::Perspective)
	{
		float Sensitivity = 0.2f;
		if (FCamera* Cam = Engine->GetLevel()->GetCamera())
		{
			Sensitivity = Cam->GetMouseSensitivity();
		}

		FocusedEntry->LocalState.Rotation.Yaw += DeltaX * Sensitivity;
		FocusedEntry->LocalState.Rotation.Pitch -= DeltaY * Sensitivity;
		if (FocusedEntry->LocalState.Rotation.Pitch > 89.0f)
		{
			FocusedEntry->LocalState.Rotation.Pitch = 89.0f;
		}
		if (FocusedEntry->LocalState.Rotation.Pitch < -89.0f)
		{
			FocusedEntry->LocalState.Rotation.Pitch = -89.0f;
		}
		return;
	}

	FVector ViewFwd;
	FVector ViewUp;
	switch (FocusedEntry->LocalState.ProjectionType)
	{
	case EViewportType::OrthoTop:
		ViewFwd = FVector(0, 0, -1);
		ViewUp = FVector(1, 0, 0);
		break;

	case EViewportType::OrthoBottom:
		ViewFwd = FVector(0, 0, 1);
		ViewUp = FVector(1, 0, 0);
		break;

	case EViewportType::OrthoLeft:
		ViewFwd = FVector(0, 1, 0);
		ViewUp = FVector(0, 0, 1);
		break;

	case EViewportType::OrthoRight:
		ViewFwd = FVector(0, -1, 0);
		ViewUp = FVector(0, 0, 1);
		break;

	case EViewportType::OrthoFront:
		ViewFwd = FVector(-1, 0, 0);
		ViewUp = FVector(0, 0, 1);
		break;

	case EViewportType::OrthoBack:
		ViewFwd = FVector(1, 0, 0);
		ViewUp = FVector(0, 0, 1);
		break;

	default:
		return;
	}

	const FVector ViewRight = FVector::CrossProduct(ViewUp, ViewFwd).GetSafeNormal();
	const int32 H = FocusedEntry->Viewport->GetRect().Height;
	if (H <= 0) return;
	float WorldPerPixel = (2.0f * FocusedEntry->LocalState.OrthoZoom) / static_cast<float>(H);
	FocusedEntry->LocalState.OrthoTarget -= ViewRight * DeltaX * WorldPerPixel;
	FocusedEntry->LocalState.OrthoTarget += ViewUp * DeltaY * WorldPerPixel;
}

void FEditorViewportInputService::HandleMessage(
	FEngine* Engine,
	FEditorEngine* EditorEngine,
	HWND Hwnd,
	UINT Msg,
	WPARAM WParam,
	LPARAM LParam,
	FEditorViewportRegistry& ViewportRegistry,
	FPicker& Picker,
	FGizmo& Gizmo,
	const std::function<void()>& OnSelectionChanged)
{
	(void)Hwnd;

	if (!Engine || !EditorEngine)
	{
		return;
	}

	FSlateApplication* Slate = EditorEngine->GetSlateApplication();
	if (!Slate)
	{
		return;
	}

	// 최상단에서 ImGui 점유 확인 (다른 패널 클릭 시 뷰포트 로직 차단 및 포커스 해제)
	if (ImGui::GetCurrentContext())
	{
		const ImGuiIO& IO = ImGui::GetIO();

		// [중요] 마우스가 캡처된 Possessed 상태라면 ImGui의 간섭을 완전히 차단함
		FInputManager* Input = Engine->GetInputManager();
		bool bIsCaptured = Input && Input->IsMouseCaptured();

		if (IO.WantCaptureMouse && !bIsCaptured)
		{
			// PIE 모드 중 실제 뷰포트 영역 안을 클릭/우클릭하는 경우는 제외함 (조작권 보장)
			if (!EditorEngine->IsPlayingInEditor() || !Slate->GetIsCoursorInArea())
			{
				if (Msg == WM_LBUTTONDOWN || Msg == WM_RBUTTONDOWN || Msg == WM_MBUTTONDOWN)
				{
					Slate->ClearFocus();
					return;
				}

				if (Msg == WM_LBUTTONUP || Msg == WM_RBUTTONUP || Msg == WM_MBUTTONUP || Msg == WM_MOUSEMOVE)
				{
					return;
				}
			}
		}
	}

	const int32 MouseX = static_cast<int32>(static_cast<short>(LOWORD(LParam)));
	const int32 MouseY = static_cast<int32>(static_cast<short>(HIWORD(LParam)));

	const bool bIsPlaying = EditorEngine->IsPlayingInEditor();
	FInputManager* Input = Engine->GetInputManager();
	const bool bIsCaptured = Input && Input->IsMouseCaptured();
	const FViewportId PIEViewportId = EditorEngine->GetPIEViewportId();

	// [수정] 마우스가 캡처된 상태라면 슬레이트 및 에디터 모든 상호작용 차단 (포커스 변경 방지)
	if (bIsPlaying && bIsCaptured)
	{
		if (Msg >= WM_MOUSEFIRST && Msg <= WM_MOUSELAST)
		{
			return;
		}
	}

	switch (Msg)
	{
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	{
		Slate->ProcessMouseDown(MouseX, MouseY);
		// PIE 모드일 때 실제 뷰포트 영역(Slate Area) 내부를 클릭했을 때만 마우스 캡처
		if (bIsPlaying && Slate->GetIsCoursorInArea())
		{
			// 현재 어느 뷰포트가 PIE용인지 판별
			FViewportId TargetPIEId = INVALID_VIEWPORT_ID;
			FViewportId FocusedId = Slate->GetFocusedViewportId();
			FViewportEntry* FocusedEntry = ViewportRegistry.FindEntryByViewportID(FocusedId);
			if (FocusedEntry && FocusedEntry->LocalState.ProjectionType == EViewportType::Perspective)
			{
				TargetPIEId = FocusedId;
			}
			else
			{
				FViewportEntry* PerspEntry = ViewportRegistry.FindEntryByType(EViewportType::Perspective);
				if (PerspEntry) TargetPIEId = PerspEntry->Id;
			}

			// 클릭한 지점이 해당 PIE 뷰포트의 영역 내부인지 최종 확인
			FViewport* PIEViewport = ViewportRegistry.GetViewportById(TargetPIEId);
			if (PIEViewport)
			{
				const FRect& Rect = PIEViewport->GetRect();
				if (MouseX >= Rect.X && MouseX < (Rect.X + Rect.Width) &&
					MouseY >= Rect.Y && MouseY < (Rect.Y + Rect.Height))
				{
					if (Input)
					{
						EditorEngine->SetPIEJustCaptured(true); // 카메라 튐 방지 플래그 설정
						Input->SetMouseCapture(true);
						
						POINT Center = { PIEViewport->GetRect().X + PIEViewport->GetRect().Width / 2, PIEViewport->GetRect().Y + PIEViewport->GetRect().Height / 2 };
						::ClientToScreen(Engine->GetRenderer()->GetHwnd(), &Center);
						::SetCursorPos(Center.x, Center.y);
						
						// [중요]: 위치 강제 이동 직후 델타값을 즉시 날려서 다음 프레임에서 거대한 이동량이 계산되지 않도록 함
						Input->ResetMouseDelta();
						
						UE_LOG("[PIE] Re-captured (Possessed)");
					}
				}
			}
		}
		break;
	}
	case WM_LBUTTONDBLCLK:
		Slate->ProcessMouseDoubleClick(MouseX, MouseY);
		break;
	case WM_MOUSEMOVE:
		Slate->ProcessMouseMove(MouseX, MouseY);
		break;
	case WM_LBUTTONUP:
		Slate->ProcessMouseUp(MouseX, MouseY);
		break;
	default:
		break;
	}

	if (Msg == WM_MOUSEWHEEL)
	{
		FViewportEntry* FocusedEntry = ViewportRegistry.FindEntryByViewportID(Slate->GetFocusedViewportId());
		if (FocusedEntry && FocusedEntry->LocalState.ProjectionType != EViewportType::Perspective)
		{
			const float WheelDelta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(WParam)) / WHEEL_DELTA;
			FocusedEntry->LocalState.OrthoZoom *= (1.0f - WheelDelta * 0.1f);
			if (FocusedEntry->LocalState.OrthoZoom < 1.0f)
			{
				FocusedEntry->LocalState.OrthoZoom = 1.0f;
			}
			if (FocusedEntry->LocalState.OrthoZoom > 10000.0f)
			{
				FocusedEntry->LocalState.OrthoZoom = 10000.0f;
			}
		}
		return;
	}

	if (Slate->IsDraggingSplitter())
	{
		return;
	}

	const bool bRightMouseDown = Input && Input->IsMouseButtonDown(FInputManager::MOUSE_RIGHT);

	auto GetLevelForViewport = [&](FViewportId ViewportId) -> ULevel*
	{
		if (bIsPlaying)
		{
			// [분할 PIE 복구]: PIE 메인 뷰포트 클릭 시에는 PIE 레벨을, 나머지는 에디터 레벨을 반환
			if (ViewportId == PIEViewportId)
			{
				return EditorEngine->GetActiveLevel();
			}
			else
			{
				return EditorEngine->GetEditorLevel();
			}
		}
		return EditorEngine->GetActiveLevel();
	};

	AActor* SelectedActor = EditorEngine->GetSelectedActor();

	switch (Msg)
	{
	case WM_KEYDOWN:
	{
		if (Slate->GetFocusedViewportId() == INVALID_VIEWPORT_ID || bRightMouseDown)
		{
			return;
		}

		switch (WParam)
		{
		case 'W': Gizmo.SetMode(EGizmoMode::Location); return;
		case 'E': Gizmo.SetMode(EGizmoMode::Rotation); return;
		case 'R': Gizmo.SetMode(EGizmoMode::Scale); return;
		case 'L': Gizmo.ToggleCoordinateSpace(); return;
		case VK_SPACE: Gizmo.CycleMode(); return;
		default: return;
		}
	}

	case WM_LBUTTONDOWN:
	{
		FViewportId FocusedId = Slate->GetFocusedViewportId();
		FViewport* Viewport = ViewportRegistry.GetViewportById(FocusedId);
		if (!Viewport)
		{
			return;
		}

		ULevel* Level = GetLevelForViewport(FocusedId);
		if (!Level)
		{
			return;
		}

		FViewportEntry* Entry = ViewportRegistry.FindEntryByViewportID(FocusedId);
		const FRect& Rect = Viewport->GetRect();
		ScreenMouseX = MouseX - Rect.X;
		ScreenMouseY = MouseY - Rect.Y;

		if (SelectedActor && Gizmo.BeginDrag(SelectedActor, Entry, Picker, ScreenMouseX, ScreenMouseY))
		{
			return;
		}

		AActor* PickedActor = Picker.PickActor(Level, Entry, ScreenMouseX, ScreenMouseY);
		EditorEngine->SetSelectedActor(PickedActor);
		if (OnSelectionChanged) OnSelectionChanged();
		return;
	}

	case WM_MOUSEMOVE:
	{
		FViewportId HoveredId = Slate->GetHoveredViewportId();
		FViewport* Viewport = ViewportRegistry.GetViewportById(HoveredId);
		if (!Viewport)
		{
			Gizmo.ClearHover();
			return;
		}

		ULevel* Level = GetLevelForViewport(HoveredId);
		if (!Level)
		{
			return;
		}

		FViewportEntry* HoveredEntry = ViewportRegistry.FindEntryByViewportID(HoveredId);
		const FRect& Rect = Viewport->GetRect();
		ScreenMouseX = MouseX - Rect.X;
		ScreenMouseY = MouseY - Rect.Y;

		if (!Gizmo.IsDragging())
		{
			Gizmo.UpdateHover(SelectedActor, HoveredEntry, Picker, ScreenMouseX, ScreenMouseY);
			return;
		}

		if (Gizmo.UpdateDrag(SelectedActor, HoveredEntry, Picker, ScreenMouseX, ScreenMouseY) && OnSelectionChanged)
		{
			OnSelectionChanged();
		}
		return;
	}

	case WM_LBUTTONUP:
	{
		if (!Gizmo.IsDragging()) return;

		Gizmo.EndDrag();
		FViewportId HoveredId = Slate->GetHoveredViewportId();
		FViewport* Viewport = ViewportRegistry.GetViewportById(HoveredId);
		if (Viewport)
		{
			FViewportEntry* HoveredEntry = ViewportRegistry.FindEntryByViewportID(HoveredId);
			const FRect& Rect = Viewport->GetRect();
			ScreenMouseX = MouseX - Rect.X;
			ScreenMouseY = MouseY - Rect.Y;
			Gizmo.UpdateHover(SelectedActor, HoveredEntry, Picker, ScreenMouseX, ScreenMouseY);
		}
		else Gizmo.ClearHover();

		if (OnSelectionChanged) OnSelectionChanged();
		return;
	}

	default:
		return;
	}
}
