#pragma once

#include "Component/PrimitiveComponent.h"
#include "Object/Ptr/ObjectPtr.h"
#include "Object/Ptr/SoftObjectPtr.h"
#include "Render/Types/VertexTypes.h"
#include <vector>

#include "Source/Engine/Component/Primitive/ClothComponent.generated.h"

class UMaterial;
class FClothSceneProxy;
class AActor;

namespace physx {
	class PxVec4;
	class PxVec3;
}

namespace nv {
namespace cloth {
class Cloth;
class Fabric;
class Solver;
}
}

UCLASS()
class UClothComponent : public UPrimitiveComponent
{
public:
	GENERATED_BODY()

	UClothComponent();
	~UClothComponent() override;

	void BeginPlay() override;
	void EndPlay() override;
	void PostEditChangeProperty(const FPropertyChangedEvent& Event) override;
	void PostEditProperty(const char* PropertyName) override;

	FPrimitiveSceneProxy* CreateSceneProxy() override;
	FMeshDataView GetMeshDataView() const override;
	void UpdateWorldAABB() const override;

	const TArray<FVertexPNCTT>& GetRenderVertices() const { return RenderVertices; }
	const TArray<uint32>& GetRenderIndices() const { return RenderIndices; }
	uint64 GetRenderRevision() const { return RenderRevision; }
	uint64 GetTopologyRevision() const { return TopologyRevision; }
	UMaterial* GetClothMaterial() const { return ClothMaterial.Get(); }

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
	void InitializeCloth();
	void ReleaseCloth();
	void BuildClothTopology();
	void BuildFabric();
	void BuildRenderVertices();
	void UpdateRenderVerticesFromSimulation();
	void RebuildNormals();
	void ResolveAnchorTargets(FVector& OutAnchorA, FVector& OutAnchorB) const;
	void ApplyAnchors();
	void Simulate(float DeltaTime);
	FBoundingBox BuildSimulationBounds() const;

private:
	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Width", Min=1.0f, Max=0.0f, Speed=0.01f)
	float ClothWidth = 2.0f;

	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Height", Min=1.0f, Max=0.0f, Speed=0.01f)
	float ClothHeight = 3.0f;

	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Segments X", Min=1, Max=0)
	int32 SegmentCountX = 15;

	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Segments Y", Min=1, Max=0)
	int32 SegmentCountY = 20;

	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Particle Mass", Min=0.001f, Max=0.0f, Speed=0.01f)
	float ParticleMass = 1.0f;

	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Gravity Scale", Min=0.0f, Max=0.0f, Speed=0.01f)
	float GravityScale = 1.0f;

	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Wind Velocity", Speed=0.1f)
	FVector WindVelocity = FVector::ZeroVector;

	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Drag", Min=0.0f, Max=1.0f, Speed=0.01f)
	float DragCoefficient = 0.05f;

	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Friction", Min=0.0f, Max=1.0f, Speed=0.01f)
	float Friction = 0.25f;

	UPROPERTY(Edit, Save, Category="Rendering", DisplayName="Material", AssetType="Material")
	FSoftObjectPtr MaterialPath = "None";

	UPROPERTY(Transient, Category="Rendering")
	TObjectPtr<UMaterial> ClothMaterial = nullptr;

	TArray<FVertexPNCTT> RenderVertices;
	TArray<uint32> RenderIndices;
	TArray<FVector> LocalSimulationPositions;
	TArray<FVector2> SimulationUVs;

	FVector WorldAnchorA = FVector::ZeroVector;
	FVector WorldAnchorB = FVector::ZeroVector;
	int32 AnchorParticleA = 0;
	int32 AnchorParticleB = 0;
	int32 SimGridWidth = 0;
	int32 SimGridHeight = 0;

	nv::cloth::Solver* ClothSolver = nullptr;
	nv::cloth::Fabric* ClothFabric = nullptr;
	nv::cloth::Cloth* ClothInstance = nullptr;

	uint64 RenderRevision = 0;
	uint64 TopologyRevision = 0;
	bool bClothInitialized = false;
	bool bDeferClothInitialization = false;
};
