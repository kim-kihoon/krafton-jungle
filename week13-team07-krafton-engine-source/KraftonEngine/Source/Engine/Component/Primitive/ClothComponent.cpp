#include "Component/Primitive/ClothComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Render/Proxy/ClothSceneProxy.h"
#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Runtime/NvClothEnvironment.h"
#include "Runtime/Engine.h"

#include <NvCloth/Cloth.h>
#include <NvCloth/Fabric.h>
#include <NvCloth/Factory.h>
#include <NvCloth/PhaseConfig.h>
#include <NvCloth/Range.h>
#include <NvCloth/Solver.h>
#include <PxPhysicsAPI.h>
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace physx;

UClothComponent::UClothComponent()
{
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BuildClothTopology();
}

UClothComponent::~UClothComponent()
{
	ReleaseCloth();
}

void UClothComponent::BeginPlay()
{
	UPrimitiveComponent::BeginPlay();
	if (!MaterialPath.ToString().empty() && MaterialPath.ToString() != "None")
		ClothMaterial = FMaterialManager::Get().GetOrCreateMaterial(MaterialPath.ToString());

	BuildClothTopology();
	InitializeCloth();
}

void UClothComponent::EndPlay()
{
	ReleaseCloth();
	UPrimitiveComponent::EndPlay();
}

void UClothComponent::PostEditChangeProperty(const FPropertyChangedEvent& Event)
{
	bDeferClothInitialization = Event.ChangeType == EPropertyChangeType::Load;
	UPrimitiveComponent::PostEditChangeProperty(Event);
	bDeferClothInitialization = false;
}

void UClothComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);

	if (std::strcmp(PropertyName, "MaterialPath") == 0 || std::strcmp(PropertyName, "Material") == 0)
	{
		ClothMaterial = MaterialPath.ToString().empty() || MaterialPath.ToString() == "None"
			? nullptr : FMaterialManager::Get().GetOrCreateMaterial(MaterialPath.ToString());
		MarkProxyDirty(EDirtyFlag::Material);
		return;
	}

	if (std::strcmp(PropertyName, "ClothWidth") == 0 || std::strcmp(PropertyName, "ClothHeight") == 0 ||
		std::strcmp(PropertyName, "SegmentCountX") == 0 || std::strcmp(PropertyName, "SegmentCountY") == 0 ||
		std::strcmp(PropertyName, "ParticleMass") == 0)
	{
		BuildClothTopology();
		ReleaseCloth();
		if (!bDeferClothInitialization) InitializeCloth();
		MarkWorldBoundsDirty();
		return;
	}

	if (std::strcmp(PropertyName, "GravityScale") == 0 || std::strcmp(PropertyName, "DragCoefficient") == 0 ||
		std::strcmp(PropertyName, "Friction") == 0 || std::strcmp(PropertyName, "WindVelocity") == 0)
	{
		if (ClothInstance)
		{
			ClothInstance->setGravity(PxVec3(0.0f, 0.0f, -9.81f * GravityScale));
			ClothInstance->setDragCoefficient(DragCoefficient);
			ClothInstance->setFriction(Friction);
			ClothInstance->setWindVelocity(PxVec3(WindVelocity.X, WindVelocity.Y, WindVelocity.Z));
		}
	}
}

FPrimitiveSceneProxy* UClothComponent::CreateSceneProxy() { return new FClothSceneProxy(this); }

FMeshDataView UClothComponent::GetMeshDataView() const
{
	FMeshDataView View;
	View.VertexData = RenderVertices.empty() ? nullptr : RenderVertices.data();
	View.VertexCount = static_cast<uint32>(RenderVertices.size());
	View.Stride = sizeof(FVertexPNCTT);
	View.IndexData = RenderIndices.empty() ? nullptr : RenderIndices.data();
	View.IndexCount = static_cast<uint32>(RenderIndices.size());
	return View;
}

