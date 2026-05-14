#pragma once

/*
	RenderBus는 Renderer에게 Draw Call 요청을 vector의 형태로 전달하는 역할을 합니다.
	Renderer가 RenderBus에 담긴 Draw Call 요청들을 처리할 수 있게 합니다.
*/

#include "Core/CoreMinimal.h"
#include "Engine/Runtime/SceneView.h"
#include "Render/Scene/RenderCommand.h"
#include <optional>

class FRenderBus
{
public:
	void Clear();

	void AddCommand(ERenderPass Pass, const FRenderCommand& InCommand);
	void AddCommand(ERenderPass Pass, FRenderCommand&& InCommand);
	void AddLight(const FRenderLight& InLight) { Lights.push_back(InLight); }
	void AddCastShadowSpotLight(const FSpotShadowConstants& InCastShadowLight) { CastShadowSpotLights.push_back(InCastShadowLight); }
	void AddCastPointShadowLight(const FPointShadowConstants& InCastShadowLight) { CastShadowPointLights.push_back(InCastShadowLight); }

	const TArray<FRenderCommand>& GetCommands(ERenderPass Pass) const;
	const TArray<FRenderLight>& GetLights() const { return Lights; }
	const TArray<FSpotShadowConstants>& GetCastShadowSpotLights() const { return CastShadowSpotLights; }
	const TArray<FPointShadowConstants>& GetCastShadowPointLights() const { return CastShadowPointLights; }

private:
	TArray<FRenderCommand> PassQueues[(uint32)ERenderPass::MAX];
	TArray<FRenderLight> Lights;
	std::optional<FDirectionalShadowConstants> DirectionalShadow;
	TArray<FSpotShadowConstants> CastShadowSpotLights;
	TArray<FPointShadowConstants> CastShadowPointLights;
	
	FSceneView SceneView;
	EShadowFilterType ShadowFilterType = EShadowFilterType::PCF;

	FShowFlags ShowFlags;
	FVector WireframeColor = FVector(1.0f, 1.0f, 1.0f);
	bool bFXAAEnabled = true;

	// Getter, Setter
public:
	void SetSceneView(const FSceneView& InSceneView) { SceneView = InSceneView; }
	void SetRenderSettings(const EViewMode NewViewMode, const FShowFlags NewShowFlags);

	const FSceneView& GetSceneView() const { return SceneView; }
	const FMatrix& GetView() const { return SceneView.View; }
	const FMatrix& GetProj() const { return SceneView.Proj; }
	FMatrix GetViewProj() const { return SceneView.View * SceneView.Proj; }
	float GetNear() const { return SceneView.NearPlane; }
	float GetFar() const { return SceneView.FarPlane; }
	const FVector& GetCameraPosition() const { return SceneView.CameraPosition;  }
	const FVector& GetCameraForward() const { return SceneView.CameraForward; }
	const FVector& GetCameraUp() const { return SceneView.CameraUp; }
	const FVector& GetCameraRight() const { return SceneView.CameraRight; }
	bool IsOrthographic() const { return SceneView.bOrthographic; }

	EViewMode GetViewMode() const { return SceneView.ViewMode; }
	FShowFlags GetShowFlags() const { return ShowFlags; }
	
	const FVector& GetWireframeColor() const { return WireframeColor; }
	void SetWireframeColor(const FVector& InColor) { WireframeColor = InColor; }
	
	bool GetFXAAEnabled() const { return bFXAAEnabled; }
	void SetFXAAEnabled(bool bInEnabled) { bFXAAEnabled = bInEnabled; }

	FVector2 GetViewportSize() const { return FVector2(static_cast<float>(SceneView.ViewRect.Width), static_cast<float>(SceneView.ViewRect.Height)); }
	FVector2 GetViewportOrigin() const { return FVector2(static_cast<float>(SceneView.ViewRect.X), static_cast<float>(SceneView.ViewRect.Y)); }

	void SetDirectionalShadow(const FDirectionalShadowConstants& InShadow) { DirectionalShadow = InShadow; }
	bool HasDirectionalShadow() const { return DirectionalShadow.has_value(); }
	const FDirectionalShadowConstants* GetDirectionalShadow() const { return DirectionalShadow.has_value() ? &DirectionalShadow.value() : nullptr; }

	EShadowFilterType GetShadowFilterType() const { return ShadowFilterType; }
	void SetShadowFilterType(const EShadowFilterType NewShadowFilterType) { ShadowFilterType = NewShadowFilterType; }

	const FPostProcessSettings& GetPostProcessSettings() const { return SceneView.PostProcessSettings; }
	const FCameraOverlaySettings& GetCameraOverlaySettings() const { return SceneView.CameraOverlaySettings; }
};
