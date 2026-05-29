# UParticleSystemComponent

## About

If FParticleEmitterInstance was the engine under the hood doing the actual simulation work, **UParticleSystemComponent** was the vehicle chassis that hooked that engine up to the rest of Unreal Engine's world.

As a USceneComponent (a UActorComponent with a 3D transform), its primary job was to act as the gameplay-facing interface and coordinator for a particle effect. When you placed an ambient fire effect in a map or spawned a blood splatter via blueprint, you were interacting directly with a UParticleSystemComponent.

```cpp
class UParticleSystemComponent : public UFXSystemComponent
{
    // ...
public:
    /** The static UObject template (The Asset) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Particles")
    class UParticleSystem* Template;
 
    /** 
     * The dynamic array of RUNTIME INSTANCES (The Simulation).
     * These are raw C++ objects (not UObjects) for high-performance ticking.
     */
    TArray<struct FParticleEmitterInstance*> EmitterInstances;
 
    /**
     * SIMPLIFIED INITIALIZATION LOGIC (Inside the Engine)
     * This demonstrates how the "Layers" (Emitters) become "Instances".
     */
    void InitParticles()
    {
        if (Template)
        {
            // Clear old instances
            DestroyEmitterInstances();
 
            // Loop through the "Layers" in the static template
            for (int32 i = 0; i < Template->Emitters.Num(); i++)
            {
                UParticleEmitter* Emitter = Template->Emitters[i];
                
                // Create a runtime "Worker" instance for this specific layer
                // This call uses the Emitter's TypeData to spawn the correct instance type
                // (e.g., FParticleSpriteEmitterInstance or FParticleMeshEmitterInstance)
                FParticleEmitterInstance* NewInstance = Emitter->CreateInstance(this);
                
                if (NewInstance)
                {
                    EmitterInstances.Add(NewInstance);
                    NewInstance->Init();
                }
            }
        }
    }
};
```

---

## Where It Sat in the Architecture

| **Class / Struct** | **Type** | **Scope** | **Main Responsibility** |
| --- | --- | --- | --- |
| **UParticleSystem** | UObject (Asset) | Shared | The template. It holds the immutable configuration data, curves, and modules authored by an artist. |
| **UParticleSystemComponent** | USceneComponent | Per-Actor | The coordinator. It handles world transforms, gameplay parameters, LODs, and component lifetime. |
| **FParticleEmitterInstance** | Native C++ Struct | Per-Emitter | The worker. It manages the raw CPU memory, tracks individual particle lifetimes, and loops through module math. |

---

## Core Responsibilities

UParticleSystemComponent wore several hats, bridging the gap between gameplay logic, scene management, and raw rendering.

### 1. Owning and Managing Emitter Instances

A single particle asset can contain multiple distinct emitters (e.g., one for sparks, one for smoke, one for light flashes). The component held the master list of these in a native array:

C++

```
TArray<FParticleEmitterInstance*> EmitterInstances;
```

When the component was activated, it looked at the assigned UParticleSystem asset, determined how many emitters needed to be created, and allocated the corresponding FParticleEmitterInstance objects. When the component ticked, it iterated through this array and called Tick() on each instance.

### 2. Gameplay Parameter Routing (The Blueprint Bridge)

If a particle system needed to react to the game world—like changing color based on a team choice or changing speed based on a vehicle's velocity—the component handled the handoff.

- It exposed functions like SetVectorParameter(), SetFloatParameter(), and SetActorParameter().
- It maintained an InstanceParameters array. When emitters calculated their frame logic, they queried their parent component to pull these dynamic values into their modules.

### 3. Transform and Scene Attachment

Because it was a scene component, it provided spatial context. It kept track of where the effect was in the world (GetComponentLocation(), GetComponentRotation()).

- It determined whether particles should spawn in **World Space** (leaving a trail behind a moving projectile) or **Local Space** (locked tightly to the moving actor).
- It handled attachment logic, ensuring that if a character ran, the particle component attached to their skeleton socket followed perfectly.

### 4. Level of Detail (LOD) Management

To maintain frame rate when dozens of particle systems were active simultaneously, the component managed LOD switching. Every frame, it calculated its distance from the local player's camera and shifted the active LOD index. This told the underlying emitter instances to drop their spawn counts, disable expensive modules, or stop simulating entirely if they were too far away.

### 5. Interfacing with the Renderer

Like all scene components that draw geometry, the component was responsible for spawning its rendering counterpart on the render thread. It implemented CreateSceneProxy(), which generated an FParticleSystemSceneProxy. This proxy read the packed vertex/dynamic data prepared by the emitter instances and fed it directly to the GPU command buckets.