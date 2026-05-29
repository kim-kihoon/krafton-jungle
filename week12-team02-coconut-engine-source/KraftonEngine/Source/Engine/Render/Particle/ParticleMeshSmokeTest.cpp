#include "Render/Particle/ParticleMeshSmokeTest.h"

#include "Component/ActorComponent.h"
#include "Component/ParticleSystemComponent.h"
#include "Component/SceneComponent.h"
#include "Core/Log.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Mesh/MeshManager.h"
#include "Mesh/StaticMesh.h"
#include "Object/Object.h"
#include "Particle/ParticleHelper.h"
#include "Render/Particle/ParticleDynamicData.h"
#include "Render/Proxy/ParticleSystemSceneProxy.h"

#include <cmath>
#include <utility>

namespace
{
	constexpr float kTwoPi = 6.28318530718f;

	// Build a mesh emitter snapshot — N instances of `Mesh` arranged on a circle
	// of given radius around WorldCenter. Per-particle Rotation cycles around the
	// ring so that the per-instance Z-axis rotation path is visibly exercised
	// (i.e. you can tell the Transform isn't just identity-with-translation).
	// Ownership of the returned ptr is transferred to UpdateDynamicData.
	FDynamicMeshEmitterData* BuildRingMeshEmitter(int32 N, UMaterial* Material,
		UStaticMesh* Mesh, float Radius, const FVector& WorldCenter)
	{
		if (N <= 0 || !Mesh) return nullptr;

		auto* Emitter = new FDynamicMeshEmitterData();
		Emitter->EmitterIndex = 0;

		FDynamicMeshEmitterReplayData& Src = Emitter->MeshSource;
		// eEmitterType is already DET_Mesh via the FDynamicMeshEmitterReplayData ctor.
		Src.MaterialInterface     = Material;
		Src.StaticMesh            = Mesh;
		Src.LODLevel              = 0;
		Src.ActiveParticleCount   = N;
		Src.ParticleStride        = AlignParticleDataSize(static_cast<int32>(sizeof(FBaseParticle)), 16);
		Src.SortMode              = EParticleSortMode::PSORTMODE_None;
		Src.BlendMode             = EBlendState::AlphaBlend;

		Src.DataContainer.Alloc(Src.ParticleStride * N, N);

		for (int32 i = 0; i < N; ++i)
		{
			FBaseParticle* P = reinterpret_cast<FBaseParticle*>(
				Src.DataContainer.ParticleData + Src.ParticleStride * i);

			const float t = (kTwoPi * static_cast<float>(i)) / static_cast<float>(N);
			P->Location           = WorldCenter
			                     + FVector(Radius * std::cos(t), Radius * std::sin(t), 0.0f);
			P->OldLocation        = P->Location;
			P->Velocity           = FVector::ZeroVector;
			P->BaseVelocity       = FVector::ZeroVector;
			// Non-uniform scale so Z rotation is visible on a symmetric sphere.
			P->Size               = FVector(5.0f, 5.0f, 5.0f);
			P->BaseSize           = P->Size;
			// Cycle Z rotation around the ring — verifies PackMeshEmitter's
			// Scale × RotationZ × Translation composition is being honored.
			P->Rotation           = FVector(0.0f, 0.0f, t);
			P->BaseRotationRate   = FVector::ZeroVector;
			P->RotationRate       = FVector::ZeroVector;
			// Hue cycle around the ring (also catches a stuck instanceColor path).
			P->Color              = FLinearColor(1.0f,
			                                     0.5f + 0.5f * std::cos(t),
			                                     0.5f + 0.5f * std::sin(t),
			                                     0.8f);
			P->BaseColor          = P->Color;
			P->RelativeTime       = 0.0f;
			P->OneOverMaxLifetime = 0.0f;
			P->Flags              = 0;

			Src.DataContainer.ParticleIndices[i] = static_cast<uint16>(i);
		}

		return Emitter;
	}
}

UMaterial* ParticleMeshSmokeTest::GetDefaultMaterial()
{
	return FMaterialManager::Get().GetOrCreateMaterial(
		FString("Asset/Materials/Editor/DefaultParticleMesh.mat"));
}

UStaticMesh* ParticleMeshSmokeTest::GetDefaultMesh(ID3D11Device* Device)
{
	if (!Device) return nullptr;
	return FMeshManager::LoadStaticMesh(
		FString("Asset/Mesh/BasicShape/Sphere_Lowpoly_StaticMesh.uasset"), Device);
}

int32 ParticleMeshSmokeTest::InjectIntoWorld(UWorld* World, UMaterial* Material,
	UStaticMesh* Mesh, int32 N, float Radius)
{
	if (!World)
	{
		UE_LOG("[ParticleMeshSmokeTest] FAIL: World is null");
		return 0;
	}
	if (!Material)
	{
		UE_LOG("[ParticleMeshSmokeTest] FAIL: Material is null. Check Asset/Materials/Editor/DefaultParticleMesh.mat exists and parses.");
		return 0;
	}
	if (!Mesh)
	{
		UE_LOG("[ParticleMeshSmokeTest] FAIL: Mesh is null. Check Asset/Mesh/BasicShape/Sphere_Lowpoly_StaticMesh.uasset loads.");
		return 0;
	}

	int32 ActorsScanned = 0;
	int32 PSCsFound = 0;
	int32 PSCsWithProxy = 0;
	int32 Injected = 0;

	for (AActor* Actor : World->GetActors())
	{
		if (!Actor) continue;
		++ActorsScanned;

		for (UActorComponent* Comp : Actor->GetComponents())
		{
			UParticleSystemComponent* PSC = Cast<UParticleSystemComponent>(Comp);
			if (!PSC) continue;
			++PSCsFound;

			FParticleSystemSceneProxy* Proxy = PSC->GetSceneProxy();
			if (!Proxy)
			{
				UE_LOG("[ParticleMeshSmokeTest] PSC on '%s' has no SceneProxy (component not registered with FScene?)",
					Actor->GetFName().ToString().c_str());
				continue;
			}
			++PSCsWithProxy;

			// Same world-space convention as sprites — the proxy uses identity
			// PerObjectConstants, so we bake the component's world location
			// into each particle's Location at snapshot time.
			const FVector RingCenter = PSC->GetWorldLocation();
			UE_LOG("[ParticleMeshSmokeTest]   '%s' PSC at world (%.1f, %.1f, %.1f); ring R=%.1f N=%d",
				Actor->GetFName().ToString().c_str(),
				RingCenter.X, RingCenter.Y, RingCenter.Z, Radius, N);

			TArray<FDynamicEmitterDataBase*> Data;
			if (FDynamicMeshEmitterData* Emitter = BuildRingMeshEmitter(N, Material, Mesh, Radius, RingCenter))
			{
				Data.push_back(Emitter);
			}

			Proxy->UpdateDynamicData(std::move(Data));
			Proxy->UpdateMaterial();
			Proxy->UpdateMesh();
			++Injected;
		}
	}

	UE_LOG("[ParticleMeshSmokeTest] World=%p WorldType=%d  ActorsScanned=%d PSCsFound=%d WithProxy=%d Injected=%d  Material=%p Pass=%d Mesh=%p  N=%d Radius=%.1f",
		World,
		static_cast<int32>(World->GetWorldType()),
		ActorsScanned, PSCsFound, PSCsWithProxy, Injected,
		Material,
		static_cast<int32>(Material->GetRenderPass()),
		Mesh,
		N, Radius);

	return Injected;
}
