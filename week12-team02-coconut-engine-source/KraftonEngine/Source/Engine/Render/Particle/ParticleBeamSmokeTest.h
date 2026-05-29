#pragma once

#include "Core/CoreTypes.h"

class UWorld;
class UMaterial;

// ---------------------------------------------------------------------------
// Renderer-side smoke test for BEAM particles.
//
// Parallel to ParticleSpriteSmokeTest / ParticleMeshSmokeTest. Validates
// FParticleSystemSceneProxy's beam-path plumbing: UpdateMaterial -> per-emitter
// material binding, FBeamParticlePacker::PackEmitter -> view-dependent side
// vector + segment expansion + taper / texture-tile math, PrepareDrawBuffer /
// ApplyDrawBuffer -> shared beam VB/IB upload and DrawIndexed submission.
//
// Independent of the teammate's UParticleSystem / FParticleEmitterInstance:
// the smoke test hand-builds one FDynamicBeamEmitterData snapshot containing
// N FBeamInstanceData entries, then pushes it directly into the proxy via
// UpdateDynamicData.
//
// Transient. Delete this file (and the toolbar button + .mat asset) once
// the teammate's CPU simulation can feed the proxy on its own.
// ---------------------------------------------------------------------------
namespace ParticleBeamSmokeTest
{
	// Walk the world, find every UParticleSystemComponent's proxy, and inject
	// a fresh starburst of N beams into each. Each beam runs from the
	// component's world origin outward to a point on a circle of given radius.
	// Repeated calls replace previous test data (proxy frees the prior emitters
	// inside UpdateDynamicData). Returns the number of proxies that received
	// data. Safe to call when World or Material is null (returns 0).
	int32 InjectIntoWorld(UWorld* World, UMaterial* Material, int32 N = 12, float Radius = 100.0f);

	// Resolves Asset/Materials/Editor/DefaultParticleBeam.mat via FMaterialManager.
	// The material binds the ParticleBeam shader at AlphaBlend / Additive with a
	// streak atlas at DiffuseTexture (t0). Returns nullptr if the .mat is missing
	// or fails to parse; the inject step logs a clear failure in that case.
	UMaterial* GetDefaultMaterial();
}
