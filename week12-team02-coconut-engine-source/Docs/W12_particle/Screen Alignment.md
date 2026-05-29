# Screen Alignment

For sprite particles in Cascade, the `EParticleScreenAlignment` enumeration defines how the particle’s quad is oriented in 3D space relative to the camera and the particle’s own movement. This field is also required for non-sprite particles, but is silenced.

Here is a detailed breakdown of each behavior:

### **1. PSA_Square**

This is the most common alignment mode used for generic effects like smoke or fire.

- **Behavior:** The sprite always faces the camera plane directly.
- **Geometry:** The quad is constrained to be a perfect square, regardless of the “Size” settings (it typically uses the X-size for both dimensions).
- **Rotation:** It remains “upright” relative to the camera’s up-vector unless a **Rotation** module is added to spin it around the view axis.
- **Best Use:** Use this to eliminate “shearing” artifacts when you want a uniform look from every angle.

### **2. PSA_Rectangle**

Similar to Square, but allows for non-uniform scaling.

- **Behavior:** The sprite faces the camera plane directly.
- **Geometry:** It respects both the X and Y values defined in the **Size** modules. This allows you to create wide or tall “billboards.”
- **Best Use:** Ideal for flat UI-style elements or rectangular glows that must always face the player.

### **3. PSA_Velocity**

This mode links the sprite’s orientation to its movement vector.

- **Behavior:** The “Vertical” (Up) axis of the sprite is aligned with the particle’s **Velocity** vector. The sprite still faces the camera, but it “leans” in the direction it is traveling.
- **Logic:** As the particle accelerates or changes direction, the sprite rotates on the view axis to follow that path.
- **Best Use:** Essential for sparks, rain, or debris. It makes the particles look like they are “streaking” through the air.

### **4. PSA_AwayFromCenter**

Used primarily for radial or spherical explosions.

- **Behavior:** The sprite’s vertical axis aligns with a vector pointing away from the **Emitter Origin** (or a specified center point).
- **Logic:** If a particle is spawned at the center and moves outward, it behaves similarly to Velocity alignment. However, if the particle is stationary but positioned away from the center, it will still point outward.
- **Best Use:** Use this to create “shockwave” spikes or radial rays that point outward from an explosion’s epicenter.

### **5. PSA_TypeSpecific**

This is a “pass-through” setting for specialized emitter types.

- **Behavior:** It tells the renderer to ignore standard camera-facing logic and instead use the logic defined within a specific module (like a **Mesh Alignment** module or a specialized **SubUV** setup).
- **Logic:** It effectively hands over the rotation/alignment responsibility to other modules in the stack.
- **Best Use:** Use this when you have a custom module that needs to eliminate standard behavior to provide its own coordinate math.

### **6. PSA_FacingCameraPosition**

While `PSA_Square/Rectangle` face the camera **plane**, this mode faces the camera **point**.

- **Behavior:** The sprite rotates to look directly at the camera’s world-space coordinates.
- **Difference:** On large screens or wide FOVs, standard plane-facing sprites can look “skewed” at the edges of the screen. `FacingCameraPosition` ensures each individual particle is angled toward the lens.
- **Best Use:** High-quality lens flares or orbs where the perspective distortion of the screen edges must be eliminated.

### **7. PSA_AlongCustomAxis**

This mode allows the user to define a static world-space or local-space axis for alignment.

- **Behavior:** The sprite aligns one axis toward a custom vector (defined in the **Lock Axis** or similar modules) while still attempting to face the camera with its front plane.
- **Best Use:** Creating effects like “ground cracks” or flat circular rings that need to stay parallel to the floor (Z-axis) while still rotating to face the player.

### **Summary of Axis Calculation**

To eliminate confusion when debugging, remember that Cascade calculates the “Final Look” of a sprite using a **Cross Product**:

1. **Forward Vector:** Calculated from the Camera Position to the Particle Position.
2. **Up Vector:** Derived from the `ScreenAlignment` (e.g., Velocity or Camera-Up).
3. **Right Vector:** The Cross Product of Forward and Up.

The engine then uses these three vectors to build the final rotation matrix for the sprite quad.