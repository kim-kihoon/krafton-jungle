# Plan — Cascade Particle Renderer

## Context

We are adding Cascade-style particle system rendering to the engine. The teammate is implementing the **CPU side** (`UParticleSystem` asset, `UParticleSystemComponent`, `UParticleEmitter`, `UParticleLODLevel`, `UParticleModule*` and the `FParticleEmitterInstance` simulation) and the **editor UI**. This plan covers the **renderer side only**: the scene proxy, dynamic emitter data snapshots, vertex packing, shaders, and AlphaBlend pass integration.

**Scope (this pass):** Sprite emitters, SubUV (atlas-animated) sprites, and Mesh (hardware-instanced) emitters. Translucent particles sorted back-to-front per emitter. GC / UObject ownership cleanup of dynamic data and module assets is **postponed** — for now, dynamic emitter snapshots are owned by the proxy and freed each frame; transient materials live until proxy destruction.

**Reference docs:** [Docs/W12_particle/](../../../Documents/GitHub/Week12/Docs/W12_particle/) — `FDynamicEmitterDataBase`, `FParticleDataContainer`, `Geometry Shader`, `FDynamicEmitterReplayDataBase`, `Optimization Techniques`.

---

## CPU↔Render Contract (boundary owned by us)

Define the snapshot structs in a renderer header. Teammate's `UParticleSystemComponent::Tick` builds one per emitter and hands them to the proxy each frame.

**New header — `KraftonEngine/Source/Engine/Render/Particle/ParticleDynamicData.h`**

```cpp
enum class EDynamicEmitterType : uint8 { None, Sprite, Mesh /*, Beam, Trail*/ };

struct FParticleDataContainer       // raw byte block from CPU
{
    int32 ParticleDataNumBytes = 0;
    int32 ParticleIndicesNumShorts = 0;
    uint8*  ParticleData    = nullptr; // owned, single allocation
    uint16* ParticleIndices = nullptr; // points inside same block
    void Alloc(int32 DataBytes, int32 IndexCount);
    void Free();
};

struct FDynamicEmitterReplayDataBase
{
    EDynamicEmitterType eEmitterType = EDynamicEmitterType::None;
    int32 ActiveParticleCount = 0;
    int32 ParticleStride      = 0;     // sizeof(FBaseParticle) + payload sum
    FParticleDataContainer DataContainer;
    FVector Scale = FVector::One();
    int32 SortMode = 0;                // 0 = none, 1 = view distance back-to-front
};

struct FDynamicSpriteEmitterReplayDataBase : public FDynamicEmitterReplayDataBase
{
    UMaterial* MaterialInterface = nullptr;
    // RequiredModule snapshot (screen alignment, blend, atlas dims):
    int32 SubImages_Horizontal = 1;
    int32 SubImages_Vertical   = 1;
    uint8 ScreenAlignment      = 0;    // PSA_Square/PSA_Velocity/PSA_FacingCameraPosition
    EBlendState BlendMode      = EBlendState::AlphaBlend;
    // Offsets into FBaseParticle payload (filled by CacheEmitterModuleInfo on CPU)
    int32 SubUVDataOffset      = -1;
    int32 DynamicParameterDataOffset = -1;
};

struct FDynamicMeshEmitterReplayData : public FDynamicSpriteEmitterReplayDataBase
{
    class UStaticMesh* StaticMesh = nullptr;
    int32 MeshAlignment = 0;
};

struct FDynamicEmitterDataBase            // render-side wrapper
{
    int32 EmitterIndex = -1;
    virtual ~FDynamicEmitterDataBase() = default;
    virtual const FDynamicEmitterReplayDataBase& GetSource() const = 0;
};

struct FDynamicSpriteEmitterDataBase : public FDynamicEmitterDataBase
{
    void SortSpriteParticles(int32 SortMode, const FVector& CameraOrigin,
                             const FMatrix& LocalToWorld,
                             uint16* InOutIndices, int32 Count,
                             const uint8* ParticleData, int32 Stride);
    virtual int32 GetDynamicVertexStride() const = 0;
};

struct FDynamicSpriteEmitterData : public FDynamicSpriteEmitterDataBase
{
    FDynamicSpriteEmitterReplayDataBase Source;
    const FDynamicEmitterReplayDataBase& GetSource() const override { return Source; }
    int32 GetDynamicVertexStride() const override { return sizeof(FParticleSpriteVertex); }
};

struct FDynamicMeshEmitterData : public FDynamicSpriteEmitterData
{
    FDynamicMeshEmitterReplayData MeshSource;
    int32 GetDynamicVertexStride() const override { return sizeof(FMeshParticleInstanceVertex); }
};
```

