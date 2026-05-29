#pragma once

#include "Core/CoreTypes.h"

struct ID3D11Device;
class UWorld;
class UMaterial;
class UStaticMesh;

// ---------------------------------------------------------------------------
// Renderer-side smoke test for MESH particles.
//
// Parallel to ParticleSpriteSmokeTest. Validates FParticleSystemSceneProxy's
// mesh-path plumbing: UpdateMaterial → MeshGeom resolution, PackMeshEmitter
// → per-instance transform composition, PrepareDrawCommandBindings →
// per-emitter InstanceVB upload + DrawIndexedInstanced submission.
//
// Independent of the teammate's UParticleSystem / FParticleEmitterInstance —
// the smoke test hand-builds an FDynamicMeshEmitterData and pushes it
// directly into the proxy via UpdateDynamicData.
//
// Transient. Delete this file (plus ParticleSpriteSmokeTest, the
// ParticleSmokeTestPanel, and the .mat assets) once the CPU sim drives the
// proxy on its own.
// ---------------------------------------------------------------------------
namespace ParticleMeshSmokeTest
{
	// Walk the world, find every UParticleSystemComponent's proxy, and inject
	// a fresh ring of N mesh particles (each an instance of `Mesh`) into each.
	// Repeated calls replace previous test data. Returns the number of proxies
	// that received data. Safe to call when any pointer is null (returns 0).
	int32 InjectIntoWorld(UWorld* World, UMaterial* Material, UStaticMesh* Mesh,
		int32 N = 32, float Radius = 100.0f);

	// Resolves Asset/Materials/Editor/DefaultParticleMesh.mat via FMaterialManager.
	UMaterial* GetDefaultMaterial();

	// Resolves Asset/Mesh/BasicShape/Sphere_Lowpoly_StaticMesh.uasset via FMeshManager.
	// Device is required by the loader for GPU buffer creation.
	UStaticMesh* GetDefaultMesh(ID3D11Device* Device);
}
