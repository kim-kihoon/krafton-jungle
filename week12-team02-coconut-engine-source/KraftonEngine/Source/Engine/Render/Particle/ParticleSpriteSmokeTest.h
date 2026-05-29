#pragma once

#include "Core/CoreTypes.h"

class UWorld;
class UMaterial;

// ---------------------------------------------------------------------------
// Renderer-side smoke test for sprite particles.
//
// Purpose: validate FParticleSystemSceneProxy's vertex packing, sort,
// material binding, and AlphaBlendPass integration WITHOUT depending on the
// teammate's UParticleSystem / FParticleEmitterInstance simulation work.
//
// Usage:
//   1. Add a UParticleSystemComponent to any actor via the editor's
//      Add component popup (Particles group).
//   2. Trigger InjectIntoWorld(GetWorld(), GetDefaultMaterial()) once.
//      A ring of N camera-facing alpha-blended sprites should appear at the
//      component's origin and stay there until the proxy is destroyed.
//
// Transient. Delete this file (and the toolbar button + .mat asset) once
// the teammate's CPU simulation can feed the proxy on its own.
// ---------------------------------------------------------------------------
namespace ParticleSpriteSmokeTest
{
	// Walk the world, find every UParticleSystemComponent's proxy, and inject
	// a fresh ring of N sprite particles into each. Marks the proxy's Mesh
	// dirty flag so UpdateMaterial/UpdateMesh re-run before the next frame.
	// Repeated calls replace previous test data (proxy frees the prior emitter
	// inside UpdateDynamicData). Returns the number of proxies that received
	// data. Safe to call when World or Material is null (returns 0).
	int32 InjectIntoWorld(UWorld* World, UMaterial* Material, int32 N = 32, float Radius = 30.0f);

	// Resolves Asset/Materials/Editor/DefaultParticleSprite.mat via
	// FMaterialManager. The material binds the ParticleSprite shader at
	// AlphaBlend / Additive with a soft circular atlas at DiffuseTexture (t0).
	// Returns nullptr if the .mat file is missing or fails to parse.
	UMaterial* GetDefaultMaterial();
}
