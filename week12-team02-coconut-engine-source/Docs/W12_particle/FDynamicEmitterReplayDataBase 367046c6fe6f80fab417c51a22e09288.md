# FDynamicEmitterReplayDataBase

## About

In Cascade, **FDynamicEmitterReplayDataBase** was the base structure used to store a serialized, pre-recorded snapshot of a single emitter instance's state for a given frame.

It worked hand-in-hand with the **UParticleSystemReplay** asset class, which allowed developers to capture a live CPU particle simulation inside the editor and "bake" it into an asset file.

```cpp
struct FDynamicEmitterReplayDataBase
{
    /** The type of emitter. */
    EDynamicEmitterType eEmitterType;

    /** The number of particles currently active in this emitter. */
    int32 ActiveParticleCount;

    int32 ParticleStride;
    FParticleDataContainer DataContainer;

    FVector3f Scale;

    int32 SortMode;
    ...
};
```

---

## **The Core Purpose: Determinism and Performance Baking**

Because Cascade was a CPU-bound simulation engine, it heavily relied on random number generators (seeds) for velocity, spawning, lifetime, and noise modules. This created two major engineering challenges:

1. **Lack of Determinism:** Cinematics, trailers, and cutscenes (via Matinee or Sequencer) required particles to behave *exactly* the same way on every run.
2. **High CPU Overhead:** Complex particle systems with hundreds of active particles spent significant CPU time looping through modules during their Tick() phase.

FDynamicEmitterReplayDataBase solved this by capturing the absolute state of the emitter. When a particle system was set to play a replay, the Game Thread completely bypassed the module update loops. Instead, it un-serialized the appropriate frame's FDynamicEmitterReplayDataBase and passed the baked data directly down the pipeline.

---

## **How It Handled the Particle Component Pipeline**

The structure contains everything necessary to reconstruct the visual state of an emitter without running a single line of physics simulation:

### 1. Snapshotting the Memory Layout

As seen in its fields, it contains a full copy of the **FParticleDataContainer DataContainer**. Instead of dynamically allocating and shuffling a live pool of particles, it saves the raw byte block of active particle structures (ParticleData) and their corresponding lookup offsets (ParticleIndices) exactly as they existed at the moment of recording.

Combined with ParticleStride and ActiveParticleCount, the render thread could step through the raw bytes exactly like a live simulation.

### 2. Polymorphic Emitter Types (eEmitterType)

Cascade supported several distinct underlying emitter configurations (Sprites, Meshes, Ribbons/Trails, Beams). FDynamicEmitterReplayDataBase acted as a polymorphic base class. Depending on the eEmitterType, it would cast to specialized subclasses that stored additional module-specific data needed for rendering:

- FDynamicSpriteEmitterReplayData
- FDynamicMeshEmitterReplayData
- FDynamicBeam2EmitterReplayData
- FDynamicTrailsEmitterReplayData

### 3. Preserving Render-State Context

Fields like Scale and SortMode were saved per frame because rendering behavior depends heavily on spatial transforms and camera distance. For instance, if the particle system component's actor scale shifted during the recording, saving the Scale vector ensured the bounding boxes and mesh transforms remained accurate during playback. Saving the SortMode ensured the render thread still knew exactly how to interpret the ParticleIndices array for sorting translucent sorting priorities.

---

## **Conceptual Architecture Flow**

When a particle system was running in **Replay Mode**, the lifecycle shifted dramatically:

`[Normal Live Mode]
Component Tick -> Emitter Instance -> Evaluate Modules -> Update FParticleDataContainer -> Send to Render Thread

[Replay Playback Mode]
Sequencer/Time -> Fetch UParticleSystemReplay Frame -> Extract FDynamicEmitterReplayDataBase -> Send to Render Thread`

By storing the raw particle allocations directly inside the replay data block, Cascade successfully decoupled the rendering of complex particle effects from the CPU simulation tick cost—trading memory/disk space for significant frame-time savings in cinematic-heavy sequences.