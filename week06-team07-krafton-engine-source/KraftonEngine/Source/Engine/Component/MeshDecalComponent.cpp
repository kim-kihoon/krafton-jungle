#include "MeshDecalComponent.h"
#include "StaticMeshComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Engine/Runtime/Engine.h"
#include "Collision/RayUtils.h"
#include "Render/Proxy/DecalMeshSceneProxy.h"
#include "Render/Pipeline/RenderBus.h"
#include "Resource/ResourceManager.h"
#include "Editor/UI/EditorConsoleWidget.h"

IMPLEMENT_CLASS(UMeshDecalComponent, UPrimitiveComponent)
namespace
{
	constexpr float MeshDecalSurfaceOffset = 0.001f;
	constexpr float MeshDecalLocalHalfExtent = 0.5f;

	const FVector MeshDecalLocalExtents = FVector(MeshDecalLocalHalfExtent, MeshDecalLocalHalfExtent, MeshDecalLocalHalfExtent);

	FVector2 ComputeDecalProjectedUV(const FVector& DecalLocalPos, const FVector& HalfExtent)
	{
		const float U = 0.5f + (HalfExtent.Y != 0.0f ? (DecalLocalPos.Y / (HalfExtent.Y * 2.0f)) : 0.0f);
		const float V = 0.5f - (HalfExtent.Z != 0.0f ? (DecalLocalPos.Z / (HalfExtent.Z * 2.0f)) : 0.0f);
		return FVector2(U, V);
	}

	FVertexPNCT MakeDecalVertex(const FVector& DecalLocalPos, const FVector& DecalLocalNormal, const FVector& HalfExtent)
	{
		FVertexPNCT Vertex = {};
		Vertex.Normal = DecalLocalNormal.Normalized();
		Vertex.Position = DecalLocalPos + Vertex.Normal * MeshDecalSurfaceOffset;
		Vertex.Color = FVector4(1.f, 1.f, 1.f, 1.f);
		Vertex.UV = ComputeDecalProjectedUV(DecalLocalPos, HalfExtent);
		return Vertex;
	}

	bool IsDecalFacingTriangle(const FVector& N0, const FVector& N1, const FVector& N2)
	{
		constexpr float ProjectionDotThreshold = 0.1f;
		FVector A = N1 - N0;
		FVector B = N2 - N0;
		FVector AvgNormal = A.Cross(B).Normalized();
		return AvgNormal.Dot(FVector(1.f, 0.f, 0.f)) <= ProjectionDotThreshold;
	}

	void AddBoxEdge(FRenderBus& RenderBus, const FVector& Start, const FVector& End, const FColor& Color)
	{
		FDebugLineEntry Entry = {};
		Entry.Start = Start;
		Entry.End = End;
		Entry.Color = Color;
		RenderBus.AddDebugLineEntry(std::move(Entry));
	}
}

FPrimitiveSceneProxy* UMeshDecalComponent::CreateSceneProxy()
{
	SetTexture(TextureName);
	SceneProxy = new FDecalMeshSceneProxy(this);
	return SceneProxy;
}

UMeshDecalComponent::UMeshDecalComponent()
{
	LocalExtents = MeshDecalLocalExtents;
}

void UMeshDecalComponent::SetTexture(const FName& InTextureName)
{
	TextureName = InTextureName;
	CachedTexture = FResourceManager::Get().FindTexture(InTextureName);

	if (SceneProxy)
	{
		static_cast<FDecalMeshSceneProxy*>(SceneProxy)->UpdateMaterial();
	}
	MarkProxyDirty(EDirtyFlag::Material);
}

ID3D11ShaderResourceView* UMeshDecalComponent::GetSRV()
{
	if (!CachedTexture && TextureName != "None")
	{
		SetTexture(TextureName);
	}
	return CachedTexture ? CachedTexture->SRV : nullptr;
}

void UMeshDecalComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UPrimitiveComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Texture", EPropertyType::Name, &TextureName });
	OutProps.push_back({ "Fade", EPropertyType::ByteBool, &bFade });
	OutProps.push_back({ "Opacity", EPropertyType::Float, &Opacity });
	OutProps.push_back({ "Opacity Rate", EPropertyType::Float, &OpacityRate });
}

void UMeshDecalComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);

	if (strcmp(PropertyName, "Texture") == 0)
	{
		SetTexture(TextureName);
	}
}

void UMeshDecalComponent::PostDuplicate()
{
	UPrimitiveComponent::PostDuplicate();
	LocalExtents = MeshDecalLocalExtents;
	SetTexture(TextureName);
	UpdateDecalMeshData();
}

void UMeshDecalComponent::CollectEditorVisualizations(FRenderBus& RenderBus) const
{
	static constexpr int32 BoxEdges[][2] = {
		{0, 1}, {1, 2}, {2, 3}, {3, 0},
		{4, 5}, {5, 6}, {6, 7}, {7, 4},
		{0, 4}, {1, 5}, {2, 6}, {3, 7}
	};

	const FVector LocalCorners[8] = {
		FVector(-LocalExtents.X, -LocalExtents.Y, -LocalExtents.Z),
		FVector(LocalExtents.X, -LocalExtents.Y, -LocalExtents.Z),
		FVector(LocalExtents.X,  LocalExtents.Y, -LocalExtents.Z),
		FVector(-LocalExtents.X,  LocalExtents.Y, -LocalExtents.Z),
		FVector(-LocalExtents.X, -LocalExtents.Y,  LocalExtents.Z),
		FVector(LocalExtents.X, -LocalExtents.Y,  LocalExtents.Z),
		FVector(LocalExtents.X,  LocalExtents.Y,  LocalExtents.Z),
		FVector(-LocalExtents.X,  LocalExtents.Y,  LocalExtents.Z),
	};

	FVector WorldCorners[8] = {};
	const FMatrix WorldMatrix = GetWorldMatrix();
	for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
	{
		WorldCorners[CornerIndex] = WorldMatrix.TransformPositionWithW(LocalCorners[CornerIndex]);
	}

	const FColor BoxColor(255, 180, 0, 255);
	for (const int32* Edge : BoxEdges)
	{
		AddBoxEdge(RenderBus, WorldCorners[Edge[0]], WorldCorners[Edge[1]], BoxColor);
	}
}

void UMeshDecalComponent::BeginPlay()
{
	UpdateDecalMeshData();
	UPrimitiveComponent::BeginPlay();
}


void UMeshDecalComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	Opacity = std::max(0.f, Opacity - DeltaTime * OpacityRate);
	SceneProxy->MarkPerObjectCBDirty();
	static_cast<FDecalMeshSceneProxy*>(SceneProxy)->UpdateOpacity();
	static_cast<FDecalMeshSceneProxy*>(SceneProxy)->UpdateFade();
}

static FArchive& operator<<(FArchive& Ar, FMaterialSlot& Slot)
{
	Ar << Slot.Path;
	Ar << Slot.bUVScroll;
	return Ar;
}

void UMeshDecalComponent::Serialize(FArchive& Ar)
{
	UPrimitiveComponent::Serialize(Ar);
	Ar << TextureName;
	Ar << Opacity;
	Ar << OpacityRate;
	Ar << bFade;

	if (Ar.IsLoading())
	{
		SetTexture(TextureName);
	}
}

void UMeshDecalComponent::OnTransformDirty()
{
	UPrimitiveComponent::OnTransformDirty();
	UpdateDecalMeshData();
}

