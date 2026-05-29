# FDynamicEmitterDataBase

### Role

This is the abstract base interface that represents an emitter's dynamic data **from the perspective of the Render Thread / Scene Proxy**.

### Functional Responsibilities & Features

- **Pipeline Identification (`EmitterIndex`):** Tracks the specific index of this emitter within the parent `UParticleSystemComponent`. This allows the `FParticleSystemSceneProxy` to map render data back to the correct emitter layer.
- **The Bridge Pattern (`GetSource()`):** The pure virtual function `GetSource()` forces all derived rendering structures to provide access to their underlying `FDynamicEmitterReplayDataBase`. This allows the renderer to generically query the snapshotted particle counts and raw byte blocks regardless of whether the emitter is a sprite, mesh, or beam.

```cpp
struct FDynamicEmitterDataBase
{
    int32 EmitterIndex;
    ...

    virtual const FDynamicEmitterReplayDataBase& GetSource() const = 0;
    ...
};
```

---

## FDynamicSpriteEmitterReplayDataBase

### Role

This is the **data container snapshot** specialized for standard 2D sprite/quad emitters. It extends the base replay class by adding the specific rendering instructions and material dependencies required to draw a flat particle in a 3D world.

### Functional Responsibilities & Features

- **Material Tracking (`MaterialInterface`):** Holds a pointer to the specific `UMaterialInterface` used by this emitter for the current frame. This ensures that even if a blueprint swaps a particle's material mid-game, the render thread uses the correct material state assigned to that specific frame batch.
- **Module Layout Definition (`RequiredModule`):** Points to the `FParticleRequiredModule` data, which dictates foundational rendering rules like screen alignment (PSA_Square, PSA_Velocity, etc.), particle blending modes, and texture UV scaling.
- **Thread-Safe Payload:** It carries the raw `FParticleDataContainer` payload from the base class, freezing the particle transforms and lifetimes so the render thread can safely read them without data races.

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

```cpp
struct FDynamicSpriteEmitterReplayDataBase : public FDynamicEmitterReplayDataBase
{
    UMaterialInterface*             MaterialInterface;
    struct FParticleRequiredModule  *RequiredModule;
    ...
};
```

---

## FDynamicSpriteEmitterDataBase

### Role

An intermediate, abstract base class on the rendering side that establishes operations common to **element-based geometry generation** (emitters that treat particles as discrete visual elements requiring sorting or explicit vertex layouts).

### Functional Responsibilities & Features

- **Render-Side Back-to-Front Sorting (`SortSpriteParticles`):** Implements the actual sorting algorithms executed immediately before vertex buffer allocation. For translucent particles, it reads the camera position, computes the distance for each active particle index, and sorts the `ParticleIndices` array to prevent alpha-blending artifacts.
- **Vertex Stride Interface (`GetDynamicVertexStride`):** Establishes the interface for determining how many bytes a single instance or vertex occupies in GPU memory.

```cpp
struct FDynamicSpriteEmitterDataBase : public FDynamicEmitterDataBase
{
    void SortSpriteParticles(...);
    virtual int32 GetDynamicVertexStride(ERHIFeatureLevel::Type /*InFeatureLevel*/) const = 0;
    ...
};
```

---

## FDynamicSpriteEmitterData

**Role**
The concrete implementation of the render-proxy data for a standard **2D Screen-Aligned Sprite Emitter**.
**Functional Responsibilities & Features**
• **Defining Sprite Geometry Stride:** Overrides `GetDynamicVertexStride` to return `sizeof(FParticleSpriteVertex)`.
• **Vertex Allocation Scaling:** A standard sprite quad consists of 4 vertices. The rendering pipeline uses the stride returned here to calculate the exact size of the dynamic vertex buffer to allocate on the GPU:

$$
\mathrm{AllocationSize}
=
\mathrm{ActiveParticleCount}\times 4 \times \mathrm{sizeof}\!\left(FParticleSpriteVertex\right)
$$

- **Data Packing:** Responsible for looping through the raw `ParticleData` bytes, evaluating the size/rotation/color of each particle, and translating them into individual `FParticleSpriteVertex` structures that the Vertex Factory can decode.

```cpp
struct FDynamicSpriteEmitterData : public FDynamicSpriteEmitterDataBase
{
    virtual int32 GetDynamicVertexStride(ERHIFeatureLevel::Type InFeatureLevel) const override
    {
        return sizeof(FParticleSpriteVertex);
    }

    ...
};
```

---

## FDynamicMeshEmitterData

### Role

The concrete implementation of the render-proxy data for a **3D Mesh Particle Emitter**.

### Functional Responsibilities & Features

- **The "Instance" Layout Paradigm:** Overrides `GetDynamicVertexStride` to return `sizeof(FMeshParticleInstanceVertex)`. Unlike sprites—where the CPU generates 4 quad corners per particle—mesh particles utilize **Hardware Instancing**.
- **Instance Data Packing:** Instead of storing raw corner vertices, `FMeshParticleInstanceVertex` stores a full transform matrix, color multiplier, and dynamic parameters *per particle instance*. The GPU uses a single static mesh asset geometry buffer and duplicates it across all instances using this data block.

```cpp
struct FDynamicMeshEmitterData : public FDynamicSpriteEmitterData
{
    virtual int32 GetDynamicVertexStride(ERHIFeatureLevel::Type /*InFeatureLevel*/) const override
    {
        return sizeof(FMeshParticleInstanceVertex);
    }
    ...
};
```

---

## Architectural Summary: Why Mesh Inherits From Sprite

A notable quirk of Cascade's legacy architecture is that `FDynamicMeshEmitterData` inherits directly from `FDynamicSpriteEmitterData`.

`FDynamicEmitterDataBase (Base Interface)
   └── FDynamicSpriteEmitterDataBase (Sorting/Stride Interface)
          └── FDynamicSpriteEmitterData (Standard 2D Quad)
                 └── FDynamicMeshEmitterData (3D Hardware Instancing)`

While semantically a 3D Mesh is not a 2D Sprite, they share almost identical logistical pipelines in Cascade:

1. Both require tracking an active count of independent elements.
2. Both require distance-based sorting for translucent materials.
3. Both require mapping custom module payloads (like SubUVs or Dynamic Parameters) into a vertex pipeline.

To avoid massive code duplication, Epic's engineers used inheritance pragmatically. `FDynamicMeshEmitterData` inherits the sorting logic, index manipulation, and base lifecycle of the sprite pipeline, but completely overrides the **Vertex Stride** and the **Vertex Factory binding** to feed 3D transforms to the GPU instancing system rather than building 2D quads.