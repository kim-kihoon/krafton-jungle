# GPU Skin Cache Plan

## Goal

Vertex shader 기반 GPU skinning을 유지하면서, 사용자 옵션에 따라 compute shader가 만든 변형 정점 결과를 같은 프레임의 여러 렌더 패스에서 재사용할 수 있게 한다.

초기 구현은 skeletal skinning만 대상으로 한다. 다만 중단기 지원 범위에 morph target이 포함되어 있으므로, `SkinCache`라는 이름은 "skinning + morph target(추후 구현)을 포함한 최종 vertex deformation cache"라는 의미로 사용한다.

## Direction

채택 방향은 `2.5안`이다.

- 2번 타협안처럼 Skin Cache 전용 대규모 VertexFactory 리팩터는 피한다.
- 앞단에 얇은 공용 GPU resource accounting 기반을 둔다.
- Skin Cache는 이 공용 기반의 첫 신규 사용자로 구현한다.
- Unreal Engine의 GPUSkinCache를 개념적으로 참고하되, 현재 엔진 규모에 맞게 단순화한다.

Unreal 참고 방향의 핵심은 source vertex를 중복 복사하지 않고 shader-readable source buffer로 재사용하며, compute output은 렌더링 가능한 vertex stream으로 넘기는 것이다. 따라서 본 계획에서는 `SV_VertexID` 전용 fetch 경로보다 명시적인 buffer wrapper 3종을 둔다.

| Buffer | Bind Intent | Primary Role |
| --- | --- | --- |
| `FVertexBuffer` | `VB` | 일반 IA 입력용 vertex buffer |
| `FVertexSourceBuffer` | `VB + SRV` | bind-pose/source vertex를 렌더 입력과 compute 입력으로 공유 |
| `FRWVertexBuffer` | `VB + SRV + UAV` | compute output이면서 render input인 deformed vertex buffer |

다만 compute output은 기존 `SkeletalMesh` vertex factory 입력과 같은 레이아웃이 아니다. 기존 skeletal input은 `BLENDINDICES`와 `BLENDWEIGHT`를 요구하지만, Skin Cache output은 이미 변형된 position/normal/tangent 계열 정점이다. 따라서 render pass 소비 단계에는 Skin Cache 전용 vertex factory 또는 pass-through shader entry가 필요하다.

## Explicit Non Goals

이번 계획에는 아래 작업을 포함하지 않는다.

- Render Graph 도입
- pass dependency 자동 resolve
- 모든 D3D 리소스 생성을 중앙 manager로 강제 이전
- VertexFactory 객체화 선행
- `FRenderResources` 전체 재설계
- 자동 메모리 예산 기반 skinning mode 전환
- recompute tangents
- ray tracing 전용 skin cache entry
- async compute 전환

## Current Baseline

현재 skeletal 변형 경로는 세 가지 상태로 나뉜다.

| Mode | Current State |
| --- | --- |
| CPU | CPU에서 `SkinnedVertices`를 만들고 component별 dynamic VB에 업로드 |
| GPUVertexShader | bind-pose VB와 bone matrix SRV를 사용해 각 draw/pass의 VS에서 skinning |
| GPUComputeSkinCache | enum은 있지만 `FSkinningRuntimeSettings::SetMode()`에서 `GPUVertexShader`로 되돌리는 예약 상태 |

현재 draw pass 대부분은 `FRenderCommand::MeshBuffer`에서 직접 VB/IB를 꺼내 `IASetVertexBuffers()`와 `IASetIndexBuffer()`를 호출한다. 따라서 Skin Cache 결과를 넣기 위해서는 draw-time vertex stream override 또는 draw binding helper가 필요하다.

현재 `FVertexBuffer::CreateRaw()`는 기본적으로 vertex buffer bind만 가진다. Compute shader가 bind-pose vertex를 읽으려면 source vertex SRV 경로를 별도로 확보해야 한다.

## Mode Policy

Compute Skin Cache는 GPU 메모리를 추가로 사용하므로 모든 skeletal mesh에 일괄 적용하지 않는다. 첫 정책은 엔진의 자동 판단보다 사용자 선택을 신뢰한다.