void UClothComponent::UpdateWorldAABB() const
{
	if (RenderVertices.empty())
	{
		UPrimitiveComponent::UpdateWorldAABB();
		return;
	}

	FVector Min = GetWorldMatrix().TransformPositionWithW(RenderVertices[0].Position);
	FVector Max = Min;
	for (const FVertexPNCTT& Vertex : RenderVertices)
	{
		const FVector WorldPosition = GetWorldMatrix().TransformPositionWithW(Vertex.Position);
		Min.X = (std::min)(Min.X, WorldPosition.X);
		Min.Y = (std::min)(Min.Y, WorldPosition.Y);
		Min.Z = (std::min)(Min.Z, WorldPosition.Z);
		Max.X = (std::max)(Max.X, WorldPosition.X);
		Max.Y = (std::max)(Max.Y, WorldPosition.Y);
		Max.Z = (std::max)(Max.Z, WorldPosition.Z);
	}

	WorldAABBMinLocation = Min;
	WorldAABBMaxLocation = Max;
	bWorldAABBDirty = false;
	bHasValidWorldAABB = true;
}

void UClothComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UPrimitiveComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bClothInitialized) InitializeCloth();
	if (!ClothInstance || !ClothSolver || DeltaTime <= 0.0f) return;

	ApplyAnchors();
	Simulate(DeltaTime);
	UpdateRenderVerticesFromSimulation();
	RenderRevision++;
	MarkWorldBoundsDirty();
}

void UClothComponent::AddReferencedObjects(FReferenceCollector& Collector)
{
	UPrimitiveComponent::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(ClothMaterial, "UClothComponent.ClothMaterial");
}

void UClothComponent::InitializeCloth()
{
	if (bClothInitialized || RenderVertices.empty() || RenderIndices.empty()) return;

	if (!FNvClothEnvironment::IsInitialized() && !FNvClothEnvironment::Initialize()) return;

	nv::cloth::Factory* ClothFactory = FNvClothEnvironment::GetFactory();
	if (!ClothFactory) return;

	BuildFabric();
	if (!ClothFabric) return;

	std::vector<PxVec4> Particles;
	Particles.resize(LocalSimulationPositions.size());

	const FMatrix WorldMatrix = GetWorldMatrix();
	WorldAnchorA = WorldMatrix.TransformPositionWithW(LocalSimulationPositions[AnchorParticleA]);
	WorldAnchorB = WorldMatrix.TransformPositionWithW(LocalSimulationPositions[AnchorParticleB]);

	for (size_t VertexIndex = 0; VertexIndex < LocalSimulationPositions.size(); ++VertexIndex)
	{
		const FVector WorldPosition = WorldMatrix.TransformPositionWithW(LocalSimulationPositions[VertexIndex]);
		Particles[VertexIndex] = PxVec4(WorldPosition.X, WorldPosition.Y, WorldPosition.Z, 1.0f / ParticleMass);
	}

	Particles[AnchorParticleA].w = 0.0f;
	Particles[AnchorParticleB].w = 0.0f;

	ResolveAnchorTargets(WorldAnchorA, WorldAnchorB);
	Particles[AnchorParticleA].x = WorldAnchorA.X; Particles[AnchorParticleA].y = WorldAnchorA.Y; Particles[AnchorParticleA].z = WorldAnchorA.Z;
	Particles[AnchorParticleB].x = WorldAnchorB.X; Particles[AnchorParticleB].y = WorldAnchorB.Y; Particles[AnchorParticleB].z = WorldAnchorB.Z;

	ClothInstance = ClothFactory->createCloth(nv::cloth::Range<const PxVec4>(Particles.data(), Particles.data() + Particles.size()), *ClothFabric);
	if (!ClothInstance) return;

	ClothInstance->setGravity(PxVec3(0.0f, 0.0f, -9.81f * GravityScale));
	ClothInstance->setDamping(PxVec3(0.1f, 0.1f, 0.1f));
	ClothInstance->setLinearDrag(PxVec3(0.1f, 0.1f, 0.1f));
	ClothInstance->setAngularDrag(PxVec3(0.1f, 0.1f, 0.1f));
	ClothInstance->setSolverFrequency(360.0f);
	ClothInstance->setStiffnessFrequency(10.0f);
	ClothInstance->setDragCoefficient(DragCoefficient);
	ClothInstance->setLiftCoefficient(0.0f);
	ClothInstance->setFriction(Friction);
	ClothInstance->setWindVelocity(PxVec3(WindVelocity.X, WindVelocity.Y, WindVelocity.Z));

	std::vector<nv::cloth::PhaseConfig> PhaseConfigs;
	PhaseConfigs.resize(ClothFabric->getNumPhases());
	for (uint32_t PhaseIndex = 0; PhaseIndex < PhaseConfigs.size(); ++PhaseIndex)
	{
		PhaseConfigs[PhaseIndex].mPhaseIndex = static_cast<uint16_t>(PhaseIndex);
		PhaseConfigs[PhaseIndex].mStiffness = 1.0f;
		PhaseConfigs[PhaseIndex].mStiffnessMultiplier = 1.0f;
		PhaseConfigs[PhaseIndex].mCompressionLimit = 0.5f;
		PhaseConfigs[PhaseIndex].mStretchLimit = 1.0f;
	}
	ClothInstance->setPhaseConfig(nv::cloth::Range<const nv::cloth::PhaseConfig>(PhaseConfigs.data(), PhaseConfigs.data() + PhaseConfigs.size()));

	ClothSolver = ClothFactory->createSolver();
	if (!ClothSolver) return;

	ClothSolver->addCloth(ClothInstance);
	UpdateRenderVerticesFromSimulation();
	RenderRevision++;
	bClothInitialized = true;
}

