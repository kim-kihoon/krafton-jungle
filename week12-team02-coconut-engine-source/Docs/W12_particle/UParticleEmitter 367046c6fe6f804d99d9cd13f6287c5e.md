# UParticleEmitter

## About

If UParticleModule represents the individual blocks of behavior (the "how"), **UParticleEmitter** is the container that organizes them into a single, cohesive stream of particles (the "what").

In Unreal Engine's Cascade architecture, a complete visual effect (like a campfire) is a UParticleSystem. That system is made up of multiple individual **Emitters** (UParticleEmitter), where one emitter handles the fire sprites, another handles the rising smoke, and a third handles the flying sparks.

---

## Core Responsibilities

UParticleEmitter acts as the master template and configuration manager for a single layer of a particle effect. Its primary responsibilities include:

### 1. Managing the Module Stack & Execution Order

The emitter holds the actual array of UParticleModule* pointers. It defines the compile-time and edit-time structure of the particle pipeline, ensuring that modules are evaluated in the correct sequence (e.g., ensuring Spawn modules run before Update modules).

### 2. Defining the Emitter Type (TypeData)

Not all particles are simple 2D billboard sprites. An emitter can have a specific UParticleModuleTypeData assigned to it, which fundamentally changes how those particles are rendered and simulated. UParticleEmitter manages this classification, dictating whether the stream behaves as:

- **Sprites:** Standard camera-facing billboards.
- **Mesh Particles:** Instanced 3D static meshes.
- **Ribbons / Beams:** Continuous connected geometry tracking moving points.

### 3. Spawn Rates and Burst Logic

It controls the scheduling of particle creation. It stores data regarding how many particles should be generated per second, or whether specific "bursts" of particles should occur at explicit keyframes along the emitter's timeline.

### 4. Level of Detail (LOD) and Performance Baking

UParticleEmitter handles performance scaling. It stores multiple UParticleLODLevel structures. If a particle effect is far away from the player camera, the emitter switches to a lower LOD level, which reduces the max particle count, disables complex modules (like collision or noise), and lowers spawn rates to save CPU/GPU cycles.

---

## Architectural Hierarchy

UParticleSystem (The complete asset, e.g., "FX_Explosion")
│
├── UParticleEmitter (Layer 1: "Shockwave_Mesh")
│      └── Array of UParticleModules (Spawn, Lifetime, Size, VectorField)
│
├── UParticleEmitter (Layer 2: "Fire_Sprites")
│      └── Array of UParticleModules (Spawn, ColorOverLife, Velocity)
│
└── UParticleEmitter (Layer 3: "Debris_Simulation")
└── Array of UParticleModules (Spawn, MeshTypeData, Collision)

```cpp
class UParticleEmitter : public UObject
{
    TArray<UParticleLODLevel*> LODLevels;

    void CacheEmitterModuleInfo()
    {
        ParticleSize = sizeof(FBaseParticle);
        ...
    }   
    
    void UParticleEmitter::UpdateModuleLists()
    {
	    for (int32 LODIdx = 0; LODIdx < LODLevels.Num(); LODIdx++)
	    {
	        UParticleLODLevel* LODLevel = LODLevels[LODIdx];
	        if (LODLevel)
	        {
	            // This is the call that effectively "caches the info" 
	            // for the single layer.
	            LODLevel->UpdateModuleLists();
	        }
	    }
	}
};
```

---

## Asset vs. Instance

A common point of confusion in Unreal Engine's C++ source code is where the actual *simulation* happens. UParticleEmitter does **not** simulate particles or hold their runtime positions in memory.

Like many systems in Unreal, Cascade separates static configuration data from dynamic runtime instances:

- **UParticleEmitter (The Blueprint/Template):** This is a UObject baked into your project's .uasset files. It is read-only during gameplay. If you have 50 exploding barrels in a level, they all share the exact same UParticleEmitter instances to save memory.
- **FParticleEmitterInstance (The Runtime Simulator):** When a particle system is spawned in the world via a UParticleSystemComponent, the engine instantiates a clean C++ struct/class called FParticleEmitterInstance for *each* active emitter.

| **Responsibility** | **UParticleEmitter (UObject Data)** | **FParticleEmitterInstance (Raw C++ Runtime)** |
| --- | --- | --- |
| **Memory Allocation** | Stores configuration parameters and UI settings. | Allocates the actual raw byte arrays for particle arrays. |
| **Tick / Simulation** | Does not tick. | Executes the Tick() loop, updating velocity, positions, and lifetimes. |
| **Render Tracking** | Stores the UMaterialInterface to use. | Packs dynamic vertex/index buffers or handles instanced draw calls for the GPU. |