User-facing mode는 우선 두 단계로 유지한다.

| User Mode | Component Option | Effective Method |
| --- | --- | --- |
| `skinmode cpu` | ignored | CPU skinning |
| `skinmode gpu` | `bUseSkinCache == false` | GPU vertex shader skinning |
| `skinmode gpu` | `bUseSkinCache == true` | GPU compute Skin Cache |

`skinmode compute`처럼 모든 skeletal mesh를 compute path로 강제하는 전역 모드는 초기 목표가 아니다. Compute path는 `skinmode gpu` 상태에서 component opt-in으로만 사용한다.

`bUseSkinCache`는 `USkeletalMeshComponent`에서 사용할 수 있는 옵션으로 추가한다. 구현상 skeletal 변형 핵심 로직이 `USkinnedMeshComponent`에 있으므로, 실제 필드는 `USkinnedMeshComponent`에 두고 `USkeletalMeshComponent`가 상속받는 형태가 자연스럽다.

User-facing mode와 실제 render path는 분리한다. `ESkinningMode`는 console/user setting 성격으로 유지하고, component 단위 실제 선택은 별도 resolver가 `EEffectiveVertexDeformationMethod` 같은 내부 enum으로 반환한다.

Default:

- `bUseSkinCache = false`
- 새 옵션을 켜지 않은 기존 skeletal mesh는 `skinmode gpu`에서도 기존 VS skinning을 유지한다.

Fallback policy:

- `bUseSkinCache == true`여도 cache allocation 또는 compute 준비가 실패하면 해당 component는 명시적으로 VS skinning으로 fallback한다.
- fallback은 조용히 숨기지 않고 console/stat UI에 표시한다.
- 메모리 기반 자동 전환은 초기 범위에서 제외한다.

## Target Flow

### CPU Skinning

```text
USkeletalMesh bind pose vertices
 -> USkinnedMeshComponent CPU skinning
 -> SkinnedVertices
 -> FMeshBufferManager component dynamic VB upload
 -> FRenderCommand.MeshBuffer
 -> FMeshDrawBinding uses MeshBuffer VB/IB
 -> Main render pass consumes already-skinned VB
```

### GPU Vertex Shader Skinning

```text
USkeletalMesh bind pose vertices
 -> skinmode gpu
 -> SkeletalMeshComponent.bUseSkinCache == false
 -> USkinnedMeshComponent bone matrix update
 -> FMeshBufferManager bind-pose VB/IB + bone matrix SRV
 -> FRenderCommand.MeshBuffer + FSkeletalGpuSkinningPayload
 -> FMeshDrawBinding uses MeshBuffer VB/IB
 -> Skeletal VS skins every pass/draw
```

### GPU Compute Skin Cache

```text
USkeletalMesh bind pose vertices
 -> skinmode gpu
 -> SkeletalMeshComponent.bUseSkinCache == true
 -> USkinnedMeshComponent bone matrix update
 -> FSkinCacheManager prepares source SRV, bone matrix SRV, output FRWVertexBuffer
 -> FSkinCachePass dispatches before DepthPrePass
 -> CS writes deformed vertices into FRWVertexBuffer UAV
 -> FRenderCommand keeps MeshBuffer for IB and sets FVertexStreamOverride to FRWVertexBuffer VB
 -> SkinCache 전용 vertex factory/pass-through VS가 already-skinned stream을 입력으로 사용
 -> FMeshDrawBinding uses override VB + MeshBuffer IB
 -> Main render pass consumes already-skinned vertex stream
```

## New And Changed Types

### GPU Accounting

| Type | Role |
| --- | --- |
| `EGpuResourceCategory` | GPU resource category. Initial values: `Mesh`, `Skeletal`, `SkinCache`, `Particles`, `Shadow`, `Lighting`, `Editor`, `Other` |
| `FGpuResourceMemoryRecord` | One resource memory record: category, name, bytes, count, stride, owner/debug label |
| `FGpuResourceMemoryStats` | Aggregates records and category totals |
| `AppendGpuMemoryStats()` pattern | Pull-based reporting. Owners append their current resources when stat UI asks |