void UClothComponent::ReleaseCloth()
{
	bClothInitialized = false;
	if (ClothSolver && ClothInstance) ClothSolver->removeCloth(ClothInstance);

	if (ClothSolver) { delete ClothSolver; ClothSolver = nullptr; }
	if (ClothInstance) { delete ClothInstance; ClothInstance = nullptr; }
	if (ClothFabric) { ClothFabric->decRefCount(); ClothFabric = nullptr; }
}

void UClothComponent::BuildClothTopology()
{
	RenderIndices.clear();
	LocalSimulationPositions.clear();
	SimulationUVs.clear();

	const int32 SafeSegmentCountX = (std::max)(1, SegmentCountX);
	const int32 SafeSegmentCountY = (std::max)(1, SegmentCountY);
	SimGridWidth = SafeSegmentCountX + 1;
	SimGridHeight = SafeSegmentCountY + 1;

	const float StepX = ClothWidth / static_cast<float>(SafeSegmentCountX);
	const float StepY = ClothHeight / static_cast<float>(SafeSegmentCountY);

	LocalSimulationPositions.reserve(SimGridWidth * SimGridHeight);
	SimulationUVs.reserve(SimGridWidth * SimGridHeight);

	for (int32 Y = 0; Y < SimGridHeight; ++Y)
	{
		for (int32 X = 0; X < SimGridWidth; ++X)
		{
			LocalSimulationPositions.push_back(FVector(
				-ClothWidth * 0.5f + StepX * static_cast<float>(X), 0.0f, -StepY * static_cast<float>(Y)));
			SimulationUVs.push_back(FVector2(
				static_cast<float>(X) / static_cast<float>(SafeSegmentCountX), static_cast<float>(Y) / static_cast<float>(SafeSegmentCountY)));
		}
	}

	RenderIndices.reserve(SafeSegmentCountX * SafeSegmentCountY * 6);
	for (int32 Y = 0; Y < SafeSegmentCountY; ++Y)
	{
		for (int32 X = 0; X < SafeSegmentCountX; ++X)
		{
			const uint32 Index00 = static_cast<uint32>(Y * SimGridWidth + X);
			const uint32 Index10 = Index00 + 1;
			const uint32 Index01 = Index00 + static_cast<uint32>(SimGridWidth);
			const uint32 Index11 = Index01 + 1;

			RenderIndices.push_back(Index00); RenderIndices.push_back(Index01); RenderIndices.push_back(Index10);
			RenderIndices.push_back(Index10); RenderIndices.push_back(Index01); RenderIndices.push_back(Index11);
		}
	}

	AnchorParticleA = 0;
	AnchorParticleB = SafeSegmentCountX;
	BuildRenderVertices();
	TopologyRevision++;
}

