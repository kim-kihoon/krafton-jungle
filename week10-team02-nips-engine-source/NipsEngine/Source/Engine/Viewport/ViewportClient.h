#pragma once
#include <Core/CoreTypes.h>
#include "Core/CoreMinimal.h"
#include "Engine/Runtime/WindowsWindow.h"

struct FSceneView;

/**
 * Viewport 의 행동 정책을 정의하는 베이스 인터페이스
 * EditorViewportClient / GameViewportClient 가 이를 상속받음.
 */
class FViewportClient
{
public:
	virtual ~FViewportClient() = default;

	/** Called once after construction to bind the OS window. */
	virtual void Initialize(FWindowsWindow* InWindow) { Window = InWindow; }

	/** Resize the logical viewport. Subclasses may override to respond. */
	virtual void SetViewportSize(float InWidth, float InHeight)
	{
		if (InWidth  > 0.f) WindowWidth  = InWidth;
		if (InHeight > 0.f) WindowHeight = InHeight;
	}

	virtual void Tick(float DeltaTime) = 0;

	/**
	 * Populate OutView with camera matrices and viewport rect for the renderer.
	 */
	virtual void BuildSceneView(FSceneView& OutView) const = 0;

protected:
	FWindowsWindow* Window       = nullptr;
	float           WindowWidth  = 1920.f;
	float           WindowHeight = 1080.f;
};