Use pull-based reporting instead of global register/unregister. This avoids stale records and release-order bugs.

### Buffer Layer

| Type | Role |
| --- | --- |
| `FVertexBuffer` | VB-only buffer. 일반 render pass의 IA 입력용 |
| `FVertexSourceBuffer` | VB + SRV buffer. bind-pose/source vertex를 VS input과 CS input에서 공유 |
| `FRWVertexBuffer` | VB + SRV + UAV buffer. CS가 쓰고 render pass가 읽는 deformed vertex stream |
| `FStructuredBuffer` tracking fields | Keep existing role, add byte/category/debug reporting |
| `FIndexBuffer` tracking fields | Add byte/category/debug reporting without changing ownership |

Declaration comments:

```cpp
// 일반 렌더 패스의 IA 입력으로만 사용하는 Vertex Buffer
class FVertexBuffer;

// 원본/source vertex를 렌더 입력과 shader input으로 공유하기 위한 Vertex Buffer
// Skin Cache compute shader가 bind-pose vertex를 읽어야 하므로 VB + SRV 조합 사용
class FVertexSourceBuffer;

// Skin Cache 시에 사용되는 Vertex Buffer
// Compute shader의 output이면서 동시에 메인 렌더 패스의 input이므로 VB + SRV + UAV 조합 사용
class FRWVertexBuffer;
```

주석은 실제 선언부에도 동일한 의도로 추가한다. 후속 작업자가 용도를 바로 읽을 수 있도록 한국어 음슴체를 사용하고 온점은 붙이지 않는다.

`FVertexSourceBuffer` should expose:

- `Create(ID3D11Device*, const void* InData, uint32 InVertexCount, uint32 InStride, EGpuResourceCategory, const FString& DebugName)`
- `Release()`
- `GetBuffer()`
- `GetSRV()`
- `GetStride()`
- `GetCount()`
- `GetByteSize()`
- `AppendGpuMemoryStats(FGpuResourceMemoryStats&)`

`FRWVertexBuffer` should expose:

- `Create(ID3D11Device*, uint32 InStride, uint32 InCount, EGpuResourceCategory, const FString& DebugName)`
- `Release()`
- `GetBuffer()`
- `GetSRV()`
- `GetUAV()`
- `GetStride()`
- `GetCount()`
- `GetByteSize()`
- `AppendGpuMemoryStats(FGpuResourceMemoryStats&)`

### Draw Binding Layer

| Type | Role |
| --- | --- |
| `FVertexStreamOverride` | Non-owning draw-time override for vertex buffer, stride, count, offset |
| `FMeshDrawBinding` | Non-owning resolved IA binding: VB, IB, stride, counts, offsets |
| `BuildMeshDrawBinding(const FRenderCommand&, FMeshDrawBinding&)` | Applies override-or-default policy in one place |
| `BindMeshDrawBinding(ID3D11DeviceContext*, const FMeshDrawBinding&)` | Performs IA binding |

`FMeshBuffer` is not renamed. It remains the long-lived mesh resource owner. `FMeshDrawBinding` is a transient view for a single draw.

Policy:

```text
VertexBuffer:
  if FRenderCommand.VertexStreamOverride is valid, use override VB
  otherwise use FRenderCommand.MeshBuffer vertex buffer

IndexBuffer:
  use FRenderCommand.MeshBuffer index buffer
```

Mesh overlay는 초기 override 대상에서 제외한다. Bone-weight heatmap은 원본 `BoneIndices`/`BoneWeights`를 읽고 shader에서 다시 skinning하므로, `FDeformedVertex`만 가진 stream으로 대체하면 기능이 깨진다. `MeshOverlayRenderPass`는 helper refactor 대상일 수는 있지만, Skin Cache override는 적용하지 않고 기존 skeletal VB + bone matrix SRV 경로를 유지한다.

### Vertex Factory / Shader Input Layer

