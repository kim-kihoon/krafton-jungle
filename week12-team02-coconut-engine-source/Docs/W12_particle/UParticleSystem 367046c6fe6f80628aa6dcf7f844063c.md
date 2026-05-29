# UParticleSystem

## About

**`UParticleSystem`** is the absolute top-level `UObject` asset container for the entire Cascade framework. It represents the actual `.uasset` file you see, move, and organize within your Content Browser (e.g., `P_Explosion` or `P_Campfire`).

If `UParticleModule` represents the specific behaviors (the ingredients) and `UParticleEmitter` represents an individual layer of effects (the individual dishes), `UParticleSystem` is the entire multi-course meal. It sits at the very top of the hierarchy, managing the macro logistics, global timings, and scene-wide optimization rules that individual components shouldn't have to worry about.

---

## Core Responsibilities

`UParticleSystem` handles four critical engine-level tasks to make sure a visual effect integrates properly into a live game scene:

### 1. Macro Lifecycle and Timing Control

While individual emitters decide how fast they spawn particles, `UParticleSystem` manages the global clock for the entire compilation of effects.

- **System Delay:** Delays the start of the entire effect by a specified duration, useful for syncing a particle sequence with gameplay cues or animations.
- **Warm-Up Time (`WarmUpTime`):** Pre-simulates a specific number of seconds of the effect before it ever renders on screen. For example, if you place a chimney in a level, you don't want the player to see smoke suddenly start sprouting from an empty pipe when the level loads; you want it already flowing. A warm-up time of 5 seconds forces Cascade to compute those initial 5 seconds instantly on initialization.

### 2. Global Level of Detail (LOD) Coordination

While `UParticleLODLevel` dictates what *one* emitter does at a certain distance, `UParticleSystem` manages the master distance thresholds for the entire asset. It evaluates the distance between the camera and the effect, then broadcasts a system-wide command telling every child emitter to switch to LOD 1, LOD 2, etc., simultaneously.

### 3. Visibility Culling via Bounding Boxes (`FBox`)

To avoid wasting CPU and GPU cycles, the engine needs to know if a particle system is actually visible on the player's screen. `UParticleSystem` is responsible for calculating and storing the asset's bounding box.

- **Dynamic Bounds:** Calculates a new bounding box every frame based on where the furthest particle has traveled. This is highly accurate but computationally expensive for the CPU.
- **Fixed Bounds:** Allows technical artists to manually define a static bounding box size. This is a massive optimization because it strips away per-frame calculation costs entirely; the engine simply checks if that fixed box is inside the camera's view frustum.

### 4. Aggregating the Emitter Array (`Emitters`)

It maintains the master `TArray<UParticleEmitter*>` that binds all individual layers together. When an artist opens the Cascade Editor, this master object acts as the root interface, orchestrating how the individual emitters layout horizontally in the UI.

---

## The Complete Cascade Memory Blueprint

```cpp
[Content Browser Asset]
UParticleSystem (.uasset Container)
  └── Global Properties (WarmUpTime, Fixed Bounds, Looping rules)
  └── TArray<UParticleEmitter*> 
        │
        ├── UParticleEmitter (e.g., Smoke Layer)
        │     └── TArray<UParticleLODLevel*>
        │           ├── LOD 0 (Close-up) -> Array of UParticleModules (Heavy Collision, high spawn rate)
        │           └── LOD 1 (Far away) -> Array of UParticleModules (No collision, low spawn rate)
        │
        └── UParticleEmitter (e.g., Sparks Layer)
              └── TArray<UParticleLODLevel*>
                    ├── LOD 0 -> Array of UParticleModules
                    └── LOD 1 -> Array of UParticleModules

====================================================================================================

[Placed In World / Runtime]
UParticleSystemComponent (The Actor Component in your scene)
  └── FParticleSystemSceneProxy (Handles sending structural data to the Renderer)
  └── TArray<FParticleEmitterInstance*> (The actual raw C++ loops executing Tick() and storing particle bytes)
```

---

## **Wrapping Up: Component vs. System**

When writing C++ gameplay code, you rarely interact with `UParticleSystem` directly other than passing its reference around. Instead, you instantiate a **`UParticleSystemComponent`**.

Think of `UParticleSystem` as the read-only blueprints for a house, while `UParticleSystemComponent` is the physical house built on a specific plot of land in your map. Multiple components can reference the exact same underlying `UParticleSystem` data asset, allowing the engine to instantiate thousands of torches, footsteps, or muzzle flashes across a map efficiently.</UParticleEmitter*>