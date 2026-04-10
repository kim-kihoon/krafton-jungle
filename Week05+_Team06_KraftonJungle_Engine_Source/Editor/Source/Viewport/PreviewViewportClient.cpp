#include "PreviewViewportClient.h"
#include "Core/ShowFlags.h"

#include "UI/EditorUI.h"
#include "EditorEngine.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderCommand.h"
#include "Component/CameraComponent.h"
#include "Math/Frustum.h"
#include "World/World.h"
#include "imgui.h"

FPreviewViewportClient::FPreviewViewportClient(FEditorUI& InEditorUI, FString InPreviewContextName)
	: EditorUI(InEditorUI)
	, PreviewContextName(std::move(InPreviewContextName))
{
}

void FPreviewViewportClient::Attach(FEngine* Engine, FRenderer* Renderer)
{
	FEditorEngine* EditorEngine = static_cast<FEditorEngine*>(Engine);
	if (!EditorEngine || !Renderer)
	{
		return;
	}

	EditorUI.Initialize(EditorEngine);
	EditorUI.AttachToRenderer(Renderer);
}

void FPreviewViewportClient::Detach(FEngine* Engine, FRenderer* Renderer)
{
	EditorUI.DetachFromRenderer(Renderer);
}

void FPreviewViewportClient::Tick(FEngine* Engine, float DeltaTime)
{
	if (!Engine)
	{
		return;
	}

	FSlateApplication* Slate = EditorUI.GetEngine()->GetSlateApplication();
	if (!Slate || Slate->GetFocusedViewportId() == INVALID_VIEWPORT_ID)
	{
		return;
	}

	if (ImGui::GetCurrentContext())
	{
		const ImGuiIO& IO = ImGui::GetIO();
		if (IO.WantCaptureKeyboard || IO.WantCaptureMouse)
		{
			return;
		}
	}

	IViewportClient::Tick(Engine, DeltaTime);
}

void FPreviewViewportClient::Render(FEngine* Engine, FRenderer* Renderer)
{
	if (!Engine || !Renderer)
	{
		return;
	}

	ULevel* Level = ResolveLevel(Engine);
	UWorld* ActiveWorld = ResolveWorld(Engine);

	if (Level && ActiveWorld)
	{
		UCameraComponent* ActiveCamera = ActiveWorld->GetActiveCameraComponent();
		if (ActiveCamera)
		{
			FRenderCommandQueue Queue;
			Queue.Reserve(Renderer->GetPrevCommandCount());
			Queue.ViewMatrix = ActiveCamera->GetViewMatrix();
			Queue.ProjectionMatrix = ActiveCamera->GetProjectionMatrix();

			FFrustum Frustum;
			Frustum.ExtractFromVP(Queue.ViewMatrix * Queue.ProjectionMatrix);

			const FVector CameraPosition = Queue.ViewMatrix.GetInverse().GetTranslation();
			BuildRenderCommands(Engine, Level, Frustum, FShowFlags{}, CameraPosition, Queue);
			Renderer->SubmitCommands(Queue);
			Renderer->ExecuteCommands();
		}
	}

	EditorUI.Render();
}

ULevel* FPreviewViewportClient::ResolveLevel(FEngine* Engine) const
{
	FEditorEngine* EditorEngine = static_cast<FEditorEngine*>(Engine);
	if (!EditorEngine)
	{
		return nullptr;
	}

	if (ULevel* PreviewLevel = EditorEngine->GetPreviewLevel(PreviewContextName))
	{
		return PreviewLevel;
	}

	return EditorEngine->GetActiveLevel();
}