| Type | Role |
| --- | --- |
| `EVertexFactoryType::SkinCacheSkeletalMesh` | Skin Cache output stream용 vertex factory. 현재 skinning, 추후 morph target 포함 |
| Skin Cache pass-through VS entries | Base/depth/shadow/selection/ID-pick에서 `FDeformedVertex`를 받아 추가 skinning 없이 clip-space로 넘김 |
| Existing `EVertexFactoryType::SkeletalMesh` | Bind-pose skeletal VB + `BLENDINDICES`/`BLENDWEIGHT` + bone matrix SRV를 쓰는 기존 VS skinning 경로 유지 |

Policy:

- CPU path는 기존 static/local vertex factory 또는 CPU dynamic mesh binding을 유지한다.
- GPU VS path는 기존 `SkeletalMesh` vertex factory를 유지한다.
- GPU Compute Cache path는 Skin Cache 전용 vertex factory/pass-through VS를 사용한다.
- `FDeformedVertex`에는 bone influence를 넣지 않는다. Bone influence가 필요한 debug/overlay pass는 원본 skeletal path를 유지한다.

Naming note:

```cpp
// SkinCache는 현재 GPU skinning 결과를 캐시하고 추후 morph target deformation까지 포괄함
```

### Skin Cache

| Type | Role |
| --- | --- |
| `FDeformedVertex` | Initial interleaved Skin Cache output vertex. Suggested fields: position, normal, tangent, UV, color |
| `FSkinCacheKey` | Component UUID + skeletal mesh pointer + vertex count + LOD/section policy if needed |
| `FSkinCacheEntry` | Per component/cache entry. Holds output `FRWVertexBuffer`, source SRV reference, current revision, state flags |
| `FSkinCacheManager` | Renderer-owned manager. Owns entries, creates/releases buffers, exposes valid output buffer, reports memory |
| `FVertexDeformationPayload` | Render command payload for compute-cache mode. Carries mode, entry/valid state, debug/fallback info if needed |
| `FSkinCachePass` | Render pass before `DepthPrePass`. Dispatches compute update for visible compute-cache entries |
| `USkinnedMeshComponent::bUseSkinCache` | Component-level opt-in. Active only when global mode is `skinmode gpu` |
| `EEffectiveVertexDeformationMethod` | Internal resolved method: CPU, GPU vertex shader, GPU compute cache, fallback state if needed |

Initial output uses one interleaved `FDeformedVertex` buffer. Unreal splits position/tangent buffers, but this engine should start simpler.

### Source Vertex SRV

Compute shader input으로 bind-pose vertex data를 읽는 경로는 `FVertexSourceBuffer`로 확정한다.

Policy:

- skeletal bind-pose buffer는 `FVertexSourceBuffer`로 생성한다.
- GPU VS skinning은 같은 buffer를 IA `VB`로 사용한다.
- GPU Compute Skin Cache는 같은 buffer의 `SRV`를 source vertex input으로 사용한다.
- CPU skinning dynamic VB와 일반 editor/debug VB는 `FVertexBuffer`를 유지한다.
- source vertex 메모리 중복을 피한다.
- 모든 `FVertexBuffer`를 무조건 SRV-capable로 만들지는 않는다.

Implementation shape:

- `FMeshBuffer::CreateImmutableVertexBuffer()`의 기본 동작은 유지한다.
- skeletal bind-pose 전용 생성 경로를 추가한다. Working name은 `FMeshBuffer::CreateImmutableSourceVertexBuffer()`로 둔다.
- `GetSkeletalBindPoseBuffer()`는 이 전용 경로를 사용해 내부 vertex buffer를 `FVertexSourceBuffer`로 만든다.
- static mesh, procedural mesh, CPU skinning dynamic mesh는 기존 `FVertexBuffer` 경로를 유지한다.
- draw binding helper는 기본 VB와 source VB를 같은 IA input view로 다룰 수 있어야 한다.

D3D11 creation requirement:

- `BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_SHADER_RESOURCE`
- `MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED`
- `StructureByteStride = sizeof(FSkeletalMeshVertex)`
- SRV `Format = DXGI_FORMAT_UNKNOWN`
- SRV `ViewDimension = D3D11_SRV_DIMENSION_BUFFER`