bool UMeshDecalComponent::LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult)
{
	FRay LocalRay = {};
	LocalRay.Origin = GetWorldInverseMatrix().TransformPositionWithW(Ray.Origin);
	LocalRay.Direction = GetWorldInverseMatrix().TransformVector(Ray.Direction);
	LocalRay.Direction.Normalize();

	float LocalTMin = 0.0f;
	float LocalTMax = 0.0f;
	const FVector BoxMin(-LocalExtents.X, -LocalExtents.Y, -LocalExtents.Z);
	const FVector BoxMax(LocalExtents.X, LocalExtents.Y, LocalExtents.Z);
	if (!FRayUtils::IntersectRayAABB(LocalRay, BoxMin, BoxMax, LocalTMin, LocalTMax))
	{
		return false;
	}

	const float LocalHitDistance = (LocalTMin >= 0.0f) ? LocalTMin : LocalTMax;
	if (LocalHitDistance < 0.0f)
	{
		return false;
	}

	const FVector LocalHitPoint = LocalRay.Origin + LocalRay.Direction * LocalHitDistance;
	FVector LocalNormal = FVector(0.0f, 0.0f, 0.0f);
	constexpr float NormalEpsilon = 0.001f;

	if (std::abs(LocalHitPoint.X - BoxMax.X) <= NormalEpsilon) LocalNormal = FVector(1.0f, 0.0f, 0.0f);
	else if (std::abs(LocalHitPoint.X - BoxMin.X) <= NormalEpsilon) LocalNormal = FVector(-1.0f, 0.0f, 0.0f);
	else if (std::abs(LocalHitPoint.Y - BoxMax.Y) <= NormalEpsilon) LocalNormal = FVector(0.0f, 1.0f, 0.0f);
	else if (std::abs(LocalHitPoint.Y - BoxMin.Y) <= NormalEpsilon) LocalNormal = FVector(0.0f, -1.0f, 0.0f);
	else if (std::abs(LocalHitPoint.Z - BoxMax.Z) <= NormalEpsilon) LocalNormal = FVector(0.0f, 0.0f, 1.0f);
	else if (std::abs(LocalHitPoint.Z - BoxMin.Z) <= NormalEpsilon) LocalNormal = FVector(0.0f, 0.0f, -1.0f);

	const FMatrix WorldMatrix = GetWorldMatrix();
	const FVector WorldHitPoint = WorldMatrix.TransformPositionWithW(LocalHitPoint);
	const FVector WorldNormal = WorldMatrix.TransformVector(LocalNormal).Normalized();

	OutHitResult.HitComponent = this;
	OutHitResult.Distance = FVector::Distance(Ray.Origin, WorldHitPoint);
	OutHitResult.WorldHitLocation = WorldHitPoint;
	OutHitResult.WorldNormal = WorldNormal;
	OutHitResult.FaceIndex = -1;
	OutHitResult.bHit = true;
	return true;
}

TArray<UPrimitiveComponent*> UMeshDecalComponent::GetCandidates() const
{
	FOBB OBB = FOBB::FromAABB(GetWorldBoundingBox());
	FMatrix Rot = GetRelativeRotation().ToMatrix();
	OBB.Axes[0] = GetForwardVector().Normalized();
	OBB.Axes[1] = GetRightVector().Normalized();
	OBB.Axes[2] = GetUpVector().Normalized();
	return GetOwner()->GetWorld()->QueryOBB(OBB);
}