void UClothComponent::BuildFabric()
{
	nv::cloth::Factory* ClothFactory = FNvClothEnvironment::GetFactory();
	const int32 VertexCountX = SimGridWidth;
	const int32 VertexCountY = SimGridHeight;

	if (!ClothFactory || VertexCountX <= 1 || VertexCountY <= 1 || LocalSimulationPositions.empty()) return;

	std::vector<uint32_t> PhaseIndices;
	std::vector<uint32_t> Sets;
	std::vector<float> RestValues;
	std::vector<uint32_t> Indices;
	std::vector<uint32_t> Anchors;
	std::vector<float> TetherLengths;
	std::vector<uint32_t> Triangles(RenderIndices.begin(), RenderIndices.end());

	auto AddConstraintSet = [&](auto&& Predicate)
	{
		const size_t RestStart = RestValues.size();
		for (int32 Y = 0; Y < VertexCountY; ++Y)
		{
			for (int32 X = 0; X < VertexCountX; ++X)
			{
				int32 A = -1; int32 B = -1;
				if (!Predicate(X, Y, VertexCountX, VertexCountY, A, B)) continue;
				const FVector Delta = LocalSimulationPositions[B] - LocalSimulationPositions[A];
				Indices.push_back(static_cast<uint32_t>(A));
				Indices.push_back(static_cast<uint32_t>(B));
				RestValues.push_back(Delta.Length());
			}
		}
		if (RestValues.size() > RestStart)
		{
			PhaseIndices.push_back(static_cast<uint32_t>(Sets.size()));
			Sets.push_back(static_cast<uint32_t>(RestValues.size()));
		}
	};

	AddConstraintSet([](int32 X, int32 Y, int32 VX, int32 VY, int32& OutA, int32& OutB) { if (X + 1 >= VX || (X & 1) != 0) return false; OutA = Y * VX + X; OutB = OutA + 1; return true; });
	AddConstraintSet([](int32 X, int32 Y, int32 VX, int32 VY, int32& OutA, int32& OutB) { if (X + 1 >= VX || (X & 1) == 0) return false; OutA = Y * VX + X; OutB = OutA + 1; return true; });
	AddConstraintSet([](int32 X, int32 Y, int32 VX, int32 VY, int32& OutA, int32& OutB) { if (Y + 1 >= VY || (Y & 1) != 0) return false; OutA = Y * VX + X; OutB = OutA + VX; return true; });
	AddConstraintSet([](int32 X, int32 Y, int32 VX, int32 VY, int32& OutA, int32& OutB) { if (Y + 1 >= VY || (Y & 1) == 0) return false; OutA = Y * VX + X; OutB = OutA + VX; return true; });
	AddConstraintSet([](int32 X, int32 Y, int32 VX, int32 VY, int32& OutA, int32& OutB) { if (X + 1 >= VX || Y + 1 >= VY || (X & 1) != 0) return false; OutA = Y * VX + X; OutB = OutA + VX + 1; return true; });
	AddConstraintSet([](int32 X, int32 Y, int32 VX, int32 VY, int32& OutA, int32& OutB) { if (X + 1 >= VX || Y + 1 >= VY || (X & 1) == 0) return false; OutA = Y * VX + X; OutB = OutA + VX + 1; return true; });
	AddConstraintSet([](int32 X, int32 Y, int32 VX, int32 VY, int32& OutA, int32& OutB) { if (X - 1 < 0 || Y + 1 >= VY || (X & 1) != 0) return false; OutA = Y * VX + X; OutB = OutA + VX - 1; return true; });
	AddConstraintSet([](int32 X, int32 Y, int32 VX, int32 VY, int32& OutA, int32& OutB) { if (X - 1 < 0 || Y + 1 >= VY || (X & 1) == 0) return false; OutA = Y * VX + X; OutB = OutA + VX - 1; return true; });
	AddConstraintSet([](int32 X, int32 Y, int32 VX, int32 VY, int32& OutA, int32& OutB) { if (X + 2 >= VX || (X & 1) != 0) return false; OutA = Y * VX + X; OutB = OutA + 2; return true; });
	AddConstraintSet([](int32 X, int32 Y, int32 VX, int32 VY, int32& OutA, int32& OutB) { if (X + 2 >= VX || (X & 1) == 0) return false; OutA = Y * VX + X; OutB = OutA + 2; return true; });
	AddConstraintSet([](int32 X, int32 Y, int32 VX, int32 VY, int32& OutA, int32& OutB) { if (Y + 2 >= VY || (Y & 1) != 0) return false; OutA = Y * VX + X; OutB = OutA + VX * 2; return true; });
	AddConstraintSet([](int32 X, int32 Y, int32 VX, int32 VY, int32& OutA, int32& OutB) { if (Y + 2 >= VY || (Y & 1) == 0) return false; OutA = Y * VX + X; OutB = OutA + VX * 2; return true; });

	Anchors.resize(LocalSimulationPositions.size());
	TetherLengths.resize(LocalSimulationPositions.size());
	for (int32 VertexIndex = 0; VertexIndex < static_cast<int32>(LocalSimulationPositions.size()); ++VertexIndex)
	{
		const float DistanceToA = FVector::Distance(LocalSimulationPositions[VertexIndex], LocalSimulationPositions[AnchorParticleA]);
		const float DistanceToB = FVector::Distance(LocalSimulationPositions[VertexIndex], LocalSimulationPositions[AnchorParticleB]);
		if (DistanceToA <= DistanceToB) { Anchors[VertexIndex] = static_cast<uint32_t>(AnchorParticleA); TetherLengths[VertexIndex] = DistanceToA; }
		else { Anchors[VertexIndex] = static_cast<uint32_t>(AnchorParticleB); TetherLengths[VertexIndex] = DistanceToB; }
	}

	ClothFabric = ClothFactory->createFabric(
		static_cast<uint32_t>(LocalSimulationPositions.size()),
		nv::cloth::Range<const uint32_t>(PhaseIndices.data(), PhaseIndices.data() + PhaseIndices.size()),
		nv::cloth::Range<const uint32_t>(Sets.data(), Sets.data() + Sets.size()),
		nv::cloth::Range<const float>(RestValues.data(), RestValues.data() + RestValues.size()),
		nv::cloth::Range<const float>(),
		nv::cloth::Range<const uint32_t>(Indices.data(), Indices.data() + Indices.size()),
		nv::cloth::Range<const uint32_t>(Anchors.data(), Anchors.data() + Anchors.size()),
		nv::cloth::Range<const float>(TetherLengths.data(), TetherLengths.data() + TetherLengths.size()),
		nv::cloth::Range<const uint32_t>(Triangles.data(), Triangles.data() + Triangles.size()));
}

