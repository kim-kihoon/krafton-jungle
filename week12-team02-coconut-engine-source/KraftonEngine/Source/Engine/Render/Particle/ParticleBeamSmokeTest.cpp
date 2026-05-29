#include "Render/Particle/ParticleBeamSmokeTest.h"

#include "Component/ActorComponent.h"
#include "Component/ParticleSystemComponent.h"
#include "Component/SceneComponent.h"
#include "Core/Log.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Object/Object.h"
#include "Particle/ParticleHelper.h"
#include "Render/Particle/ParticleDynamicData.h"
#include "Render/Proxy/ParticleSystemSceneProxy.h"

#include <cmath>
#include <utility>

namespace
{
	constexpr float kTwoPi = 6.28318530718f;

	// Build the starburst as one emitter with N beams. Per-emitter state
	// (Sheets, InterpolationPoints, TextureTile) lives on the replay data;
	// per-beam state (Source/Target/Color/Width/taper) lives in Beams. The
	// packer tessellates every beam into one contiguous slice of the proxy's
	// shared VB/IB and the result is a single DrawIndexed.
	//
	// Positions are world-space because FParticleSystemSceneProxy uses an
	// identity model matrix, so the owning component's world location is baked
	// into each beam at snapshot time.
	FDynamicBeamEmitterData* BuildStarburstEmitter(int32 N, UMaterial* Material,
		float Radius, const FVector& WorldCenter)
	{
		if (N <= 0) return nullptr;

		auto* Emitter = new FDynamicBeamEmitterData();
		Emitter->EmitterIndex = 0;

		FDynamicBeamEmitterReplayData& Src = Emitter->BeamSource;
		Src.MaterialInterface    = Material;
		Src.ParticleStride       = AlignParticleDataSize(static_cast<int32>(sizeof(FBaseParticle)), 16);
		Src.SortMode             = EParticleSortMode::PSORTMODE_None;
		Src.BlendMode            = Material ? Material->GetBlendState() : EBlendState::Additive;

		Src.InterpolationPoints  = 8;
		Src.Sheets               = 1;
		Src.TextureTile          = 20;
		Src.TextureTileDistance  = 0.0f;
		Src.LogicalBeamCount     = N;
		Src.ActiveParticleCount  = N * Src.Sheets;
		Src.bRenderGeometry      = true;

		Src.Beams.reserve(N);
		for (int32 i = 0; i < N; ++i)
		{
			const float t = (kTwoPi * static_cast<float>(i)) / static_cast<float>(N);

			FBeamInstanceData Beam;
			Beam.Source = WorldCenter;
			Beam.Target = WorldCenter + FVector(Radius * std::cos(t), Radius * std::sin(t), 0.0f);
			// Hue cycle around the ring exercises the per-beam color path.
			Beam.Color = FVector(1.0f,
			                     0.5f + 0.5f * std::cos(t),
			                     0.5f + 0.5f * std::sin(t));
			Beam.Alpha       = 0.8f;
			Beam.Width       = 4.0f;
			// Alternate taper so the packer's per-beam taper branch is hit.
			Beam.TaperMethod = (i & 1) == 0 ? PEBTM_Full : PEBTM_None;
			Beam.TaperFactor = 1.0f;
			Beam.TaperScale  = 1.0f;
			Beam.BeamProgress = 1.0f;
			Src.Beams.push_back(Beam);
		}

		// Inert FBaseParticle payload kept for consistency with other emitter
		// types; the beam packer reads Beams directly, not these bytes.
		Src.DataContainer.Alloc(Src.ParticleStride, 1);
		FBaseParticle* P = reinterpret_cast<FBaseParticle*>(Src.DataContainer.ParticleData);
		P->Location           = WorldCenter;
		P->OldLocation        = P->Location;
		P->Velocity           = FVector::ZeroVector;
		P->BaseVelocity       = FVector::ZeroVector;
		P->Size               = FVector(4.0f, 4.0f, 0.0f);
		P->BaseSize           = P->Size;
		P->Rotation           = FVector::ZeroVector;
		P->BaseRotationRate   = FVector::ZeroVector;
		P->RotationRate       = FVector::ZeroVector;
		P->Color              = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
		P->BaseColor          = P->Color;
		P->RelativeTime       = 0.0f;
		P->OneOverMaxLifetime = 0.0f;
		P->Flags              = 0;
		Src.DataContainer.ParticleIndices[0] = 0;

		return Emitter;
	}
}

UMaterial* ParticleBeamSmokeTest::GetDefaultMaterial()
{
	return FMaterialManager::Get().GetOrCreateMaterial(
		FString("Asset/Materials/Editor/DefaultParticleBeam.mat"));
}

int32 ParticleBeamSmokeTest::InjectIntoWorld(UWorld* World, UMaterial* Material,
                                              int32 N, float Radius)
{
	if (!World)
	{
		UE_LOG("[ParticleBeamSmokeTest] FAIL: World is null");
		return 0;
	}
	if (!Material)
	{
		UE_LOG("[ParticleBeamSmokeTest] FAIL: Material is null. Check Asset/Materials/Editor/DefaultParticleBeam.mat exists and parses.");
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
				UE_LOG("[ParticleBeamSmokeTest] PSC on '%s' has no SceneProxy (component not registered with FScene?)",
					Actor->GetFName().ToString().c_str());
				continue;
			}
			++PSCsWithProxy;

			const FVector RingCenter = PSC->GetWorldLocation();
			UE_LOG("[ParticleBeamSmokeTest]   '%s' PSC at world (%.1f, %.1f, %.1f); starburst R=%.1f N=%d",
				Actor->GetFName().ToString().c_str(),
				RingCenter.X, RingCenter.Y, RingCenter.Z, Radius, N);

			TArray<FDynamicEmitterDataBase*> Data;
			if (FDynamicBeamEmitterData* Emitter = BuildStarburstEmitter(N, Material, Radius, RingCenter))
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
	UE_LOG("[ParticleBeamSmokeTest] World=%p WorldType=%d  ActorsScanned=%d PSCsFound=%d WithProxy=%d Injected=%d  Material=%p Pass=%d Shader=%p t0SRV=%p  N=%d Radius=%.1f",
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
