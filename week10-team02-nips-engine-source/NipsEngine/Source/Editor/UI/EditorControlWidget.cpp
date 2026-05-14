#include "Editor/UI/EditorControlWidget.h"

#include "Editor/EditorEngine.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "Core/Logging/Timer.h"

#include "ImGui/imgui.h"
#include "Component/GizmoComponent.h"

#define SEPARATOR()     \
	;                   \
	ImGui::Spacing();   \
	ImGui::Spacing();   \
	ImGui::Separator(); \
	ImGui::Spacing();   \
	ImGui::Spacing();

// 에디터 컨트롤 위젯을 초기화하고 기본 상태를 설정합니다.
void FEditorControlWidget::Initialize(UEditorEngine* InEditorEngine)
{
	FEditorWidget::Initialize(InEditorEngine);
}

// 컨트롤 패널 UI를 렌더링하고 액터 생성 및 카메라 제어 기능을 처리합니다.
void FEditorControlWidget::Render(float DeltaTime)
{
	(void)DeltaTime;
	if (!EditorEngine)
	{
		return;
	}

	ImGui::SetNextWindowCollapsed(false, ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(500.0f, 480.0f), ImGuiCond_Once);

	ImGui::Begin("Control Panel");

	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);

	// Camera
	FViewportCamera* Camera = EditorEngine->GetCamera();
	if (Camera == nullptr)
	{
		ImGui::End();
		return;
	}

	bool bIsOrtho = (Camera->GetProjectionType() == EViewportProjectionType::Orthographic);
	if (ImGui::Checkbox("Orthographic", &bIsOrtho))
	{
		Camera->SetProjectionType(bIsOrtho ? EViewportProjectionType::Orthographic : EViewportProjectionType::Perspective);
	}

	float CameraFOV_Deg = MathUtil::RadiansToDegrees(Camera->GetFOV());
	if (ImGui::DragFloat("Camera FOV", &CameraFOV_Deg, 0.5f, 1.0f, 90.0f))
	{
		Camera->SetFOV(MathUtil::DegreesToRadians(CameraFOV_Deg));
	}

	float OrthoHeight = Camera->GetOrthoHeight();
	if (ImGui::DragFloat("Ortho Height", &OrthoHeight, 0.1f, 0.1f, 1000.0f))
	{
		Camera->SetOrthoHeight(MathUtil::Clamp(OrthoHeight, 0.1f, 1000.0f));
	}

	FVector CamPos = Camera->GetLocation();
	float CameraLocation[3] = { CamPos.X, CamPos.Y, CamPos.Z };
	if (ImGui::DragFloat3("Camera Location", CameraLocation, 0.1f, 0.0f, 0.0f, "%.1f"))
	{
		Camera->SetLocation(FVector(CameraLocation[0], CameraLocation[1], CameraLocation[2]));
	}

	FVector CamRot = Camera->GetRotation().Rotator().Euler();
	float CameraRotation[3] = { CamRot.X, CamRot.Y, CamRot.Z };
	if (ImGui::DragFloat3("Camera Rotation", CameraRotation, 0.1f, 0.0f, 0.0f, "%.1f"))
	{
		CameraRotation[1] = MathUtil::Clamp(CameraRotation[1], -89.9f, 89.9f);
		FRotator NewRotation = FRotator::MakeFromEuler(FVector(CameraRotation[0], CameraRotation[1], CameraRotation[2]));

		NewRotation.Normalize();
		Camera->SetRotation(NewRotation);
	}
	ImGui::PopItemWidth();

	ImGui::End();
}
