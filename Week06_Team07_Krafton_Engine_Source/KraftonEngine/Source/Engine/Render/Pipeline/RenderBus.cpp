#include "RenderBus.h"
#include "Component/CameraComponent.h"
#include "Viewport/Viewport.h"

void FRenderBus::Clear()
{
	for (uint32 i = 0; i < (uint32)ERenderPass::MAX; ++i)
	{
		ProxyQueues[i].clear();
	}

	FontEntries.clear();
	OverlayFontEntries.clear();
	SubUVEntries.clear();
	BillboardEntries.clear();
	AABBEntries.clear();
	OBBEntries.clear();
	GridEntries.clear();
	DebugLineEntries.clear();
	FireBallEntries.clear();

	ViewportRTV = nullptr;
	ViewportDSV = nullptr;
	ViewportStencilSRV = nullptr;
	PostProcessRTV1 = nullptr;
	PostProcessRTV2 = nullptr;
	PostProcessSRV1 = nullptr;
	PostProcessSRV2 = nullptr;
	BaseSRV = nullptr;
	CurrentRTV = nullptr;
	CurrentSRV = nullptr;
	bHasPostProcessOutput = false;

	bHasFog = false;
	FogParams = {};
}

void FRenderBus::AddProxy(ERenderPass Pass, const FPrimitiveSceneProxy* Proxy)
{
	ProxyQueues[(uint32)Pass].push_back(Proxy);
}

const TArray<const FPrimitiveSceneProxy*>& FRenderBus::GetProxies(ERenderPass Pass) const
{
	return ProxyQueues[(uint32)Pass];
}

void FRenderBus::AddFontEntry(FFontEntry&& Entry)
{
	FontEntries.push_back(std::move(Entry));
}

void FRenderBus::AddOverlayFontEntry(FFontEntry&& Entry)
{
	OverlayFontEntries.push_back(std::move(Entry));
}

void FRenderBus::AddSubUVEntry(FSubUVEntry&& Entry)
{
	SubUVEntries.push_back(std::move(Entry));
}

void FRenderBus::AddBillboardEntry(FBillboardEntry&& Entry)
{
	BillboardEntries.push_back(std::move(Entry));
}

void FRenderBus::AddAABBEntry(FAABBEntry&& Entry)
{
	AABBEntries.push_back(std::move(Entry));
}

void FRenderBus::AddOBBEntry(FOBBEntry&& Entry)
{
	OBBEntries.push_back(std::move(Entry));
}

void FRenderBus::AddGridEntry(FGridEntry&& Entry)
{
	GridEntries.push_back(std::move(Entry));
}

void FRenderBus::AddDebugLineEntry(FDebugLineEntry&& Entry)
{
	DebugLineEntries.push_back(std::move(Entry));
}

void FRenderBus::AddFireBallEntry(FFireBallEntry& Entry)
{
	FireBallEntries.push_back(Entry);
}

void FRenderBus::SetCameraInfo(const UCameraComponent* Camera)
{
	View = Camera->GetViewMatrix();
	Proj = Camera->GetProjectionMatrix();
	CameraPosition = Camera->GetWorldLocation();
	CameraForward = Camera->GetForwardVector();
	CameraRight = Camera->GetRightVector();
	CameraUp = Camera->GetUpVector();
	bIsOrtho = Camera->IsOrthogonal();
	OrthoWidth = Camera->GetOrthoWidth();
}

void FRenderBus::SetViewportInfo(const FViewport* VP)
{
	viewportWidth = static_cast<float>(VP->GetWidth());
	viewportHeight = static_cast<float>(VP->GetHeight());
	ViewportRTV = VP->GetBaseRTV();
	ViewportDSV = VP->GetDSV();
	ViewportStencilSRV = VP->GetStencilSRV();
	PostProcessRTV1 = VP->GetPostProcessRTV1();
	PostProcessRTV2 = VP->GetPostProcessRTV2();
	PostProcessSRV1 = VP->GetPostProcessSRV1();
	PostProcessSRV2 = VP->GetPostProcessSRV2();
	BaseSRV = VP->GetBaseSRV();
	ViewportDepthSRV = VP->GetDepthSRV();
	CurrentRTV = ViewportRTV;
	CurrentSRV = BaseSRV;
	bHasPostProcessOutput = false;
}

void FRenderBus::SetRenderSettings(const EViewMode NewViewMode, const FShowFlags NewShowFlags)
{
	ViewMode = NewViewMode;
	ShowFlags = NewShowFlags;
}

void FRenderBus::SetViewportSize(float InWidth, float InHeight)
{
	viewportWidth = InWidth;
	viewportHeight = InHeight;
}
