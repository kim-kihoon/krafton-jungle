#include "RenderCollector.h"

#include "Render/LineBatcher.h"
#include "Render/Renderer/RenderFlow/ShadowAtlasManager.h"
#include "GameFramework/World.h"
#include "GameFramework/AActor.h"
#include "Object/ActorIterator.h"
#include "Component/BillboardComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/GizmoComponent.h"
#include "Component/TextRenderComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/DecalComponent.h"
#include "Component/HeightFogComponent.h"
#include "Component/SkyAtmosphereComponent.h"
#include "Component/Light/AmbientLightComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Component/Light/LightComponent.h"
#include "Component/Light/PointLightComponent.h"
#include "Component/Light/SpotLightComponent.h"
#include "Core/ResourceManager.h"
#include "Engine/Geometry/Frustum.h"
#include "Engine/Asset/StaticMesh.h"
#include "Engine/GameFramework/PrimitiveActors.h"
#include "Render/Resource/Material.h"
#include "Math/Utils.h"
#include "Object/ObjectIterator.h"
#include "Runtime/Stats/ScopeCycleCounter.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

namespace
{
	// ─────────────────── Billboard, SubUV ───────────────────
	FMatrix MakeViewSubUVSelectionMatrix(const USubUVComponent* SubUVComp, const FRenderBus& RenderBus);

	// ─────────────────── AABB, BVH ───────────────────
	bool UsesCameraDependentRenderBounds(const UPrimitiveComponent* PrimitiveComponent);
	FAABB BuildQuadAABB(const FMatrix& WorldMatrix);
	FAABB BuildRenderAABB(const UPrimitiveComponent* PrimitiveComponent, const FRenderBus& RenderBus);
} // namespace

void FRenderCollector::ResetCullingStats()
{
	LastStats.Culling = {};
}

void FRenderCollector::ResetDecalStats()
{
	LastStats.Decal = {};
}

void FRenderCollector::ResetShadowStats()
{
	LastStats.Shadow = {};
}

void FRenderCollector::Initialize(ID3D11Device* InDevice)
{
	MeshBufferManager.Create(InDevice);
	LightRenderCollector.Initialize(&MeshBufferManager);
	OverlayRenderCollector.Initialize(&MeshBufferManager);
	PrimitiveRenderCollector.Initialize(&MeshBufferManager);
}

void FRenderCollector::Release()
{
	LineBatcher = nullptr;
	LightRenderCollector.Release();
	OverlayRenderCollector.Release();
	PrimitiveRenderCollector.Release();
	MeshBufferManager.Release();
}

