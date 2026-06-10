#include "ParticleModuleTypeDataMesh.h"

#include "Mesh/MeshManager.h"
#include "Mesh/Static/StaticMesh.h"
#include "Object/Object.h"
#include "Particle/ParticleEmitterInstance.h"
#include "Render/Device/D3DDevice.h"
#include "Render/Pipeline/Renderer.h"
#include "Runtime/Engine.h"

FParticleEmitterInstance* UParticleModuleTypeDataMesh::CreateInstance(UParticleSystemComponent* /*InComponent*/)
{
	// The normal emitter/PSC path performs Init(); TypeData only selects the runtime instance type.
	return new FParticleMeshEmitterInstance();
}

UStaticMesh* UParticleModuleTypeDataMesh::ResolveMesh()
{
	if (CachedMesh)
	{
		return CachedMesh;
	}

	if (UStaticMesh* CachedObject = Cast<UStaticMesh>(MeshSlot.Get()))
	{
		CachedMesh = CachedObject;
		return CachedMesh;
	}

	const FString& Path = MeshSlot.ToString();
	if (Path.empty() || Path == "None")
	{
		return nullptr;
	}

	ID3D11Device* Device = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDevice() : nullptr;
	if (!Device)
	{
		return nullptr;
	}

	// Resolve the soft path once, cache it on the type-data object, and hand the same mesh to replay.
	CachedMesh = FMeshManager::LoadStaticMesh(Path, Device);
	if (CachedMesh)
	{
		MeshSlot.SetCachedObject(CachedMesh);
	}

	return CachedMesh;
}