void UClothComponent::BuildRenderVertices()
{
	RenderVertices.resize(LocalSimulationPositions.size());
	for (int32 VertexIndex = 0; VertexIndex < static_cast<int32>(LocalSimulationPositions.size()); ++VertexIndex)
	{
		RenderVertices[VertexIndex].Position = LocalSimulationPositions[VertexIndex];
		RenderVertices[VertexIndex].Normal = FVector(0.0f, 1.0f, 0.0f);
		RenderVertices[VertexIndex].Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		RenderVertices[VertexIndex].UV = SimulationUVs[VertexIndex];
		RenderVertices[VertexIndex].Tangent = FVector4(1.0f, 0.0f, 0.0f, 1.0f);
	}
	RebuildNormals();
	RenderRevision++;
}

void UClothComponent::UpdateRenderVerticesFromSimulation()
{
	if (!ClothInstance) return;

	const nv::cloth::Cloth& ConstCloth = *ClothInstance;
	nv::cloth::MappedRange<const PxVec4> Particles = ConstCloth.getCurrentParticles();
	const FMatrix InverseWorld = GetWorldInverseMatrix();

	for (int32 VertexIndex = 0; VertexIndex < static_cast<int32>(RenderVertices.size()); ++VertexIndex)
	{
		const FVector WorldPosition(Particles[VertexIndex].x, Particles[VertexIndex].y, Particles[VertexIndex].z);
		RenderVertices[VertexIndex].Position = InverseWorld.TransformPositionWithW(WorldPosition);
	}
	RebuildNormals();
}

