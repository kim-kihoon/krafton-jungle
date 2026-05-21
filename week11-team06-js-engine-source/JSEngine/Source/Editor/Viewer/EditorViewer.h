#pragma once

#include "Core/CoreTypes.h"
#include "Editor/Viewport/FSceneViewport.h"
#include "Editor/Viewport/EditorViewportClient.h"

class UEditorEngine;
class UWorld;
class FSelectionManager;
class FWindowsWindow;
struct ID3D11ShaderResourceView;

class FEditorViewer
{
public:
    virtual ~FEditorViewer() = default;

    virtual void Init(
        FWindowsWindow* InWindow,
        UEditorEngine* InEditor,
        UWorld* InWorld,
        FSelectionManager* InSelectionManager);

    virtual void Shutdown() = 0;

    virtual void Tick(float DeltaTime);

    virtual void SetRect(const FViewportRect& InRect);

	ID3D11ShaderResourceView* GetSRV() const { return Viewport.GetOutSRV(); }

	FSceneViewport& GetViewport() { return Viewport; }
    const FSceneViewport& GetViewport() const { return Viewport; }
    virtual FEditorViewportClient* GetViewportClient() { return Viewport.GetClient(); }
    virtual const FEditorViewportClient* GetViewportClient() const { return Viewport.GetClient(); }

	virtual void ChangeTarget(const FString& InFileName);
    const FString& GetFileName() const { return FileName; }

	virtual FEditorViewportClient& GetClient() = 0;
    virtual const FEditorViewportClient& GetClient() const = 0;

protected:
    FSceneViewport Viewport;

    FString FileName;
};
