# GTL Week 12 - Cascade particles

## UParticleSystem

```jsx
UParticleSystem (asset, shared)
        │
        │ Template ptr
        ▼
UParticleSystemComponent (instance, per-actor)
        │
        │ owned by
        ▼
AEmitter (or any Actor with a particle component)
```

[UParticleModule](UParticleModule%20367046c6fe6f802893abd18fe04f698c.md)

[**UParticleEmitter**](UParticleEmitter%20367046c6fe6f804d99d9cd13f6287c5e.md)

[**CacheEmitterModuleInfo**](CacheEmitterModuleInfo%20367046c6fe6f80d583c2f23999a47da1.md)

[UParticleLODLevel](UParticleLODLevel%20367046c6fe6f80c7b514d013504d8e9d.md)

[UParticleSystem](UParticleSystem%20367046c6fe6f80628aa6dcf7f844063c.md)

## Particle Component

```cpp
struct FBaseParticle
{
    FVector    Location;
    FVector    Velocity;
    float      RelativeTime;
    float      Lifetime;
    FVector    BaseVelocity;
    float      Rotation;
    float      RotationRate;
    FVector    Size;
    FColor     Color;
    ...
};
```

[UParticleSystemComponent](UParticleSystemComponent%20367046c6fe6f80398b39c3274b8ffe0c.md)

[**FParticleEmitterInstance**](FParticleEmitterInstance%20367046c6fe6f801aad98ce77cdc27f81.md)

[`FParticleDataContainer` ](FParticleDataContainer%20367046c6fe6f808db12eed54a440fab5.md)

[FDynamicEmitterReplayDataBase ](FDynamicEmitterReplayDataBase%20367046c6fe6f80fab417c51a22e09288.md)

[**FDynamicEmitterDataBase**](FDynamicEmitterDataBase%20367046c6fe6f80f0a5cef420a013b9d2.md)

[ParticleHelper Macros](ParticleHelper%20Macros%20367046c6fe6f80ff9840e25500c427df.md)

## Runtime

[Runtime Instance](Runtime%20Instance%20368046c6fe6f80358417df5d2cb867a6.md)

## Renderer Side

[Geometry Shader?](Geometry%20Shader%20367046c6fe6f807f8432e97bfd5adaa4.md)

[Optimization Techniques](Optimization%20Techniques%20367046c6fe6f8048bfc2d1f266f1d289.md)