void UClothComponent::RebuildNormals()
{
	for (FVertexPNCTT& Vertex : RenderVertices) Vertex.Normal = FVector::ZeroVector;

	for (int32 TriangleIndex = 0; TriangleIndex + 2 < static_cast<int32>(RenderIndices.size()); TriangleIndex += 3)
	{
		FVertexPNCTT& Vertex0 = RenderVertices[RenderIndices[TriangleIndex]];
		FVertexPNCTT& Vertex1 = RenderVertices[RenderIndices[TriangleIndex + 1]];
		FVertexPNCTT& Vertex2 = RenderVertices[RenderIndices[TriangleIndex + 2]];
		const FVector EdgeA = Vertex1.Position - Vertex0.Position;
		const FVector EdgeB = Vertex2.Position - Vertex0.Position;
		const FVector FaceNormal = EdgeA.Cross(EdgeB).GetSafeNormal();
		Vertex0.Normal += FaceNormal; Vertex1.Normal += FaceNormal; Vertex2.Normal += FaceNormal;
	}

	for (FVertexPNCTT& Vertex : RenderVertices)
	{
		Vertex.Normal = Vertex.Normal.GetSafeNormal();
		Vertex.Tangent = FVector4(1.0f, 0.0f, 0.0f, 1.0f);
	}
}

void UClothComponent::ResolveAnchorTargets(FVector& OutAnchorA, FVector& OutAnchorB) const
{
	const FMatrix WorldMatrix = GetWorldMatrix();
	OutAnchorA = WorldMatrix.TransformPositionWithW(LocalSimulationPositions[AnchorParticleA]);
	OutAnchorB = WorldMatrix.TransformPositionWithW(LocalSimulationPositions[AnchorParticleB]);
}

void UClothComponent::ApplyAnchors()
{
	if (!ClothInstance) return;

	FVector AnchorA = WorldAnchorA;
	FVector AnchorB = WorldAnchorB;
	ResolveAnchorTargets(AnchorA, AnchorB);

	nv::cloth::MappedRange<PxVec4> CurrentParticles = ClothInstance->getCurrentParticles();
	nv::cloth::MappedRange<PxVec4> PreviousParticles = ClothInstance->getPreviousParticles();

	CurrentParticles[AnchorParticleA] = PxVec4(AnchorA.X, AnchorA.Y, AnchorA.Z, 0.0f);
	CurrentParticles[AnchorParticleB] = PxVec4(AnchorB.X, AnchorB.Y, AnchorB.Z, 0.0f);
	PreviousParticles[AnchorParticleA] = CurrentParticles[AnchorParticleA];
	PreviousParticles[AnchorParticleB] = CurrentParticles[AnchorParticleB];
}

void UClothComponent::Simulate(float DeltaTime)
{
	if (!ClothSolver || !ClothInstance) return;
	if (ClothSolver->beginSimulation(DeltaTime))
	{
		const int32 ChunkCount = ClothSolver->getSimulationChunkCount();
		for (int32 ChunkIndex = 0; ChunkIndex < ChunkCount; ++ChunkIndex) ClothSolver->simulateChunk(ChunkIndex);
		ClothSolver->endSimulation();
	}
}

FBoundingBox UClothComponent::BuildSimulationBounds() const
{
	FBoundingBox Bounds;
	if (!ClothInstance)
	{
		for (const FVertexPNCTT& Vertex : RenderVertices) Bounds.Expand(GetWorldMatrix().TransformPositionWithW(Vertex.Position));
		return Bounds;
	}

	const nv::cloth::Cloth& ConstCloth = *ClothInstance;
	nv::cloth::MappedRange<const PxVec4> Particles = ConstCloth.getCurrentParticles();
	for (const PxVec4& Particle : Particles) Bounds.Expand(FVector(Particle.x, Particle.y, Particle.z));
	return Bounds;
}