**`FBaseParticle` layout** (must match CPU side — coordinate with teammate; put in shared header `Engine/Particle/ParticleTypes.h`): `FVector Location; FVector OldLocation; FVector Velocity; float RelativeTime; float Lifetime; FVector BaseSize; FVector Size; float Rotation; float RotationRate; FColor Color; FColor BaseColor; uint32 Flags;` (~88 bytes; payload follows).

---

## Renderer Components

### 1. Particle vertex types — `Render/Types/VertexTypes.h` (extend existing)

```cpp
struct FParticleSpriteVertex            // 4 verts per particle (VS expansion, no GS)
{
    FVector  Position;                  // particle center world pos
    FVector  Size;                      // (sizeX, sizeY, subImageLerp)
    FVector2 UV;                        // corner ID -> base UVs (0,0)..(1,1)
    FColor   Color;
    float    Rotation;
    float    SubImageIndex;             // frame index, fractional for blend
    FVector  Velocity;                  // for PSA_Velocity alignment
};

struct FMeshParticleInstanceVertex      // 1 per particle, instanced
{
    FMatrix Transform;                  // 64B — world matrix of the particle
    FColor  Color;
    FVector4 DynamicParam;
};
```

### 2. Scene proxy — `Render/Proxy/ParticleSystemSceneProxy.{h,cpp}` (new)

`FParticleSystemSceneProxy : public FPrimitiveSceneProxy` — one proxy per `UParticleSystemComponent`. Multiple sections, one per emitter.

```cpp
class FParticleSystemSceneProxy : public FPrimitiveSceneProxy
{
public:
    FParticleSystemSceneProxy(UParticleSystemComponent* InComponent);
    ~FParticleSystemSceneProxy() override;

    // Called by Component each frame after CPU sim. Proxy takes ownership.
    void UpdateDynamicData(TArray<FDynamicEmitterDataBase*>&& NewData);

    // Required overrides:
    void UpdateTransform()   override;  // Local-space proxy: just CachedWorldPos + identity model
    void UpdateMaterial()    override;  // Pull material from each emitter Source
    void UpdateVisibility()  override;
    void UpdateMesh()        override;  // Reset SectionDraws; sized by emitter count
    void UpdatePerViewport(const FFrameContext& Frame) override;

    bool PrepareDrawBuffer(ID3D11Device*, ID3D11DeviceContext*, FDrawCommandBuffer&) const override;
    bool PrepareDrawCommandBindings(ID3D11Device*, ID3D11DeviceContext*,
        const FPrimitiveDrawOptions&, FDrawCommand&) const override;

    const char* GetVertexShaderEntryName() const override { return "VS_ParticleSprite"; }

private:
    // Per-emitter draw range inside the shared dynamic VB/IB
    struct FEmitterDraw
    {
        EDynamicEmitterType Type;
        UMaterial* Material;
        uint32 FirstIndex;
        uint32 IndexCount;
        // mesh path: per-emitter instance buffer + base static-mesh VB/IB
        FDynamicVertexBuffer InstanceVB;
        FMeshBuffer*         MeshGeom = nullptr;   // borrowed from UStaticMesh
        uint32               InstanceCount = 0;
    };

    void PackSpriteEmitter(const FFrameContext& Frame, FDynamicSpriteEmitterData& Emitter,
                           TArray<FParticleSpriteVertex>& OutVerts,
                           TArray<uint32>& OutIndices, uint32& IndexCursor);
    void PackMeshEmitter (const FFrameContext& Frame, FDynamicMeshEmitterData& Emitter);

    TArray<FDynamicEmitterDataBase*> DynamicData;   // owned, freed on next UpdateDynamicData
    TArray<FEmitterDraw>             EmitterDraws;

    // Sprite path — shared across all sprite emitters this proxy owns
    FDynamicVertexBuffer SpriteVB;
    FDynamicIndexBuffer  SpriteIB;
    FConstantBuffer      ParticleParamCB;   // b2: per-emitter (alignment mode, sub-uv dims)
};
```

