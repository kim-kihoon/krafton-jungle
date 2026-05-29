# CacheEmitterModuleInfo

## About

In Cascade, **`UParticleEmitter::CacheEmitterModuleInfo()`** is an internal preprocessing function. It acts like a localized mini-compiler or baking step.  
When a technical artist is in the editor shuffling modules, toggling checkboxes, or changing execution order, the particle system is highly abstract. But running thousands of particles at 60+ FPS requires raw, predictable structures. `CacheEmitterModuleInfo()` is the function responsible for translating that abstract, human-friendly stack of editor modules into optimized metadata that the runtime simulation (`FParticleEmitterInstance`) can read instantly.
It is executed whenever a particle asset is loaded, saved, cooked, or modified inside the Cascade editor.

```cpp
void CacheEmitterModuleInfo()
{
    ParticleSize = sizeof(FBaseParticle);
    ...
}
```

---

## Core Responsibility

`CacheEmitterModuleInfo()` performs three vital housekeeping tasks before a particle ever spawns in a level:

**1. Mapping the Raw Payload Layout (Offset Calculation)**
As established, each active `UParticleModule` can request custom bytes of data per particle (like storing a vector for an orbit module). However, a particle's memory is just a flat, raw byte array.
`CacheEmitterModuleInfo()` runs through the entire module stack, queries each module's `RequiredBytes()`, and calculates the **exact byte offset** where that module's data will live inside the particle.
• It stores these offsets within the module data itself.
• When the game is running, instead of performing expensive pointer arithmetic or lookups, a module instantly accesses its data block using:

$Particle\ Data\ Address + Cached\ Module\ Offset$

**2. Categorizing Modules for the Runtime Ticking Loop**
Not every module does everything. Some only execute when a particle is born (`Spawn`), some execute every frame (`Update`), and others perform operations at the very end of a frame (`FinalUpdate`).
Iterating through the entire module array every single frame just to check if a module *needs* to update would decimate the CPU cache. `CacheEmitterModuleInfo()` pre-sorts and categorizes the modules into separate runtime pointers or internal bitmasks:
• **Spawn Modules Array:** Only looped through when generating a new particle.
• **Update Modules Array:** Only looped through during the mid-frame simulation tick.
This guarantees that the runtime instance's ticking engine loops exclusively over modules that have actual work to do during that specific phase.

**3. Pre-calculating Sizing, Bounds, and TypeData Flags**
The function calculates and caches the absolute structural size of a final single particle (`ParticleSize`), which is the base structure size plus the sum of all module payloads.
It also evaluates specific engine flags based on the module composition. For instance, if a `UParticleModuleTypeDataMesh` or `Beam` is present in the stack, `CacheEmitterModuleInfo()` ensures the emitter caches the direct pointer to that TypeData. This tells the runtime instance exactly what kind of vertex layouts, sorting algorithms, or rendering paths it needs to initialize.

---

## The Big Picture: Why It Was Necessary

Cascade was built at a time when Object-Oriented Programming (OOP) dominated engine design. Because every module in Cascade is an isolated `UObject` pointer, traversing them haphazardly at runtime causes a massive penalty in **CPU cache misses**.

`CacheEmitterModuleInfo()` was Cascade's primary defense mechanism against its own OOP overhead. By consolidation, filtering, and caching all layout offsets and execution paths into flat arrays ahead of time, it stripped away the dynamic search penalties, allowing the legacy CPU simulation to loop through particle blocks as linearly as possible.