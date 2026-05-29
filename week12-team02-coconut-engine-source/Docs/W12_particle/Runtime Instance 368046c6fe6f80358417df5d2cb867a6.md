# Runtime Instance

The class (specifically a C++ struct) that defines a single runtime particle instance in Cascade is **`FBaseParticle`**.

In the architectural context of Cascade, `FBaseParticle` represents the raw data for one individual particle in the world. Unlike `UObject` based classes, it is a lightweight struct designed to be managed in large arrays for performance.

### **1. The Structure of `FBaseParticle`**

Inside the engine code (`ParticleDefinitions.h`), the struct is defined with a few fixed “Core” properties, followed by a hidden “Payload” area:

- **Fixed Header**: Every `FBaseParticle` contains a small set of hard-coded variables that almost every particle needs:
    - `FVector Location`: Current world or local position.
    - `FVector RelativeTime`: The current age of the particle (0.0 to 1.0).
    - `float InitialTime`: The timestamp of when it was born.
- **The Payload**: This is the most unique part of the Cascade architecture. Directly after the fixed header in memory, the engine allocates extra bytes. This extra space is where modules like `UParticleModuleLifetime` or `UParticleModuleColor` store their data.

### **2. How it Works at Runtime**

When you spawn a `UParticleSystem`, the **`FParticleEmitterInstance`** (the runtime “brain” of an emitter) calculates the total size needed for a single particle:

1. It starts with the size of `FBaseParticle` (the header).
2. It loops through every module in the current **`UParticleLODLevel`**.
3. Each module’s `RequiredBytes()` function returns how much memory it needs (e.g., 4 bytes for a float, 12 for a vector).
4. The emitter allocates one large contiguous block of memory (a “Particle Pool”) where each slot is `sizeof(FBaseParticle) + TotalPayloadBytes`.

### **3. Memory Layout Example**

If an emitter has a **Lifetime** module and a **Velocity** module, the memory for one particle instance looks like this:

| Memory Section | Data Stored | Size |
| --- | --- | --- |
| **FBaseParticle Header** | Location, RelativeTime, etc. | Fixed |
| **Payload Offset A** | Lifetime value (from Lifetime Module) | 4 Bytes |
| **Payload Offset B** | Velocity vector (from Velocity Module) | 12 Bytes |

### **4. Why This Class Name Matters**

When programmers had to write custom Cascade modules in C++, they would receive a pointer to an `FBaseParticle`. They would then use a macro or an offset calculation to find their specific data inside that particle to avoid the **elimination** of other modules’ data.

**Example of legacy C++ access:**

```cpp
void UParticleModuleMyCustom::Update(FParticleEmitterInstance* Owner, int32 Offset, float DeltaTime, FBaseParticle* Particle)
{
    // Offset is provided by the Emitter so we know where our data lives in this specific instance
    FMyCustomData* MyData = (FMyCustomData*)((uint8*)Particle + Offset);
    
    // Logic to update the particle instance
    MyData->SomeValue += DeltaTime;
}
```