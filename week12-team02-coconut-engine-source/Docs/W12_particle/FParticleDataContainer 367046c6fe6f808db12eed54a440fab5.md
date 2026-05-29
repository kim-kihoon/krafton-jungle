# FParticleDataContainer

In the legacy **Cascade** particle system of Unreal Engine (UE3 and UE4), `FParticleDataContainer` was a low-level utility struct responsible for managing the raw CPU-side memory block allocated for a single particle emitter instance (`FParticleEmitterInstance`).

Because Cascade operated entirely on the CPU (unlike Niagara, which relies heavily on GPU compute and a Data Interface model), it needed a highly specialized, cache-friendly, and minimal-overhead way to store and manipulate particle properties.

Here is a breakdown of what `FParticleDataContainer` did and its specific responsibilities within the particle component system.

```cpp
struct FParticleDataContainer
{
    int32 MemBlockSize;
    int32 ParticleDataNumBytes;
    int32 ParticleIndicesNumShorts;
    uint8* ParticleData; // this is also the memory block we allocated
    uint16* ParticleIndices; // not allocated, this is at the end of the memory block
    ...
};
```

## 1. Single-Allocation Memory Consolidation

To avoid memory fragmentation and minimize heap allocation overhead, Cascade used a **single contiguous block of memory** for both the raw particle structures and their lookup indices.

`FParticleDataContainer` acted as the wrapper for this block:

- It called a single `FMemory::Malloc` for the total size (`MemBlockSize`).
- It split that block internally: the front half became the raw particle payload data (`ParticleData`), and the trailing tail of the block became the lookup index array (`ParticleIndices`).

## 2. Managing Dynamic-Stride Array of Structures (AoS)

In Cascade, a particle’s memory layout wasn’t fixed at compile time. A base particle struct (`FBaseParticle`) contained essential data (Position, Velocity, Lifetime), but every module added to the emitter (e.g., *Color Over Life*, *Size By Speed*, *SubUV Animation*) could request extra payload bytes.

At runtime, the emitter calculated the final stride:

$$
\mathrm{ParticleStride}
= \mathrm{sizeof}\!\left(FBaseParticle\right)
+ \sum_{m=1}^{N} \mathrm{PayloadSize}_m
$$

Because of this variable runtime size, Cascade couldn't use a standard standard template library vector or typed C++ array. Instead, `FParticleDataContainer::ParticleData` stored these variable-sized structs back-to-back as a raw `uint8*` byte buffer. To access a particle at slot `i`, the system used explicit pointer arithmetic:

C++

```
FBaseParticle* Particle = (FBaseParticle*)(Container.ParticleData + (i * ParticleStride));
```

## 3. Indirection and Cache-Friendly Indexing

The `ParticleIndices` pointer (`uint16*`) served as a lightweight **indirection layer**. Shuffling large, variable-sized structures in memory is incredibly expensive and ruins CPU cache lines. Instead of moving data in `ParticleData`, Cascade manipulated the 16-bit indices in `ParticleIndices`.

It was primarily responsible for two things:

- **Active/Inactive Pool Tracking (Free-Lists):** It tracked which slots in `ParticleData` held living particles and which ones were dead and available for spawning.
- **Camera Distance Sorting:** For translucent materials, CPU particles had to be sorted back-to-front relative to the camera to render correctly. Rather than sorting the bulky raw particle structs, Cascade’s sorting routines sorted the `uint16` indices inside `ParticleIndices`. The renderer then used these indices to draw the particles in the correct order.

> **Note:** This design is why classic Cascade CPU emitters had a hard limit of **65,535 max particles** per instance—the indirection array used a `uint16` to conserve memory and keep indices compact.
> 

### Summary of Fields

| **Field** | **Purpose** |
| --- | --- |
| **`MemBlockSize`** | Total bytes allocated from the heap for the entire container (Data + Indices). |
| **`ParticleDataNumBytes`** | The subset of bytes dedicated exclusively to storing the raw `FBaseParticle` + payload structures. |
| **`ParticleIndicesNumShorts`** | The maximum number of particle slots tracked by the indirection array. |
| **`ParticleData`** | Points to the start of the memory block where active and dead particle payloads are stored sequentially. |
| **`ParticleIndices`** | Points directly to the tail section of the *same* allocation block, bypassing a secondary heap allocation call. |

### Legacy vs. Modern Context

`FParticleDataContainer` represents the peak of CPU-bound **Array of Structures (AoS)** design. While excellent for reducing allocation counts, it suffered from CPU cache thrashing when modules only needed to update a single property (like color) across all particles.

This architectural limitation is the main reason Epic Games built **Niagara**, which completely replaced this model with a **Structure of Arrays (SoA)** format, decoupling payloads into individual property arrays optimized for SIMD execution and GPU streaming.