HLSL source layout requirement:

- `FSkeletalMeshVertex::BoneIndices`는 C++에서 `uint8[4]`이고 IA layout에서는 `DXGI_FORMAT_R8G8B8A8_UINT`로 해석한다.
- Compute shader의 `StructuredBuffer` 입력 구조체는 `uint8[4]`를 직접 표현하지 말고 packed `uint BoneIndicesPacked`로 읽은 뒤 byte unpack한다.
- HLSL source vertex struct의 field 순서와 byte size는 `sizeof(FSkeletalMeshVertex)`와 맞춰야 한다.
- 권장 HLSL 형태:

```hlsl
struct FSkeletalSourceVertex
{
    float3 Position;
    float4 Color;
    float3 Normal;
    float2 UVs;
    float4 Tangent;
    uint BoneIndicesPacked;
    float4 BoneWeights;
};
```

- 위 layout은 현재 C++ `FSkeletalMeshVertex`의 `BoneIndices` offset과 `BoneWeights` offset을 맞추기 위한 계약이다. C++ vertex struct가 바뀌면 compute shader source layout도 같이 갱신한다.

Memory accounting:

- `FVertexSourceBuffer`는 skeletal bind-pose baseline 리소스이므로 `GPU.Skeletal` 또는 `GPU.Mesh` 쪽에 집계한다.
- `GPU.SkinCache`에는 compute output `FRWVertexBuffer`, cache entry metadata, fallback/failed allocation count를 중심으로 집계한다.
- `stat memory`에서 Skin Cache 추가 비용을 볼 때 source vertex memory가 중복 비용처럼 보이지 않도록 표시명을 분리한다.

### Cache Manager Ownership

`FSkinCacheManager`는 `FRenderer`가 소유한다.

Policy:

- `FRenderer`가 manager를 생성, 유지, 해제한다.
- `FRenderPassContext`에는 non-owning `FSkinCacheManager*`를 전달한다.
- `FSkinCachePass`는 context의 manager를 통해 dispatch 대상 entry를 갱신한다.
- command build 단계는 manager에서 valid output을 받아 `FRenderCommand::VertexStreamOverride` 같은 resolved draw data로 기록한다.
- 일반 draw pass는 manager를 직접 조회하지 않는다. Draw pass는 `FRenderCommand`와 `FMeshDrawBinding`만 소비한다.
- component는 GPU cache buffer를 직접 소유하지 않는다. Component는 `bUseSkinCache`, deformation revision, invalidation signal 같은 CPU-side 상태만 가진다.

Reason:

- Skin Cache entry는 프레임 간 유지되는 renderer-side GPU resource다.
- `FRenderer` 소유가 device/resource release, pass 실행 순서, frame lifecycle과 가장 잘 맞는다.
- singleton보다 수명과 테스트 범위가 명확하다.
- `FRenderPipeline`보다 바깥쪽 소유이므로 pipeline/pass 재구성에도 manager 수명이 흔들리지 않는다.

## Phase Plan

### Phase 0: GPU Resource Accounting Foundation

Tasks:

- Add `EGpuResourceCategory`, `FGpuResourceMemoryRecord`, `FGpuResourceMemoryStats`.
- Add byte/category/debug-name tracking to `FStructuredBuffer`.
- Add byte/category/debug-name tracking to `FVertexBuffer`, `FIndexBuffer`, and `FMeshBuffer`.
- Add `AppendGpuMemoryStats()` to `FMeshBuffer` and `FMeshBufferManager`.
- Extend `ResourceMemoryReporter` with GPU stats collection entry points.
- Update `stat memory` UI to show a GPU section.
- Update both memory display paths in `EditorViewportOverlayWidget.cpp`:
  - viewport overlay memory text
  - debug/stat memory panel output

Initial categories to show:

- `GPU.Mesh`
- `GPU.Skeletal`
- `GPU.SkinCache`
- `GPU.Shadow`
- `GPU.Lighting`
- `GPU.Particles`
- `GPU.Other`

Validation:

