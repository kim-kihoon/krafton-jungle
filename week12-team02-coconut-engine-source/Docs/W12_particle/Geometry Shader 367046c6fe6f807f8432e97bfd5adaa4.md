# Geometry Shader?

Unreal Engine did not use Geometry Shaders (GS) for the standard rendering of Cascade particles.

While it is a common technical assumption that particle quads are generated via a Geometry Shader, Epic Games opted for a different architectural approach for both Cascade and the modern Niagara system to ensure better cross-platform performance and to avoid known hardware bottlenecks.

### **How Cascade Generates Particles**

Instead of using a Geometry Shader to expand a point into a quad, Cascade (and Niagara) primarily uses **Vertex Shader Expansion** or **Hardware Instancing**.

- **Vertex Shader Expansion:** The engine sends a vertex buffer containing four vertices per particle (a quad) to the GPU. The Vertex Shader then calculates the specific corner offset for each vertex based on the particle’s center position, size, and rotation.
- **Dynamic Parameter Packing:** For GPU sprites, particle data (position, velocity, life) is stored in textures or buffers. The Vertex Shader reads this data using a `VertexID` to determine which particle it belongs to and positions the quad accordingly.

### **Why Geometry Shaders Were Avoided**

Epic Games has historically avoided Geometry Shaders in the main rendering pipeline for several reasons:

- **Performance Inconsistency:** On many GPU architectures (especially older ones or mobile tiles), Geometry Shaders can be significantly slower than Vertex Shaders. They often require the GPU to break its parallel processing flow to manage the creation of new primitives.
- **Vertex Reuse:** Geometry Shaders often result in the GPU being unable to reuse post-transform vertex cache data efficiently.
- **Cross-Platform Parity:** To ensure that a particle system looks and performs similarly on PC, consoles, and mobile, using a standard Vertex/Pixel shader path is more reliable than relying on varying GS implementations.

---

## Not even for LOD?

Unreal Engine also does not use Geometry Shaders for **Particle Level of Detail (LOD)** in Cascade. Particle LOD transitions are handled primarily on the **CPU** or through **Vertex Shader** logic, rather than using a Geometry Shader to procedurally reduce or alter geometry density.

The implementation of LODs in Cascade focuses on reducing the simulation and rendering load through the following mechanisms:

### **1. CPU-Driven LOD Switching**

Cascade utilizes a distance-based LOD system managed by the CPU. The engine calculates the distance between the camera and the particle emitter to determine which LOD index to use.

- **Module Overrides:** You can define different values for modules (like Spawn Rate) at different LOD levels. For example, LOD 0 might spawn 100 particles, while LOD 1 (further away) spawns only 20.
- **Module Elimination:** Certain high-cost modules, such as complex noise or collisions, can be entirely **eliminated** in lower LODs to save CPU cycles.
- **Emitter Disabling:** If an emitter is far enough away, the entire emitter can be disabled, effectively performing a “cull” to stop all simulation and rendering.

### **2. Vertex Shader-Based Optimization**

While the CPU handles which LOD is active, the GPU optimizes the rendering of those particles using the Vertex Shader:

- **Distance Culling:** The Vertex Shader can check the distance of a particle and collapse the quad (setting its size to zero) if it exceeds a certain threshold.
- **Static Mesh LODs:** If the emitter is using Mesh Particles (rather than sprites), the engine uses standard Static Mesh LODs. This switches the geometry to a lower-poly version as the camera moves away, a process handled by the standard mesh rendering pipeline, not a Geometry Shader.

### **3. Why Not Geometry Shaders for LOD?**

In some graphics research, Geometry Shaders are proposed for “primitive thinning” (procedurally removing every second particle) or “tessellation.” Unreal Engine avoids this for Particle LOD for specific reasons:

- **Memory vs. Computation:** It is more efficient to simply tell the CPU to spawn fewer particles (reducing memory and simulation time) than to spawn many particles and ask a Geometry Shader to “delete” them at the end of the pipe.
- **Simulation Consistency:** If a Geometry Shader were to randomly **eliminate** particles for LOD purposes, it could create visual “popping” or flickering that is difficult to control compared to the deterministic spawn-rate scaling used in Cascade.

### **Summary of Elimination in LOD**

When a particle system reaches its maximum distance or lowest detail setting, the system logic will **eliminate** the rendering of those particles entirely. This **elimination** of draw calls and simulation overhead is critical for maintaining high frame rates in complex scenes, ensuring that only the most relevant visual data is processed by the GPU.