#include "Render/Particle/ParticleSpriteSmokeTest.h"

#include "Component/ActorComponent.h"
#include "Component/ParticleSystemComponent.h"
#include "Component/SceneComponent.h"
#include "Core/Log.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Object/Object.h"
#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleHelper.h"
#include "Render/Particle/ParticleDynamicData.h"
#include "Render/Proxy/ParticleSystemSceneProxy.h"

#include <cmath>
#include <utility>

namespace
{
	constexpr float kTwoPi = 6.28318530718f;

	// Build one sprite emitter snapshot — N stationary, camera-facing quads
	// arranged on a circle of given radius around WorldCenter. Particle positions
	// are world-space because FParticleSystemSceneProxy::UpdateTransform forces
	// PerObjectConstants to identity, so the caller must bake the owning
	// component's world location into the ring center.
	// Ownership of the returned ptr is passed to UpdateDynamicData.
	FDynamicSpriteEmitterData* BuildRingEmitter(int32 N, UMaterial* Material, float Radius, const FVector& WorldCenter)
	{
		if (N <= 0) return nullptr;

		auto* Emitter = new FDynamicSpriteEmitterData();
		Emitter->EmitterIndex = 0;

		FDynamicSpriteEmitterReplayData& Src = Emitter->Source;
		// eEmitterType is already DET_Sprite via the FDynamicSpriteEmitterReplayDataBase ctor.
		Src.MaterialInterface     = Material;
		Src.ActiveParticleCount   = N;
		Src.ParticleStride        = AlignParticleDataSize(static_cast<int32>(sizeof(FBaseParticle)), 16);
		Src.SortMode              = EParticleSortMode::PSORTMODE_ViewProjDepth;
		Src.SubImages_Horizontal  = 1;
		Src.SubImages_Vertical    = 1;
		Src.ScreenAlignment       = static_cast<uint8>(PSA_FacingCameraPosition);
		Src.EmitterOrigin         = WorldCenter;
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
			P->Size               = FVector(10.0f, 10.0f, 0.0f);
			P->BaseSize           = P->Size;
			P->Rotation           = FVector::ZeroVector;
			P->BaseRotationRate   = FVector::ZeroVector;
			P->RotationRate       = FVector::ZeroVector;
			// Cycle hue around the ring so it is obvious the per-particle color
			// path is exercised, not just a single constant.
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

UMaterial* ParticleSpriteSmokeTest::GetDefaultMaterial()
{
	return FMaterialManager::Get().GetOrCreateMaterial(
		FString("Asset/Materials/Editor/DefaultParticleSprite.mat"));
}

int32 ParticleSpriteSmokeTest::InjectIntoWorld(UWorld* World, UMaterial* Material,
                                                int32 N, float Radius)
{
	if (!World)
	{
		UE_LOG("[ParticleSmokeTest] FAIL: World is null");
		return 0;
	}
	if (!Material)
	{
		UE_LOG("[ParticleSmokeTest] FAIL: Material is null. Check Asset/Materials/Editor/DefaultParticleSprite.mat exists and parses.");
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
				UE_LOG("[ParticleSmokeTest] PSC on '%s' has no SceneProxy (component not registered with FScene?)",
					Actor->GetFName().ToString().c_str());
				continue;
			}
			++PSCsWithProxy;

			// Bake the component's world location into the ring center — the proxy
			// renders particle positions as world-space (identity model matrix).
			const FVector RingCenter = PSC->GetWorldLocation();
			UE_LOG("[ParticleSmokeTest]   '%s' PSC at world (%.1f, %.1f, %.1f); ring R=%.1f size=20",
				Actor->GetFName().ToString().c_str(),
				RingCenter.X, RingCenter.Y, RingCenter.Z, Radius);

			TArray<FDynamicEmitterDataBase*> Data;
			if (FDynamicSpriteEmitterData* Emitter = BuildRingEmitter(N, Material, Radius, RingCenter))
			{
				Data.push_back(Emitter);
			}

			Proxy->UpdateDynamicData(std::move(Data));
			Proxy->UpdateMaterial();
			Proxy->UpdateMesh();
			++Injected;
		}
	}

	const ID3D11ShaderResourceView* const* SRVs = Material->GetCachedSRVs();
	UE_LOG("[ParticleSmokeTest] World=%p WorldType=%d  ActorsScanned=%d PSCsFound=%d WithProxy=%d Injected=%d  Material=%p Pass=%d Shader=%p t0SRV=%p  N=%d Radius=%.1f",
		World,
		static_cast<int32>(World->GetWorldType()),
		ActorsScanned, PSCsFound, PSCsWithProxy, Injected,
		Material,
		static_cast<int32>(Material->GetRenderPass()),
		Material->GetShader(),
		SRVs ? SRVs[0] : nullptr,
		N, Radius);

	return Injected;
}
