# UParticleModule

## About

`UParticleModule` is the base C++ class for all modules used within Unreal Engine’s **Cascade Particle System**.

In Unreal Engine, particle effects are built using emitters, and those emitters are driven by stackable blocks called **modules**. `UParticleModule` is the abstract foundation that defines how those modules behave, initialize, and update particles over time.

---

## Core Responsibilities

Every module derived from `UParticleModule` acts as a discrete piece of logic responsible for a specific behavior or attribute of a particle effect. It primarily interfaces with the particle simulation using two critical lifecycle phases:

- **Spawn (`Spawn()`):** Controls the initial properties of a particle at the exact frame it is born. This includes setting things like its initial velocity, starting location, or baseline size.
- **Update (`Update()`):** Executes every frame to change a particle's behavior over its lifespan. This is used for behaviors like accelerating due to gravity, changing color over time, or fading out.

---

## Subclasses

If you have ever used Unreal Engine's Cascade Editor, you have interacted with derivatives of this class directly in the UI. Common examples include:
• **`UParticleModuleLifetime`:** Dictates how many seconds a particle will exist before disappearing.
• **`UParticleModuleSize` / `UParticleModuleSizeMultiplyLife`:** Handles how large a particle is at birth and how it scales up or down as it ages.
• **`UParticleModuleVelocity`:** Provides the initial speed and direction vectors for particles.
• **`UParticleModuleColorOverLife`:** Gradually shifts the RGB values or alpha transparency of a particle across its timeline.  
• **`UParticleModuleLocation`:** Defines the shape or area from which particles sprout (e.g., a box, sphere, or skeletal mesh surface).

```cpp
class UParticleModule : public UObject
{
};

/*
UParticleModuleRequired: Emitter에 필수적인 설정을 포함하며, 파티클의 기본 속성들을 정의합니다.
UParticleModuleSpawn: 파티클의 생성 빈도와 수량을 제어합니다.
UParticleModuleLifetime: 파티클의 수명을 설정합니다.
UParticleModuleLocation: 파티클의 초기 위치를 결정합니다.
UParticleModuleVelocity: 파티클의 초기 속도와 방향을 설정합니다.
UParticleModuleColor: 파티클의 색상을 정의하며, 시간에 따른 색상 변화를 설정할 수 있습니다.
UParticleModuleSize: 파티클의 크기를 설정합니다.
*/
```

---

## Why Subclasses? (1. Bandwidth Optimization)

Packing every possible particle property into a single class or struct runs into a classic game engine design trap: a massive **"God Object"** that is highly wasteful, inflexible, and difficult to maintain.

In a heavy visual effect, you might have tens of thousands of particles active simultaneously. If Unreal Engine used a single, monolithic particle struct containing field members for every single feature (Velocity, Color, SubUV texture coordinates, Skeletal Mesh attachment, Vector Field influences, Collision data, Orbit properties), every individual particle would occupy that memory.

```cpp
// Hypotolithic Wasteful Approach (Monolithic Struct)
struct FMonolithicParticle {
	FVector Position;
	FVector Velocity;
	FLinearColor Color;
	FVector OrbitOffset;       // Wasted memory if the particle doesn't orbit!
	FVector CollisionNormal;   // Wasted memory if the particle has no collision!
	int32 SubUVIndex;          // Wasted memory if it's not an animated texture!
};
```

Instead, Cascade uses a dynamic memory payload model:

- The core particle structure (`FBaseParticle`) is kept incredibly lean, holding only absolute essentials.
- When an emitter loads, it queries each active `UParticleModule` subclass: *"How many bytes of data do you need to store per particle?"* (via the `RequiredBytes()` function).
- The engine then accumulates these requests and allocates a single raw byte array per particle.

If a particle system only handles simple sparks requiring just location and velocity, it allocates a tiny chunk of memory. The payload layout only expands to accommodate data fields like orbit offsets or collision normals if those specific module subclasses are actively added to the emitter stack.

---

## Why Subclasses? (2. Logic and Evaluation (Data vs. Behavior))

A field member is just passive data (like a float or a vector). However, particle properties in a production engine are rarely static values; they change over time, randomize within ranges, or pull dynamically from external game state.

Subclasses wrap both the **data storage** and the **execution logic** together:

- **Mathematical Distributions:** Instead of a simple float, modules hold `UDistributionFloat` or `UDistributionVector` objects. These allow a single module to switch seamlessly between a constant value, a random range, or a complex curve driven by the particle's relative lifetime.
- **Complex Processing:** Some modules perform heavy lifting. For example, a location module might need to sample a random triangle on a Skeletal Mesh surface. Packing that specific logic into a giant conditional switch-case block inside a single particle manager would quickly make the engine code unmaintainable.

---

## Why Subclasses? (3. The Open-Closed Principle (Extensibility))

By making modules independent subclasses, Unreal's architecture obeys a core software engineering design principle: systems should be open for extension but closed for modification.

If a project requires a highly specialized behavior—such as a custom module that forces particles to swirl around a player's exact bounding volume—a programmer does not need to modify Unreal’s core emitter source code. They can simply subclass `UParticleModule`, override `Spawn()` and `Update()`, and the engine automatically integrates it into the editor UI and simulation loop.