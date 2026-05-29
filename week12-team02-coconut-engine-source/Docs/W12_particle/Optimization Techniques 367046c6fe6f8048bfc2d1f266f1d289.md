# Optimization Techniques

Ranking optimization techniques is highly dependent on whether your project is **CPU-bound** (too much logic/simulation) or **GPU-bound** (too many pixels/shaders). However, in general production environments, the following ranking represents the most significant “wins” for performance.

### **1. Overdraw Mitigation (GPU Impact: Extreme)**

This is almost always the #1 performance killer in Unreal Engine. Even a small number of particles can tank the frame rate if they cover the whole screen with multiple layers of transparency.

- **Why:** Each overlapping transparent pixel requires the GPU to read, blend, and write data multiple times.
- **The Win:** Using **Particle Cutouts** and reducing **Spawn Rates** can **eliminate** massive amounts of redundant pixel processing. This is the difference between an effect being “free” and it costing 5ms of GPU time.

### **2. Fixed Bounds (CPU Impact: High)**

By default, the CPU checks every single particle’s position to calculate the system’s size for culling.

- **Why:** If you have 1,000 particles, the CPU performs 1,000 checks every frame just to see if it should draw them.
- **The Win:** Setting **Fixed Bounds** **eliminates** this per-particle iteration. The CPU does a single check against a static box. In a scene with many emitters, this is the most effective way to reduce “Game Thread” (CPU) bottlenecks.

### **3. GPU Sprite Simulation (Scalability Impact: High)**

Moving the simulation from the CPU to the GPU.

- **Why:** The CPU is excellent at complex logic but poor at moving thousands of individual points. The GPU is designed for exactly this.
- **The Win:** This allows you to scale from hundreds of particles to hundreds of thousands. It **eliminates** the CPU simulation cost entirely, though it increases GPU memory usage slightly.

### **4. Distance Culling & LODs (General Impact: Medium-High)**

Aggressively turning off or simplifying effects based on distance.

- **Why:** There is no reason to simulate a complex campfire or a mechanical **elimination** spark effect if the player is 100 meters away.
- **The Win:** This **eliminates** both the CPU simulation and the GPU draw calls for distant objects. It is essential for maintaining a stable frame rate in open-world games.

### **5. Eliminating Expensive Modules (CPU/Memory Impact: Medium)**

Disabling specific features like “Collision,” “Light Emission,” or “External Forces.”

- **Why:** Collision modules in Cascade often use expensive CPU raycasts (Line Traces).
- **The Win:** By **eliminating** collisions or high-frequency noise modules on background particles, you save significant CPU “Tick” time.