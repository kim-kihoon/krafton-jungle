# ParticleHelper Macros

```cpp
// ParticleHelper.h

DECLARE_PARTICLE_PTR

BEGIN_UPDATE_LOOP

END_UPDATE_LOOP
```

In Unreal Engine’s legacy **Cascade** particle system, these macros were defined in `ParticleHelper.h` to help developers write custom C++ particle modules without having to rewrite repetitive boilerplate code.

Because Cascade managed particle data as a contiguous block of memory (using byte strides and offsets) rather than standard C++ objects, iterating through a particle emitter and extracting individual particle data was cumbersome. These macros abstracted that memory management away.

Here is what each macro did under the hood:

### 1. `DECLARE_PARTICLE_PTR`

This macro calculated the correct memory address for a specific particle and cast it into a usable C++ struct. Cascade stored custom module data via byte offsets attached to a base particle payload.
When called, this macro took the current loop index and the emitter’s memory stride, did the pointer math, and declared a local pointer/reference (usually `FBaseParticle& Particle`) so you could easily read or modify properties like `Particle.Velocity` or `Particle.Location`.

### 2. `BEGIN_UPDATE_LOOP`

This macro initiated a `for` loop that iterated backwards over all currently active particles within an `FParticleEmitterInstance`.
It handled checking the active particle count (`Owner->ActiveParticles`), scoping the loop, and typically invoked `DECLARE_PARTICLE_PTR` automatically. By just dropping this macro into your module's `Update` function, it essentially said: *"Loop through every living particle and fetch its data for me."*

### 3. `END_UPDATE_LOOP`

This simply closed the loop and the scope bracket opened by `BEGIN_UPDATE_LOOP`.

### How they looked in practice

If you were writing a custom module (e.g., to scale a particle's size based on its speed), the `Update` method would look incredibly clean thanks to these macros. It would look something like this:

C++

```cpp
void UParticleModuleCustom::Update(FParticleEmitterInstance* Owner, int32 Offset, float DeltaTime){
    // Opens the for-loop and declares 'Particle'
    BEGIN_UPDATE_LOOP;

    // Now you can directly manipulate the individual particle
    float Speed = Particle.Velocity.Size();
    Particle.Size = BaseSize * Speed;

    // Closes the for-loop
    END_UPDATE_LOOP;
}
```

### Modern Context

Cascade has been fully deprecated and replaced by **Niagara** in modern Unreal Engine (UE5). Niagara uses a completely different, highly optimized data paradigm (HLSL-based compute shaders and visual node graphs), so these macros are now a relic of UE4 C++ development.