### 3. `UParticleSystemComponent::CreateSceneProxy()` (teammate adds, we provide signature)

```cpp
FPrimitiveSceneProxy* UParticleSystemComponent::CreateSceneProxy() override
{
    return new FParticleSystemSceneProxy(this);
}
```

Teammate's `TickComponent` calls `Proxy->UpdateDynamicData(std::move(NewSnapshots))` after simulation.

---

## Shaders

**New folder `KraftonEngine/Shaders/Particle/`** with two shaders.

### `ParticleSprite.hlsl`
- VS: `VS_ParticleSprite(FParticleSpriteVertex In) -> PS_Input_ParticleSprite`
  - 4 verts/particle. `UV.xy` (0..1) is the corner ID (already packed CPU-side).
  - Apply rotation about particle center using `Rotation`.
  - Apply screen alignment based on `b2.ScreenAlignment`: Square (camera up/right), Velocity (use `Velocity` as up), FacingCameraPosition.
  - World pos = `Position + (RotMat * (UV*2-1) * Size.xy)` projected onto camera basis.
  - Output: clip pos, UV remapped through SubUV region (per-particle frame from `SubImageIndex`), Color.
- PS: sample atlas (t0), modulate by Color, alpha-blend.
- Reuse `Common/Functions.hlsli` (`ApplyMVP` not used — we go straight to view*proj).

### `ParticleMesh.hlsl`
- VS: `VS_ParticleMesh(VS_Input_PNCT vert, FMeshParticleInstanceVertex inst)` — read instance transform via second IA slot.
- Standard lit/unlit PS depending on emitter material flags.

**Material slot reuse:** sprite material binds atlas to `Diffuse` slot (t0) like SubUV. Per-emitter `b2` CB carries `(SubUVCols, SubUVRows, ScreenAlignment, _pad)`.

---

## Frame Flow (renderer perspective)

1. **CPU Tick** (teammate): each `FParticleEmitterInstance` simulates → builds a `FDynamicSpriteEmitterData` (or Mesh variant) — copies `ParticleData` bytes + `ParticleIndices` into the snapshot's `FParticleDataContainer`. Pushes the pointer to a `TArray<FDynamicEmitterDataBase*>`.
2. **Component**: hands array to `Proxy->UpdateDynamicData()`; proxy deletes previous snapshots, stores new ones; marks `UpdateMesh` / `UpdateMaterial` dirty.
3. **RenderCollector** (existing — [RenderCollector.h:20](../../../Documents/GitHub/Week12/KraftonEngine/Source/Engine/Render/Pipeline/RenderCollector.h)): enumerates scene proxies, frustum-culls using `CachedBounds` (proxy supplies system-wide AABB from snapshot positions or component fixed bounds).
4. **`UpdatePerViewport`**: For each sprite emitter — sort `ParticleIndices` by camera distance (using `FDynamicSpriteEmitterDataBase::SortSpriteParticles`, when `SortMode==1`). Optionally compute per-emitter alignment basis here.
5. **`PrepareDrawBuffer`**: lazily (re)pack into `SpriteVB`/`SpriteIB` once per frame:
   - For each sprite emitter, loop over `ActiveParticleCount` in sorted order; for each particle, append 4 `FParticleSpriteVertex` (corner IDs 0..3) and 6 indices to the shared buffers. Index ranges per emitter are recorded in `FEmitterDraw`.
   - For each mesh emitter, build a separate per-emitter instance VB (`FDynamicVertexBuffer InstanceVB`, stride = `sizeof(FMeshParticleInstanceVertex)`).