void FRenderCollector::CollectWorld(UWorld* World, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus,
									const FFrustum* ViewFrustum)
{
	ResetCullingStats();
	ResetDecalStats();
	ResetShadowStats();

	if (!World)
		return;

	CollectLight(World, RenderBus, ViewFrustum);
	if (ShowFlags.bShadow)
	{
		CollectShadowCasters(World, RenderBus);
	}

	if (ViewFrustum)
	{
		VisiblePrimitiveScratch.clear();
		World->GetSpatialIndex().FrustumQueryPrimitives(*ViewFrustum, VisiblePrimitiveScratch, FrustumQueryScratch);

		for (UPrimitiveComponent* Primitive : VisiblePrimitiveScratch)
		{
			if (Primitive == nullptr || UsesCameraDependentRenderBounds(Primitive) || !Primitive->IsEnableCull())
				continue;
			++LastStats.Culling.BVHPassedPrimitiveCount;
			CollectFromComponent(Primitive, ShowFlags, ViewMode, RenderBus, World->GetWorldType());
		}
	}

	std::unordered_set<UPrimitiveComponent*> CollectCameraDependentPrimitives;
	if (ViewFrustum)
	{
		CollectCameraDependentPrimitives.reserve(32);
	}

	// Frustum이 없다면 액터 단위로 통째로 수집하고, 그렇지 않다면 BVH에서 누락된 컴포넌트들을 개별 수집
	for (TActorIterator<AActor> Iter(World); Iter; ++Iter)
	{
		AActor* Actor = *Iter;
		if (!Actor || !Actor->IsVisible())
			continue;

		if (!ViewFrustum)
		{
			for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
			{
				if (Primitive != nullptr && !Primitive->IsVisible())
				{
					++LastStats.Culling.TotalVisiblePrimitiveCount;
				}
			}
			CollectFromActor(Actor, ShowFlags, ViewMode, RenderBus, World->GetWorldType());
			continue; // early-continue
		}

		// 이미 처리된 컴포넌트, 중복된 컴포넌트는 제외하고 Frustum Culling 수행
		for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
		{
			if (!Primitive || !Primitive->IsVisible())
				continue;

			++LastStats.Culling.TotalVisiblePrimitiveCount;

			const bool bIsCameraDependent = UsesCameraDependentRenderBounds(Primitive);
			if (!bIsCameraDependent && Primitive->IsEnableCull() || !CollectCameraDependentPrimitives.insert(Primitive).second)
				continue;

			if (bIsCameraDependent && Primitive->IsEnableCull())
			{
				if (ViewFrustum->Intersects(BuildRenderAABB(Primitive, RenderBus)) == FFrustum::EFrustumIntersectResult::Outside)
					continue;
			}

			++LastStats.Culling.FallbackPassedPrimitiveCount;
			CollectFromComponent(Primitive, ShowFlags, ViewMode, RenderBus, World->GetWorldType());
		}
	}
}

// ─────────────────── Sub Collects ────────────────────────────────────────────────────────────

// Frustum Culling을 통해 Light Collect와 Shadow Collect를 동시에 수행해줍니다.
void FRenderCollector::CollectLight(UWorld* World, FRenderBus& RenderBus, const FFrustum* ViewFrustum)
{
	LightRenderCollector.CollectLight(World, RenderBus, LastStats, ViewFrustum);
}

void FRenderCollector::CollectShadowCasters(UWorld* World, FRenderBus& RenderBus)
{
	LightRenderCollector.CollectShadowCasters(World, RenderBus);
}

void FRenderCollector::CollectSelection(const TArray<AActor*>& SelectedActors, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus)
{
	OverlayRenderCollector.CollectSelection(SelectedActors, ShowFlags, ViewMode, RenderBus, LineBatcher);
}

void FRenderCollector::CollectOutline(const TArray<AActor*>& Actors, const FVector4& OutlineColor, float OutlineThicknessPixels, FRenderBus& RenderBus)
{
	OverlayRenderCollector.CollectOutline(Actors, OutlineColor, OutlineThicknessPixels, RenderBus);
}

void FRenderCollector::CollectDebugBounds(UWorld* World, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus)
{
	OverlayRenderCollector.CollectDebugBounds(World, ShowFlags, ViewMode, RenderBus, LineBatcher);
}

void FRenderCollector::CollectGrid(float GridSpacing, int32 GridHalfLineCount, FRenderBus& RenderBus, bool bOrthographic)
{
	OverlayRenderCollector.CollectGrid(GridSpacing, GridHalfLineCount, RenderBus, bOrthographic);
}

void FRenderCollector::CollectGizmo(UGizmoComponent* Gizmo, const FShowFlags& ShowFlags, FRenderBus& RenderBus, bool bIsActiveOperation)
{
	OverlayRenderCollector.CollectGizmo(Gizmo, ShowFlags, RenderBus, bIsActiveOperation);
}

void FRenderCollector::CollectFromActor(AActor* Actor, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus, EWorldType WorldType)
{
	PrimitiveRenderCollector.CollectFromActor(Actor, ShowFlags, ViewMode, RenderBus, WorldType, LastStats, LineBatcher);
}

