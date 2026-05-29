# UParticleLODLevel

## About

In Cascade, **`UParticleLODLevel`** is the C++ class responsible for encapsulating the entire structural configuration of a particle emitter for a specific **Level of Detail (LOD)**.

An `UParticleEmitter` does not actually hold a single, flat list of modules at its root. Instead, it stores an array of `UParticleLODLevel` objects (typically mapping to LOD 0 for close-up, LOD 1 for mid-range, LOD 2 for long-distance, and so on).

```cpp
// Within ParticleEmitter.h
class UParticleEmitter : public UObject
{
    // The emitter holds the list of LODs
    UPROPERTY()
    TArray<class UParticleLODLevel*> LODLevels;
};
 
// Within ParticleLODLevel.h
class UParticleLODLevel : public UObject
{
		int32 Level;
    bool bEnabled;

    // The LOD level holds the specific Required Module for this distance
    UPROPERTY()
    class UParticleModuleRequired* RequiredModule; // Material, duration
 
    UPROPERTY()
    class UParticleModuleSpawn* SpawnModule;
 
    UPROPERTY()
    TArray<class UParticleModule*> Modules; // Lifetime, Location, Velocity, etc.
};
```

---

## Core Responsibilities

Each LOD level stores its own distinct array of `UParticleModule*` pointers. This architectural decoupling allows you to completely alter the complexity of the processing pipeline based on distance. For example:

- **LOD 0 (Close-up):** Contains CPU-heavy modules like `UParticleModuleCollision` or dynamic light injectors.
- **LOD 1 (Mid-range):** Strips out the collision and light modules entirely to conserve processing power since the player won't notice the missing detail.

### 2. Managing Discrete Spawn Profiles (`SpawnModule`)

It isolates the generation logic for that specific distance layer. It holds a pointer to a dedicated `UParticleModuleSpawn` instance, enabling you to aggressively lower performance costs as the camera pulls away—such as bursting 150 particles up close (LOD 0) but dropping down to 15 particles from afar (LOD 2).

### 3. Toggling Emitter Lifecycle States (`bEnabled`)

It tracks structural switches like whether a specific layer should render at all. If you have a campfire effect with a dedicated emitter for tiny floating ash embers, you can toggle `bEnabled = false` inside the long-distance `UParticleLODLevel` objects. The engine completely stops processing that layer when the camera moves beyond the specified distance threshold.

---

## How It Works at Runtime

When a particle system is active in your game world via a `UParticleSystemComponent`, the engine manages the execution flow via these phases:

1. **LOD Determination:** The engine calculates the distance between the rendering camera and the effect to select the correct active LOD index.
2. **Context Retrieval:** The runtime simulator (`FParticleEmitterInstance`) fetches the corresponding `UParticleLODLevel` reference from the emitter asset data.
3. **Execution Loop:** The simulation loop steps through the specific `Modules` array and spawn instructions stored inside *that specific* LOD object.

This design gives technical artists granular control over performance budgeting, ensuring that Cascade doesn't waste critical CPU cycles running heavy calculations on elements that only span a few pixels on screen.