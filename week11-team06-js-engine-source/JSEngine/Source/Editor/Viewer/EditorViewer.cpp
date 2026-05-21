#include "EditorViewer.h"

#include "Component/PostProcess/Light/AmbientLightComponent.h"
#include "EditorEngine.h"
#include "GameFramework/PrimitiveActors.h"

void FEditorViewer::Init(
    FWindowsWindow* InWindow,
    UEditorEngine* InEditor,
    UWorld* InWorld,
    FSelectionManager* InSelectionManager)
{
    Viewport.GetState().ViewMode = EViewMode::Lit_BlinnPhong;
    Viewport.GetState().LightCullMode = ELightCullMode::None;

    FViewportRect Rect = { 0, 0, 300, 300 };
    Viewport.SetRect(Rect);

    ADirectionalLightActor* DirectionalLight = InWorld->SpawnActor<ADirectionalLightActor>();
    if (DirectionalLight)
    {
        DirectionalLight->InitDefaultComponents();
        DirectionalLight->SetFName(FName("Viewer Directional Light"));
        DirectionalLight->SetActorLocation(FVector(100000.0f, 100000.0f, 100000.0f));
        DirectionalLight->SetActorRotation(FVector(0.0f, 44.0f, 0.0f));
    }

    AAmbientLightActor* AmbientLight = InWorld->SpawnActor<AAmbientLightActor>();
    if (AmbientLight)
    {
        AmbientLight->InitDefaultComponents();
        AmbientLight->SetFName(FName("Viewer Ambient Light"));
        AmbientLight->SetActorLocation(FVector(100000.0f, 100000.0f, 100000.0f));
        AmbientLight->FindComponent<UAmbientLightComponent>()->Intensity = 0.7f;
    }
}

void FEditorViewer::Tick(float DeltaTime)
{
    if (FEditorViewportClient* Client = Viewport.GetClient())
    {
        Client->Tick(DeltaTime);
    }
}

void FEditorViewer::SetRect(const FViewportRect& InRect)
{
    Viewport.SetRect(InRect);
    if (FEditorViewportClient* Client = Viewport.GetClient())
    {
        Client->SetViewportSize((float)InRect.Width, (float)InRect.Height);
    }
}

void FEditorViewer::ChangeTarget(const FString& InFileName)
{
    FileName = InFileName;
}