6. **`UpdateMesh`** rebuilds `SectionDraws` — one `FMeshSectionDraw{Material, FirstIndex, IndexCount}` per sprite emitter. Mesh emitters can't use the `SectionDraws` IB path (they need instanced draw), so they emit via custom DrawCommands in `PrepareDrawCommandBindings`/a small override.
7. **DrawCommandBuilder** ([DrawCommandBuilder.cpp:138](../../../Documents/GitHub/Week12/KraftonEngine/Source/Engine/Render/Command/DrawCommandBuilder.cpp)): the existing loop walks `SectionDraws` and emits one `FDrawCommand` per emitter section with `Pass = AlphaBlend`. Sort key is `(Pass, Shader, VB, Diffuse SRV)` — fine for grouping; per-particle order is already baked into the IB.
8. **AlphaBlendPass** ([AlphaBlendPass.h:5](../../../Documents/GitHub/Week12/KraftonEngine/Source/Engine/Render/RenderPass/AlphaBlendPass.h)): draws commands via the existing default `RenderPassBase::Execute()` — no changes needed.

### Mesh path drawing
The current `FDrawCommand` only supports `DrawIndexed`. For instanced draw we need either:
- **Recommended:** add an `InstanceCount` field + secondary `InstanceVB` to `FDrawCommandBuffer` (small extension to [DrawCommand.h:16](../../../Documents/GitHub/Week12/KraftonEngine/Source/Engine/Render/Command/DrawCommand.h)) and let `RenderPassBase::Execute()` call `DrawIndexedInstanced` when `InstanceCount > 1`.
- The proxy's `PrepareDrawCommandBindings` fills `InstanceVB` and `InstanceCount` for mesh emitter sections; sprite emitters leave it at 0/1.

---

## Sorting Helper

Implement `FDynamicSpriteEmitterDataBase::SortSpriteParticles` in `ParticleDynamicData.cpp`:

```cpp
// SortMode 1: back-to-front by view-space depth
for each i in [0, Count):
    const FBaseParticle& P = *(const FBaseParticle*)(ParticleData + Indices[i]*Stride);
    Key[i].SortDist = -FVector::Dot(P.Location * LocalToWorld - CameraOrigin, ViewForward);
    Key[i].Idx = Indices[i];
std::sort(Key, Key+Count, by SortDist desc);
for each i: Indices[i] = Key[i].Idx;
```

`uint16` index limit — matches Cascade's hard ceiling of 65,535 active particles per emitter. Fine.

---

## Files to Add

| Path | Purpose |
|---|---|
| `Source/Engine/Render/Particle/ParticleDynamicData.h` | Snapshot structs (data contract with CPU) |
| `Source/Engine/Render/Particle/ParticleDynamicData.cpp` | `FParticleDataContainer::Alloc/Free`, `SortSpriteParticles` |
| `Source/Engine/Render/Proxy/ParticleSystemSceneProxy.h` | Proxy declaration |
| `Source/Engine/Render/Proxy/ParticleSystemSceneProxy.cpp` | Proxy implementation + per-frame vertex packing |
| `Source/Engine/Particle/ParticleTypes.h` | `FBaseParticle` shared with teammate (coordinate fields) |
| `Shaders/Particle/ParticleSprite.hlsl` | VS expansion + PS atlas sample |
| `Shaders/Particle/ParticleMesh.hlsl` | Instanced mesh VS + lit/unlit PS |

## Files to Modify (small)

| Path | Change |
|---|---|
| [Render/Types/VertexTypes.h](../../../Documents/GitHub/Week12/KraftonEngine/Source/Engine/Render/Types/VertexTypes.h) | Add `FParticleSpriteVertex`, `FMeshParticleInstanceVertex` + their `D3D11_INPUT_ELEMENT_DESC` arrays |
| [Render/Command/DrawCommand.h](../../../Documents/GitHub/Week12/KraftonEngine/Source/Engine/Render/Command/DrawCommand.h) | Add `ID3D11Buffer* InstanceVB`, `uint32 InstanceVBStride`, `uint32 InstanceCount` to `FDrawCommandBuffer` |
| `Render/RenderPass/RenderPassBase.cpp` | When `Cmd.Buffer.InstanceCount > 1`, bind second IA slot and call `DrawIndexedInstanced` |
| [Render/Shader/ShaderManager](../../../Documents/GitHub/Week12/KraftonEngine/Source/Engine/Render/Shader/) | Register `EShaderPath::ParticleSprite`, `EShaderPath::ParticleMesh` |

