# Beam Modules

In Cascade, there is a specific category of modules that inherit from `UParticleModuleBeamBase`. These modules are functionally exclusive to beam emitters because they rely on the **Source-to-Target** architectural logic that standard sprite or ribbon particles do not possess.

Here are the primary exclusive modules and their roles:

### **1. Beam Data (The Core)**

This is the “Required” module equivalent for beams. It is the only place where you can define the fundamental structural properties that eliminate the beam’s identity as a simple sprite.

- **Beam Method:** Determines if the beam is a direct line, a spline, or a branch.
- **Texture Tile / Distance:** Manages the UV scaling logic unique to long strips.
- **Sheets:** Dictates how many planes of geometry are generated (e.g., 1 for a flat ribbon, 2 for a cross-shape).
- **Interpolation Points:** Controls the tessellation (the “smoothness” of the curve).

### **2. Beam Source**

This module is exclusive to beams because it defines the **origin point** of the geometry. Standard particles spawn at the emitter location; beams require a specific start point that can be:

- **User-defined:** A specific coordinate.
- **Emitter:** Attached to the emitter’s location.
- **Actor:** Attached to a specific AActor in the level (using the Source Name field).

### **3. Beam Target**

Similar to the Source module, the Target module is exclusive because it defines the **termination point**. Without this module, the engine would not know where to “stretch” the vertex buffer. It allows for the same attachment logic as the Source (Actor-based, User-based, or Emitter-based).

### **4. Beam Noise**

While standard particles have “Orbit” or “Velocity” modules, **Beam Noise** is a specialized implementation designed to oscillate the segments *between* the source and target.

- It is unique because it performs **Segment Displacement**. It doesn’t move the particle’s center; it jitters the vertices along the path.
- It includes specific logic for **Frequency** and **Noise Tessellation** that only makes sense in the context of a connected line.