- Existing `stat memory` still works.
- Existing rendering output is unchanged.
- GPU totals are non-zero for known mesh/light/shadow resources after categories are connected.

### Phase 1: Draw Binding Helper

Tasks:

- Add `FVertexStreamOverride`.
- Add `FMeshDrawBinding`.
- Add `BuildMeshDrawBinding()` and `BindMeshDrawBinding()`.
- Refactor mesh draw passes to use the helper instead of directly reading `Cmd.MeshBuffer->GetVertexBuffer()`.
- Keep shader selection and `BindVertexFactoryResources()` unchanged.
- Include ID-pick helper paths that bind mesh buffers directly, especially `Renderer.cpp::DrawIdPickCommand()`.

Target passes:

- `OpaqueRenderPass`
- `DepthPrePass`
- `ShadowPass`
- `SelectionMaskRenderPass`
- `TranslucentRenderPass`
- `DepthLessRenderPass`
- `MeshOverlayRenderPass` binding helper only. Do not apply Skin Cache override initially.
- `DecalRenderPass` if still mesh-buffer based
- `Renderer.cpp` legacy/default draw helpers that bind mesh buffers directly

Validation:

- CPU mode renders unchanged.
- GPU VS mode renders unchanged.
- ID picking and selection mask still match visible geometry.

### Phase 2: Vertex Source/RW Buffer Wrappers

Tasks:

- Add `FVertexSourceBuffer`.
- Add `FRWVertexBuffer`.
- Support D3D11 buffer creation for `FVertexSourceBuffer` with vertex buffer and shader resource bind flags.
- Support D3D11 buffer creation for `FRWVertexBuffer` with vertex buffer, shader resource, and unordered access bind flags.
- Add SRV creation for source buffers.
- Add SRV/UAV creation for deformed output buffers.
- Add memory stats reporting.
- Add explicit release and validity checks.
- Add a skeletal bind-pose-only creation path on `FMeshBuffer`, working name `CreateImmutableSourceVertexBuffer()`.
- Change `FMeshBufferManager::GetSkeletalBindPoseBuffer()` to use that path and create its vertex data as `FVertexSourceBuffer`.
- Do not change default `FMeshBuffer::CreateImmutableVertexBuffer()` behavior for ordinary static/procedural/editor meshes.
- Add a matching HLSL source vertex layout for `FSkeletalMeshVertex`, including packed `uint BoneIndicesPacked` unpacking.
- Keep CPU skinning dynamic VB and unrelated mesh/editor buffers on `FVertexBuffer`.
- Add `FDeformedVertex` layout documentation adjacent to the C++/HLSL declarations.
- Add Skin Cache vertex factory type and shader path entries as stubs if needed, so Phase 5 does not have to change pass binding and shader input contracts at the same time.

Decision:

- First implementation uses one interleaved `FDeformedVertex` output buffer.
- HLSL and C++ layout must be documented together.

Validation:

- GPU VS skinning still renders using the `FVertexSourceBuffer` as a normal VB.
- Compute pass can bind the same source buffer as SRV without duplicating source vertex memory.
- Static mesh, procedural mesh, editor gizmo, and CPU skinning dynamic buffers remain on the existing VB-only path.
- Standalone creation/release path succeeds.
- `stat memory` reports skeletal source buffer bytes separately from `GPU.SkinCache` output bytes.

### Phase 3: Skin Cache Manager

Tasks:

- Add `FSkinCacheEntry`.
- Add `FSkinCacheManager`.
- Use component UUID as first cache key.
- Only create/use entries for skeletal components whose effective method is compute cache.
- Track mesh pointer and vertex count to detect incompatible reuse.
- Allocate output `FRWVertexBuffer`.
- Reference skeletal bind-pose `FVertexSourceBuffer` SRV as source input.
- Expose a valid `FVertexStreamOverride` view for render commands.
- Report bytes/counts through GPU accounting.
- Add `FRenderer`-owned manager access path and pass a non-owning pointer through `FRenderPassContext`.
- Add a monotonic component-side deformation revision counter for compute dispatch skip. Existing dirty bools can stay for current CPU/VS behavior, but cache reuse needs an entry-visible revision value.

