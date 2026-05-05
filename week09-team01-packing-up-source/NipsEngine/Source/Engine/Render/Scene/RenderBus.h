#pragma once

/*
	RenderBus는 Renderer에게 Draw Call 요청을 vector의 형태로 전달하는 역할을 합니다.
	Renderer가 RenderBus에 담긴 Draw Call 요청들을 처리할 수 있게 합니다.
*/

#include "Core/CoreMinimal.h"
#include "Render/Scene/RenderCommand.h"
#include "Render/Common/ViewTypes.h"
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

	// Getter, Setter
	void SetViewProjection(const FMatrix& InView, const FMatrix& InProj);
	void SetCameraPlane(float InNear, float InFar) { NearPlane = InNear; FarPlane = InFar; }
	void SetRenderSettings(const EViewMode NewViewMode, const FShowFlags NewShowFlags);

	const FMatrix& GetView() const { return View; }
	const FMatrix& GetProj() const { return Proj; }
	const float& GetNear() const { return NearPlane; }
	const float& GetFar() const { return FarPlane; }
	const FVector& GetCameraPosition() const { return CameraPosition;  }
	const FVector& GetCameraForward() const { return CameraForward; }
	const FVector& GetCameraUp() const { return CameraUp; }
	const FVector& GetCameraRight() const { return CameraRight; }
	bool IsOrthographic() const { return Proj.M[3][3] == 1.0f; }

	EViewMode GetViewMode() const { return ViewMode; }
	FShowFlags GetShowFlags() const { return ShowFlags; }
	
	const FVector& GetWireframeColor() const { return WireframeColor; }
	void SetWireframeColor(const FVector& InColor) { WireframeColor = InColor; }
	
	bool GetFXAAEnabled() const { return bFXAAEnabled; }
	void SetFXAAEnabled(bool bInEnabled) { bFXAAEnabled = bInEnabled; }

	void SetViewportSize(const FVector2& InViewportSize) { ViewportSize = InViewportSize; }
	const FVector2& GetViewportSize() const { return ViewportSize; }
	void SetViewportOrigin(const FVector2& InViewportOrigin) { ViewportOrigin = InViewportOrigin; }
	const FVector2& GetViewportOrigin() const { return ViewportOrigin; }

	void SetDirectionalShadow(const FDirectionalShadowConstants& InShadow) { DirectionalShadow = InShadow; }
	bool HasDirectionalShadow() const { return DirectionalShadow.has_value(); }
	const FDirectionalShadowConstants* GetDirectionalShadow() const { return DirectionalShadow.has_value() ? &DirectionalShadow.value() : nullptr; }

	EShadowFilterType GetShadowFilterType() const { return ShadowFilterType; }
    void SetShadowFilterType(const EShadowFilterType NewShadowFilterType) { ShadowFilterType = NewShadowFilterType; }

private:
	TArray<FRenderCommand> PassQueues[(uint32)ERenderPass::MAX];
	TArray<FRenderLight> Lights;
	std::optional<FDirectionalShadowConstants> DirectionalShadow;
    TArray<FSpotShadowConstants> CastShadowSpotLights;
    TArray<FPointShadowConstants> CastShadowPointLights;

	EShadowFilterType ShadowFilterType = EShadowFilterType::PCF;

	FMatrix View;
	FMatrix Proj;
	FVector CameraPosition;
	FVector CameraForward;
	FVector CameraRight;
	FVector CameraUp;
	float NearPlane;
	float FarPlane;

	FVector2 ViewportSize;
	FVector2 ViewportOrigin = FVector2(0.0f, 0.0f);

	// Editor Settings
	EViewMode ViewMode = EViewMode::Lit;
	FShowFlags ShowFlags;
	FVector WireframeColor = FVector(1.0f, 1.0f, 1.0f);
	bool bFXAAEnabled = true;
};
