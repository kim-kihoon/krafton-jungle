# FParticleEmitterInstance

## About

In Unreal Engine’s legacy **Cascade** particle system, `FParticleEmitterInstance` was the native C++ struct representing the active, real-time instance of a single particle emitter (`UParticleEmitter`) in the game world.

While classes like `UParticleSystem` and `UParticleEmitter` served as the data templates (the configuration assets created by technical artists in the editor), `FParticleEmitterInstance` did the actual heavy lifting of tracking particle data, managing memory, and driving the simulation on the CPU.

```cpp
struct FParticleEmitterInstance
{
    UParticleEmitter* SpriteTemplate;

    // Owner
    UParticleSystemComponent* Component;

    int32 CurrentLODLevelIndex;
    UParticleLODLevel* CurrentLODLevel;

    /** Pointer to the particle data array.                             */
    uint8* ParticleData;
    /** Pointer to the particle index array.                            */
    uint16* ParticleIndices;
    /** Pointer to the instance data array.                             */
    uint8* InstanceData;
    /** The size of the Instance data array.                            */
    int32 InstancePayloadSize;
    /** The offset to the particle data.                                */
    int32 PayloadOffset;
    /** The total size of a particle (in bytes).                        */
    int32 ParticleSize;
    /** The stride between particles in the ParticleData array.         */
    int32 ParticleStride;
    /** The number of particles currently active in the emitter.        */
    int32 ActiveParticles;
    /** Monotonically increasing counter. */
    uint32 ParticleCounter;
    /** The maximum number of active particles that can be held in 
     *  the particle data array.
     */
    int32 MaxActiveParticles;
    /** The fraction of time left over from spawning.                   */

    void SpawnParticles( int32 Count, float StartTime, float Increment, const FVector& InitialLocation, const FVector& InitialVelocity, struct FParticleEventInstancePayload* EventPayload )
    {
        for (int32 i = 0; i < Count; i++)
        {
            DECLARE_PARTICLE_PTR
            PreSpawn(Particle, InitialLocation, InitialVelocity);

            for (int32 ModuleIndex = 0; ModuleIndex < LODLevel->SpawnModules.Num(); ModuleIndex++)
            {
                ...
            }

            PostSpawn(Particle, Interp, SpawnTime);
        }
    }

    void KillParticle(int32 Index);

    ...
};
```

---

## Memory Architecture

One of the most defining characteristics of `FParticleEmitterInstance` was how it handled raw particle memory. Unlike modern systems like Niagara that heavily lean on structured GPU buffers or separate SIMD data streams, Cascade was almost entirely CPU-driven and object-oriented.
• **Flat Array Allocation:** The instance allocated and maintained a single raw byte array pointer (`uint8* ParticleData`).
• **Dynamic Particle Stride:** The size of an individual particle (`ParticleStride`) wasn't fixed across the engine. It was calculated dynamically at initialization based on which modules were present in the emitter template. If an artist added an orbit module or a custom parameter module, the instance expanded the byte footprint of every single particle to accommodate that module's custom payload offset.  
• **Active and Dead Pools:** It maintained an array of active particle indices. When a particle died, its slot was swapped with the last active particle in the array, allowing for $O(1)$ particle creation and destruction without constantly reallocating memory.

---

## Primary Responsibilities

`FParticleEmitterInstance` acted as the manager of the emitter's life cycle. Its core duties split into four main areas:

### 1. Spawning and Lifecycle Control

It tracked execution time, looping states, and managed particle instantiation. It checked the asset's spawn rates and burst lists every frame, calculating how many particles needed to be born during the current frame time using internal state variables like `SpawnFraction`.

### 2. Driving Module Logic

Cascade was a heavily modular system. During its `Tick()` loop, the emitter instance iterated over the collection of `UParticleModule` objects defined in the asset template. It was responsible for:

- Invoking `Spawn()` modules to initialize data fields for brand-new particles (e.g., initial velocity, color, size).
- Invoking `Update()` modules every frame to manipulate active particle attributes over time.

### 3. Simulation & Housekeeping (`Tick`)

Every frame, the instance evaluated particle lifetimes. It updated fundamental physics components (if applied), integrated positions, and handled cleanup. When a particle's relative time exceeded its lifespan, the instance executed `KillParticle()` to recycle that index back into the free pool. It was also responsible for updating the emitter's bounding box (`UpdateBoundingBox`) for frustum culling.

### 4. Packing Data for the Render Thread

Before the GPU could draw anything, `FParticleEmitterInstance` had to bridge the gap to the rendering system. It packed the computed simulation states—such as transformed positions, scales, rotations, and texture sheet UV offsets—into dynamic geometry data and handed them off to the component's `FParticleSystemSceneProxy` to feed the render thread.

To see how these underlying technical concepts map to the artist-facing configuration tools in the classic editor, you can check out this [Introduction to the Cascade Emitter Interface](https://www.youtube.com/watch?v=apP3K9rpl8M). This video is useful because it visualizes the exact properties, modules, and data layouts that `FParticleEmitterInstance` handles under the hood.