Dirty/recreate rules:

- Recreate when mesh pointer changes.
- Recreate when vertex count or output stride changes.
- Re-dispatch when deformation revision changes. The revision should increment on bone matrix update, animation pose change, mesh assignment, relevant morph state change, and `bUseSkinCache`/mode transitions that invalidate cached output.
- Invalidate or release on component/mesh destruction path when available.

Validation:

- Entries are reused while mesh and capacity match.
- Switching skeletal mesh recreates the entry.
- Memory stats update after create/release.

### Phase 4: Compute Shader And Compute Pass

Tasks:

- Add `Shaders/Compute/SkinCacheCS.hlsl`.
- Add shader path and load/compile registration.
- Add `FSkinCachePass`.
- Insert the pass before `DepthPrePass`.
- Dispatch visible skeletal entries whose global mode is `skinmode gpu` and whose component `bUseSkinCache` is true.
- Bind source vertex data, bone matrix SRV, and output UAV.
- Unbind UAV before draw passes consume the output as VB.
- Register dispatch/fallback stats so skipped dispatch, successful dispatch, allocation failure, and missing input SRV can be distinguished.

Initial shader scope:

- Skeletal skinning only.
- No morph target yet.
- No recompute tangents.
- No previous position.

Validation:

- Compute pass dispatch count is visible in stats/logs.
- Output buffer is updated before `DepthPrePass`.
- No D3D debug hazard from UAV still bound while drawing.

### Phase 5: Render Command Integration

Tasks:

- Keep user-facing `skinmode gpu` as the global GPU mode.
- Add component-level `bUseSkinCache` and expose it through editable/reflected properties.
- Add an effective method resolver for skeletal components:
  - global CPU mode returns CPU.
  - global GPU mode + `bUseSkinCache == false` returns GPU vertex shader.
  - global GPU mode + `bUseSkinCache == true` returns GPU compute cache if resources are valid.
  - allocation/input/shader failure returns explicit GPU vertex shader fallback with a visible reason.
- In skeletal command build:
  - CPU mode: existing CPU dynamic VB path.
  - GPU mode + component `bUseSkinCache == false`: existing bind-pose VB + bone matrix SRV path.
  - GPU mode + component `bUseSkinCache == true`: bind-pose/index source plus deformed vertex cache output override.
- Add or reuse a pass-through skeletal/deformed vertex shader entry that consumes already-deformed `FDeformedVertex`.
- Keep material pixel shader selection unchanged.
- Use the Skin Cache vertex factory/pass-through VS for compute-cache draws. Do not bind compute output through the existing `SkeletalMesh` input layout.
- Keep `MeshOverlayRenderPass`/bone-weight heatmap on the original skeletal VS path in the first implementation.
- Compute-cache components use the same conservative bounds policy as GPU VS skinning. Do not allow CPU/GPU/compute mode switches to change culling behavior unexpectedly.

Validation:

- `skinmode cpu` renders every skeletal component through CPU skinning.
- `skinmode gpu` renders `bUseSkinCache == false` components through VS skinning.
- `skinmode gpu` renders `bUseSkinCache == true` components through Compute Skin Cache.
- Base pass, depth prepass, shadow, selection mask, and ID picking agree.
- `Renderer.cpp::DrawIdPickCommand()` and editor ID-pick buffer paths are tested separately from normal selection mask rendering.
- Bone-weight heatmap still reads original bone weights and remains visually correct when the same component uses Skin Cache for main rendering.
- Compute-cache components use the same deformed output across multiple passes in the frame.

### Phase 6: Memory/Failure Reporting

Tasks:

- Add `stat memory` lines for Skin Cache:
  - total bytes
  - entry count
  - output VB bytes
  - source/bone buffer bytes if tracked separately
  - failed allocation/fallback count if implemented
- Add console/status output for current global skinning mode and per-component Skin Cache state where practical.
- Keep fallback explicit. If compute cache allocation fails, report it instead of silently pretending the user-selected mode succeeded.
- Ensure both viewport overlay memory output and debug/stat memory panel output show the same GPU category totals.