void UMeshDecalComponent::UpdateDecalMeshData()
{
	DecalMeshData.Indices.clear();
	DecalMeshData.Vertices.clear();
	MeshBuffer.Release();

	FMatrix WorldToDecalLocal = GetWorldMatrix().GetInverse();

	//SAT를 통해 후보가 될 Primitive들을 가져오기.
	if (!GetOwner()) return;
	if (!GetOwner()->GetWorld()) return;
	TArray<UPrimitiveComponent*> Candidates = GetCandidates();

	for (UPrimitiveComponent* Primitive : Candidates)
	{
		if (Primitive->GetOwner() == GetOwner()) continue;
		UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(Primitive);
		if (!StaticMeshComp) continue;

		FMatrix PrimitiveWorld = StaticMeshComp->GetWorldMatrix();
		TArray<FNormalVertex>& OriginVertices = StaticMeshComp->GetStaticMesh()->GetStaticMeshAsset()->Vertices;
		TArray<uint32>& OriginIndices = StaticMeshComp->GetStaticMesh()->GetStaticMeshAsset()->Indices;

		for (int i = 0; i + 2 < OriginIndices.size(); i += 3)
		{
			//매쉬의 로컬에서의 정점 좌표와 노말
			FVector PrimOriginPos0 = OriginVertices[OriginIndices[i]].pos;
			FVector PrimOriginPos1 = OriginVertices[OriginIndices[i + 1]].pos;
			FVector PrimOriginPos2 = OriginVertices[OriginIndices[i + 2]].pos;

			FVector PrimOriginNormal0 = OriginVertices[OriginIndices[i]].normal;
			FVector PrimOriginNormal1 = OriginVertices[OriginIndices[i + 1]].normal;
			FVector PrimOriginNormal2 = OriginVertices[OriginIndices[i + 2]].normal;

			//매쉬의 월드에서의 정점 좌표와 노말
			FVector PrimWorldPos0 = PrimitiveWorld.TransformPositionWithW(PrimOriginPos0);
			FVector PrimWorldPos1 = PrimitiveWorld.TransformPositionWithW(PrimOriginPos1);
			FVector PrimWorldPos2 = PrimitiveWorld.TransformPositionWithW(PrimOriginPos2);

			FVector PrimNormal0 = PrimitiveWorld.TransformVector(PrimOriginNormal0);
			FVector PrimNormal1 = PrimitiveWorld.TransformVector(PrimOriginNormal1);
			FVector PrimNormal2 = PrimitiveWorld.TransformVector(PrimOriginNormal2);

			//매쉬의 DecalLocal에서의 정점 좌표와 노말
			FVector PrimDecalPos0 = WorldToDecalLocal.TransformPositionWithW(PrimWorldPos0);
			FVector PrimDecalPos1 = WorldToDecalLocal.TransformPositionWithW(PrimWorldPos1);
			FVector PrimDecalPos2 = WorldToDecalLocal.TransformPositionWithW(PrimWorldPos2);

			FVector PrimDecalNormal0 = WorldToDecalLocal.TransformVector(PrimNormal0);
			FVector PrimDecalNormal1 = WorldToDecalLocal.TransformVector(PrimNormal1);
			FVector PrimDecalNormal2 = WorldToDecalLocal.TransformVector(PrimNormal2);

			if (!IsDecalFacingTriangle(PrimDecalPos0, PrimDecalPos1, PrimDecalPos2))
			{
				continue;
			}

			FVertexPNCT PrimDecalVertex0 = MakeDecalVertex(PrimDecalPos0, PrimDecalNormal0, MeshDecalLocalExtents);
			FVertexPNCT PrimDecalVertex1 = MakeDecalVertex(PrimDecalPos1, PrimDecalNormal1, MeshDecalLocalExtents);
			FVertexPNCT PrimDecalVertex2 = MakeDecalVertex(PrimDecalPos2, PrimDecalNormal2, MeshDecalLocalExtents);

			TArray<FVertexPNCT> ClippedVertices;
			ClipByDecalLocalOBB(ClippedVertices, PrimDecalVertex0, PrimDecalVertex1, PrimDecalVertex2);

			//정점이 3개 이하면 다각형이 아니니 패스
			if (ClippedVertices.size() < 3)
			{
				continue;
			}

			const uint32 BaseVertexIndex = static_cast<uint32>(DecalMeshData.Vertices.size());
			DecalMeshData.Vertices.insert(DecalMeshData.Vertices.end(), ClippedVertices.begin(), ClippedVertices.end());

			for (uint32 FanIndex = 1; FanIndex + 1 < static_cast<uint32>(ClippedVertices.size()); ++FanIndex)
			{
				DecalMeshData.Indices.push_back(BaseVertexIndex);
				DecalMeshData.Indices.push_back(BaseVertexIndex + FanIndex);
				DecalMeshData.Indices.push_back(BaseVertexIndex + FanIndex + 1);
			}
		}
	}

	if (GEngine && !DecalMeshData.Vertices.empty())
	{
		ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
		if (Device)
		{
			MeshBuffer.Create(Device, DecalMeshData);
		}
	}

	MarkProxyDirty(EDirtyFlag::Mesh);
}