void FRenderCollector::CollectFromComponent(UPrimitiveComponent* Primitive, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus, EWorldType WorldType)
{
	PrimitiveRenderCollector.CollectFromComponent(Primitive, ShowFlags, ViewMode, RenderBus, WorldType, LastStats, LineBatcher);
}

void FRenderCollector::CollectBVHInternalNodeAABBs(UPrimitiveComponent* PrimitiveComponent, const FShowFlags& ShowFlags, FRenderBus& RenderBus, std::unordered_set<int32>& SeenNodeIndices)
{
	OverlayRenderCollector.CollectBVHInternalNodeAABBs(PrimitiveComponent, ShowFlags, RenderBus, LineBatcher, SeenNodeIndices);
}

// ─────────────────── namespace ────────────────────────────────────────────────────────────

namespace
	{
	bool UsesCameraDependentRenderBounds(const UPrimitiveComponent* PrimitiveComponent)
	{
		if (PrimitiveComponent == nullptr)
		{
			return false;
		}

		switch (PrimitiveComponent->GetPrimitiveType())
		{
		case EPrimitiveType::EPT_Billboard:
		case EPrimitiveType::EPT_Text:
		case EPrimitiveType::EPT_SubUV:
			return true;
		default:
			return false;
		}
	}

	FMatrix MakeViewSubUVSelectionMatrix(const USubUVComponent* SubUVComp, const FRenderBus& RenderBus)
	{
		const FVector WorldScale = SubUVComp->GetWorldScale();
		return USubUVComponent::MakeBillboardWorldMatrix(
			SubUVComp->GetWorldLocation(),
			FVector(
				WorldScale.X > 0.01f ? WorldScale.X : 0.01f,
				SubUVComp->GetWidth() * WorldScale.Y,
				SubUVComp->GetHeight() * WorldScale.Z),
			RenderBus.GetCameraForward(),
			RenderBus.GetCameraRight(),
			RenderBus.GetCameraUp());
	}

	// BillBoardComponent를 상속받은 text, SubUV가 사용하는 AABB 계산함수(의존성 분리)
	FAABB BuildQuadAABB(const FMatrix& WorldMatrix)
	{
		static constexpr FVector LocalQuadCorners[4] = {
			FVector(0.0f, -0.5f, 0.5f),
			FVector(0.0f, 0.5f, 0.5f),
			FVector(0.0f, 0.5f, -0.5f),
			FVector(0.0f, -0.5f, -0.5f)
		};

		FAABB Box;
		Box.Reset();

		for (const FVector& Corner : LocalQuadCorners)
		{
			Box.Expand(WorldMatrix.TransformPosition(Corner));
		}

		return Box;
	}

	FAABB BuildRenderAABB(const UPrimitiveComponent* PrimitiveComponent, const FRenderBus& RenderBus)
	{
		switch (PrimitiveComponent->GetPrimitiveType())
		{
		case EPrimitiveType::EPT_Billboard:
		{
			const UBillboardComponent* BillboardComponent = static_cast<const UBillboardComponent*>(PrimitiveComponent);
			return BuildQuadAABB(UBillboardComponent::MakeBillboardWorldMatrix(
				BillboardComponent->GetWorldLocation(),
				FVector(0.00f, BillboardComponent->GetWidth(), BillboardComponent->GetHeight()),
				RenderBus.GetCameraForward(),
				RenderBus.GetCameraRight(),
				RenderBus.GetCameraUp()));
		}
		case EPrimitiveType::EPT_Text:
		{
			const UTextRenderComponent* TextComp = static_cast<const UTextRenderComponent*>(PrimitiveComponent);
			return BuildQuadAABB(TextComp->GetTextMatrix());
		}
		case EPrimitiveType::EPT_SubUV:
		{
			const USubUVComponent* SubUVComp = static_cast<const USubUVComponent*>(PrimitiveComponent);
			return BuildQuadAABB(MakeViewSubUVSelectionMatrix(SubUVComp, RenderBus));
		}

		default:
			return PrimitiveComponent->GetWorldAABB();
		}
	}
} // namespace