## Reused Existing Code (do not duplicate)

- `FPrimitiveSceneProxy` Update-dirty pattern — [PrimitiveSceneProxy.h:97-105](../../../Documents/GitHub/Week12/KraftonEngine/Source/Engine/Render/Proxy/PrimitiveSceneProxy.h)
- `FDynamicVertexBuffer::EnsureCapacity` + `Update` — [Buffer.h:133](../../../Documents/GitHub/Week12/KraftonEngine/Source/Engine/Render/Resource/Buffer.h) — exactly fits per-frame particle uploads
- `FDynamicIndexBuffer` — same file, for the 6-indices/particle IB
- `UMaterial::CreateTransient` — used by `FSubUVSceneProxy` ([SubUVSceneProxy.cpp:48](../../../Documents/GitHub/Week12/KraftonEngine/Source/Engine/Render/Proxy/SubUVSceneProxy.cpp)) — same pattern for per-emitter materials
- `FConstantBuffer` + `BindPerShaderCB<T>` at `ECBSlot::PerShader0` (b2) — see [SubUVSceneProxy.cpp:32](../../../Documents/GitHub/Week12/KraftonEngine/Source/Engine/Render/Proxy/SubUVSceneProxy.cpp)
- `FMeshSectionDraw` + `BuildCommandForProxy` — proxy emits one section per emitter, no builder change needed for sprite path
- `RenderPassBase::Execute` default loop — handles AlphaBlend pass already
- `FDrawCommand::ComputeSortKey` — adequate; per-particle order is in the IB itself

## Deferred (explicit non-goals)

- GC / lifetime ownership of `FDynamicEmitterDataBase*` snapshots and transient materials — currently raw-owned and leaked at proxy destruction time. Revisit when garbage collection lands.
- LOD switching of render data (CPU-driven on teammate's side; renderer just consumes whatever snapshot arrives).
- GPU sprite simulation (compute-shader particle sim) — `Optimization Techniques` #3. CPU sim only.
- Beams / Ribbons / Trails.
- Hi-Z occlusion culling integration for particle bounds.
- Particle shadows / receiving lights — particles render unlit in AlphaBlend pass.

---

## Verification

1. **Build** — `msbuild KraftonEngine.sln /p:Configuration=Debug /p:Platform=x64`. After regenerating project files (`python Scripts/GenerateProjectFiles.py`) the new sources must compile.
2. **Smoke test** — once teammate's `UParticleSystemComponent` can spawn a single sprite emitter (constant spawn rate, linear velocity, finite lifetime), drop one into a scene. Expect: camera-facing quads moving along velocity, alpha-blended against the scene depth, no GS warnings, no Z-fight ordering artifacts when viewed from multiple angles (validates the per-emitter sort).
3. **SubUV** — set `SubImages_Horizontal=8`, `_Vertical=8`, animate `SubImageIndex` over particle life. Expect a flipbook explosion-style atlas playing per particle.
4. **Mesh emitter** — assign a `UStaticMesh` to a mesh-type emitter; expect instanced draws of that mesh at each particle position with per-particle color/scale.
5. **GPU profiler / RenderDoc** — confirm: (a) `DrawIndexed` count == emitter count for sprites (one per section), (b) `DrawIndexedInstanced` with correct InstanceCount for mesh emitters, (c) no GS stage bound, (d) Pass column = `AlphaBlend`.
6. **No-CPU-sim regression check** — set ActiveParticleCount=0 in a snapshot; proxy must skip emitting a draw command for that emitter (not draw zero-length, not crash).