void UMeshDecalComponent::ClipByDecalLocalOBB(TArray<FVertexPNCT>& OutClippedVertices, const FVertexPNCT& P0, const FVertexPNCT& P1, const FVertexPNCT& P2)
{
	TArray<FVertexPNCT> InputVertices = { P0, P1, P2 };
	TArray<FVertexPNCT> TempVertices;

	auto ClipPolygonByPlane = [&](float HExtent, uint32 AxisType, bool Larger)
		{
			if (InputVertices.empty())
			{
				return;
			}

			TempVertices.clear();
			const int32 VertexCount = static_cast<int32>(InputVertices.size());
			for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
			{
				const FVertexPNCT& Start = InputVertices[VertexIndex];
				const FVertexPNCT& End = InputVertices[(VertexIndex + 1) % VertexCount];
				ClipByAxis(HExtent, AxisType, Start, End, Larger, TempVertices);
			}

			InputVertices = TempVertices;
		};

	ClipPolygonByPlane(-MeshDecalLocalHalfExtent, 0, true);
	ClipPolygonByPlane(MeshDecalLocalHalfExtent, 0, false);

	ClipPolygonByPlane(-MeshDecalLocalHalfExtent, 1, true);
	ClipPolygonByPlane(MeshDecalLocalHalfExtent, 1, false);

	ClipPolygonByPlane(-MeshDecalLocalHalfExtent, 2, true);
	ClipPolygonByPlane(MeshDecalLocalHalfExtent, 2, false);

	OutClippedVertices = InputVertices;
}

void UMeshDecalComponent::ClipByAxis(float HExtent, uint32 AxisType,
	const FVertexPNCT& Start, const FVertexPNCT& End,
	bool Larger, TArray<FVertexPNCT>& OutClippedVertices)
{
	float AxisValueStart = (AxisType == 0) ? Start.Position.X : (AxisType == 1) ? Start.Position.Y : Start.Position.Z;
	float AxisValueEnd = (AxisType == 0) ? End.Position.X : (AxisType == 1) ? End.Position.Y : End.Position.Z;

	bool bStartInside = Larger ? (AxisValueStart >= HExtent) : (AxisValueStart <= HExtent);
	bool bEndInside = Larger ? (AxisValueEnd >= HExtent) : (AxisValueEnd <= HExtent);

	auto Intersect = [&]() -> FVertexPNCT
		{
			float Denom = AxisValueEnd - AxisValueStart;
			if (Denom == 0.0f)
			{
				return Start;
			}

			float t = (HExtent - AxisValueStart) / Denom;

			FVertexPNCT Intersected = {};
			Intersected.Normal = (Start.Normal + (End.Normal - Start.Normal) * t).Normalized();
			Intersected.Position = Start.Position + (End.Position - Start.Position) * t;
			Intersected.Position += Intersected.Normal * MeshDecalSurfaceOffset;
			Intersected.Color = Start.Color + (End.Color - Start.Color) * t;
			Intersected.UV = ComputeDecalProjectedUV(Intersected.Position - Intersected.Normal * MeshDecalSurfaceOffset, MeshDecalLocalExtents);
			return Intersected;
		};

	// 안 -> 안 : end 추가
	if (bStartInside && bEndInside)
	{
		OutClippedVertices.push_back(End);
	}
	// 안 -> 밖 : 교차점만 추가
	else if (bStartInside && !bEndInside)
	{
		OutClippedVertices.push_back(Intersect());
	}
	// 밖 -> 안 : 교차점 + end 추가
	else if (!bStartInside && bEndInside)
	{
		OutClippedVertices.push_back(Intersect());
		OutClippedVertices.push_back(End);
	}
	// 밖 -> 밖 : 아무것도 안함
}