Validation:

- User can compare memory cost between CPU, GPU VS, and GPU Compute modes.
- User can identify which selected/visible skeletal components are using Skin Cache.
- Failure/fallback state is visible in console/stat UI.

### Phase 7: Morph Target Preparation

Tasks:

- Reserve manager/shader input concepts for morph delta buffer and morph weights.
- Document deformation order as `morph -> skinning`.
- Avoid naming payloads as skinning-only where morph will later fit.

Not in first implementation:

- Actual morph target compute application.
- Previous morph buffer.
- tangent recompute.

Validation:

- Adding morph input later should not require rewriting draw pass binding.
- Draw passes should continue to consume the same deformed vertex stream.

## File Impact Estimate

Likely touched files:

- `JSEngine/Source/Engine/Render/Resource/Buffer.h`
- `JSEngine/Source/Engine/Render/Resource/Buffer.cpp`
- `JSEngine/Source/Engine/Render/Resource/MeshBufferManager.h`
- `JSEngine/Source/Engine/Render/Resource/MeshBufferManager.cpp`
- `JSEngine/Source/Engine/Core/ResourceMemoryReporter.h`
- `JSEngine/Source/Engine/Core/ResourceMemoryReporter.cpp`
- `JSEngine/Source/Editor/UI/EditorViewportOverlayWidget.cpp`
- `JSEngine/Source/Engine/Render/Scene/RenderCommand.h`
- `JSEngine/Source/Engine/Render/Scene/PrimitiveDrawCommandBuilder.cpp`
- `JSEngine/Source/Engine/Render/Scene/EditorOverlayCollector.cpp`
- mesh draw pass files under `JSEngine/Source/Engine/Render/Renderer/RenderFlow/`
- `JSEngine/Source/Engine/Render/Renderer/RenderFlow/RenderPipeline.cpp`
- `JSEngine/Source/Engine/Render/Renderer/Renderer.h`
- `JSEngine/Source/Engine/Render/Renderer/Renderer.cpp`
- `JSEngine/Source/Engine/Render/Resource/ShaderPaths.h`
- `JSEngine/Source/Engine/Render/VertexFactory/VertexFactoryTypes.h`
- `JSEngine/Source/Engine/Render/Skinning/SkinningRuntimeSettings.h`
- `JSEngine/Source/Engine/Render/Renderer/RenderPassContext.h`
- `JSEngine/Source/Engine/Component/SkinnedMeshComponent.h`
- `JSEngine/Source/Engine/Component/SkinnedMeshComponent.cpp`
- `JSEngine/Source/Editor/UI/EditorConsoleWidget.cpp`
- new deformed vertex cache manager/pass files
- new compute shader file

Any new C++ files should be added through `GenerateProjectFiles.bat`.

## Resolved Naming Policy

- Feature/cache layer: `SkinCache`
- Manager/pass/key/entry: `FSkinCacheManager`, `FSkinCachePass`, `FSkinCacheKey`, `FSkinCacheEntry`
- Vertex factory: `EVertexFactoryType::SkinCacheSkeletalMesh`
- Compute shader: `SkinCacheCS.hlsl`
- Output vertex layout: `FDeformedVertex`

`SkinCache`라는 이름은 Unreal 참고성을 우선한 이름이다. 코드 주석에는 현재 GPU skinning 결과를 캐시하며 추후 morph target deformation까지 포괄한다는 의미를 짧게 남긴다.

## Recommended Implementation Order

1. Phase 0 GPU accounting.
2. Phase 1 draw binding helper with no behavior change.
3. Phase 2 `FVertexSourceBuffer` / `FRWVertexBuffer`.
4. Phase 3 cache manager with allocation/stat only.
5. Phase 4 compute pass and shader.
6. Phase 5 render command integration and mode enable.
7. Phase 6 stat/failure polish.
8. Phase 7 morph preparation notes and reserved fields.

Each phase should compile independently where possible. Phase 1 is especially important as a low-risk refactor before adding compute-cache behavior.
