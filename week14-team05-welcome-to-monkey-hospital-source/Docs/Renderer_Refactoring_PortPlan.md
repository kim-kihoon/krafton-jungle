# Renderer Refactoring Port Plan

## Context

Week12 `C:\Projects\Jungle_Week12_Team4`의 `refactor: Renderer Refactoring #1` ~ `#4`는 `UberLit`을 장면 렌더링용 main opaque shader로 좁히고, Transparent, decal, debug viewmode, 공통 shader helper 책임을 분리한 연속 작업이다.

Week13 `KraftonEngine`은 이미 material/domain 개편, `Transparent`, `Decal`, `AdditiveDecal`, `Fog`, DoF pass chain이 들어와 있어 Week12 커밋을 그대로 cherry-pick할 수 없다. 이 문서는 각 커밋의 의도를 현재 구조에 맞게 이식하는 계획이다.

## Source Commits

| Commit | Week12 Summary | Week13 Port Policy |
|---|---|---|
| `ccd1a6c` Renderer Refactoring #1 | Transparent pass 활성화, `UberTransparent.hlsl`, material blend/depth 정책 정리 | pass/material state 의도는 이미 반영됨. 이번 작업에서는 `UberLit`에 남은 Transparent self-fog permutation만 `UberTransparent.hlsl`로 분리 |
| `486fb39` Renderer Refactoring #2 | Decal을 `UberLit.hlsl`에서 분리하고 `DecalRenderPass`로 이동 | pass/shader 분리는 이미 반영됨. 이번 작업에서는 final cleanup 시 `UberLit`로의 역결합이 없는지만 제거/검증 |
| `65b60c9` Renderer Refactoring #3 | Debug viewmode를 `UberLit`에서 분리, `ViewModeMesh`/`DebugViewModeResolve` 추가, old MRT 제거 | 이번 포팅의 핵심. Week13의 `WorldNormal`, `LightCulling`, `SceneDepth`, `WeightBoneHeatMap` 정책에 맞게 재설계. `DoFCoC`는 기존 DoF path 유지 |
| `aa3110b` Renderer Refactoring #4 | `UberLit`/`UberTransparent`/`Decal` 공통 helper 추출 | #3 이후 적용. 현재 `ForwardLighting.hlsli`, `ShadowSampling.hlsli`와 중복되지 않게 normal mapping/light lookup/shadow helper를 정리 |

## Current Week13 Snapshot

- `ERenderPass` 실행 순서는 enum 순서로 결정된다. 현재 주요 순서는 `PreDepth -> LightCulling -> ShadowMap -> Opaque -> Decal -> AdditiveDecal -> ViewModeMesh -> Fog -> DoFSetup -> Transparent -> DoFBackgroundBlur -> DoFForegroundBlur -> DoFBokehScatter -> DoF -> DebugViewModeResolve -> SelectionMask -> EditorLines -> PostProcess -> FXAA -> EditorIcon -> GizmoOuter -> GizmoInner -> OverlayFont -> UI -> GammaCorrection`.
- `FOpaquePass`는 `SceneColor` 단일 render target만 바인딩한다. old `NormalRTV`/`CullingHeatmapRTV` MRT path는 제거됐다.
- `UberLit.hlsl`은 main opaque scene shading용 single `SV_TARGET` shader다. `WEIGHT_BONE_HEATMAP`, Transparent `FORWARD_FOG`, debug MRT output 책임은 더 이상 갖지 않는다.
- `WorldNormal`과 `LightCulling`은 `ViewModeMesh`가 opaque surface mesh를 직접 debug output으로 렌더한다. `SceneNormal.hlsl`, `LightCulling.hlsl`, `NormalSRV`, `CullingHeatmapSRV` fallback은 남기지 않는다.
- `SceneDepth`는 `DebugViewModeResolve` fullscreen pass가 depth snapshot을 시각화한다. `DoFCoC`는 기존 DoF pass chain 기반 debug path를 유지한다.
- Transparent scene shading은 `UberTransparent.hlsl` 또는 graph/generated Transparent shader가 담당한다.

## Resolved Audit Result

Phase 0 성격의 조사는 이 문서 작성 시점에 완료된 것으로 본다. 작업자는 별도 audit phase를 수행하지 않고 Phase 1 코드 수정부터 시작한다.

### Transparent

- `EMaterialDomain::Surface` + `EBlendMode::Transparent/Additive/Modulate`는 이미 `ERenderPass::Transparent`로 라우팅된다.
- Transparent material state는 `AlphaBlend/Additive/Modulate`, `DepthReadOnly`, `SolidNoCull`로 도출된다.
- `ResolveSectionShader`는 `SecPass == ERenderPass::Transparent`일 때 `UberTransparent.hlsl` 또는 graph/generated Transparent shader를 선택한다.
- Transparent self-fog는 `UberTransparent.hlsl` 및 graph/generated Transparent shader 경로가 담당한다.
- graph/generated Transparent surface shader는 single-target output이며 `ApplyFogTransparent`를 직접 호출한다. `UberTransparent.hlsl`로 강제 통합하지 않고 generated path를 유지한다.

### Decal

- `EMaterialDomain::Decal`은 `Decal` 또는 `AdditiveDecal` pass로 라우팅된다.
- `FDecalPass`/`FAdditiveDecalPass`는 `DepthReadOnly`, `SolidNoCull`, `AlphaBlend`/`Additive` 상태를 가진다.
- `Shaders/Geometry/Decal.hlsl`은 독립 shader이며 `UberLit.hlsl`을 include하지 않는다. 필요한 lighting은 `ForwardLighting.hlsli`를 통해 공유한다.
- decal draw는 decal proxy 자체 mesh를 그리는 구조가 아니라, `BuildDecalCommands`가 receiver proxy를 순회하며 receiver sections를 decal shader로 다시 그리는 구조다. 이번 작업에서 이 receiver redraw 구조는 변경하지 않는다.
- Phase 8은 별도 설계 조사가 아니라, final cleanup 전후에 `UberLit`로 decal-specific branch/macro/texture binding이 되돌아가지 않았는지 확인하는 cleanup checkpoint다.

### Debug Viewmode Ownership

- `WorldNormal`: `ViewModeMesh` replace path가 opaque surface mesh를 직접 debug color로 렌더한다.
- `LightCulling`: `ViewModeMesh` replace path가 tile/cluster light-count heatmap을 직접 출력한다.
- `SceneDepth`: `DebugViewModeResolve` fullscreen pass가 depth snapshot을 시각화한다.
- `DoFCoC`: `DoFSetup` + `DoF` debug composite를 유지한다.
- `bWeightBoneHeatMap`: `ViewModeMesh` overlay path가 scene shading 위에 selected bone heatmap을 alpha blend한다.

### UberLit Responsibilities Removed

- `Normal : SV_TARGET1`, `Culling : SV_TARGET2`
- `WEIGHT_BONE_HEATMAP` macro permutations and selected-bone constant binding
- `FORWARD_FOG` Transparent self-fog branch
- pure debug viewmode shader selection

### Graph Material Generator Scope

- opaque generated surface shader는 single color target을 출력한다.
- generated Transparent surface shader도 `SV_TARGET` 단일 output을 유지한다.
- pure debug viewmode에서는 graph/custom surface shader보다 `ViewModeMesh`를 우선한다. graph material 자체에 debug output permutation을 추가하지 않는다.

## Goals

- `UberLit.hlsl`을 main opaque scene shading shader로 재정의한다.
- Debug viewmode용 mesh shading과 fullscreen resolve를 `UberLit`/`OpaquePass`에서 분리한다.
- Transparent scene shader 책임을 `UberTransparent.hlsl` 별도 shader path로 분리한다.
- Decal shader/pass가 `UberLit`에 다시 기대지 않도록 잔여 coupling을 제거한다.
- 중복 shader helper를 공통 파일로 옮기되 `ForwardLighting.hlsli`, `ShadowSampling.hlsli` 같은 기존 공통 파일과 충돌하지 않게 한다.
- DoF/Fog/Overlay pass order와 debug viewmode skip 정책을 명확히 한다.

## No Goals

- DoF 알고리즘 품질이나 CoC encoding 정책을 바꾸지 않는다.
- `DoF` 토글 off + `DoFCoC` viewmode에서 viewport가 검게 나오는 현재 제한을 이번 작업에서 고치지 않는다.
- Material domain/blend serialization 구조를 다시 갈아엎지 않는다.
- Deferred renderer나 GBuffer 기반 전체 구조로 전환하지 않는다.
- Debug viewmode 작업 범위를 넘어 editor overlay 시스템 전체를 재설계하지 않는다.
- Week12 asset files 또는 `.mat`/`.matinst` 포맷을 가져오지 않는다.

## ViewMode Policy

| ViewMode | Category | Target Path |
|---|---|---|
| `Lit_Phong`, `Lit_Gouraud`, `Lit_Lambert` | Scene view | `Opaque`/`Transparent` scene shader path |
| `Unlit` | Scene view | scene shader permutation, debug path 아님 |
| `Wireframe` | Scene view override | 기존 material pass + rasterizer/state override 유지 |
| `WorldNormal` | Pure mesh debug | `ViewModeMesh`가 opaque surface mesh를 직접 debug color로 렌더. 기존 `NormalRTV/NormalSRV` 의존은 남기지 않음 |
| `LightCulling` | Pure mesh debug | `ViewModeMesh`가 opaque surface mesh에서 light-count heatmap을 직접 출력. old `CullingHeatmapRTV` fullscreen debug 의존은 남기지 않음 |
| `SceneDepth` | Pure fullscreen debug | `DebugViewModeResolve`가 depth copy를 시각화 |
| `DoFCoC` | Existing DoF debug | 이번 작업에서 제외. 기존 DoF setup + `DoF` debug composite 방식을 유지하고 `DebugViewModeResolve`로 합치지 않음 |
| `bWeightBoneHeatMap` | Mesh overlay debug | scene view의 opaque draw를 유지하고 `ViewModeMesh`에서 skeletal overlay draw를 추가. 기존 mesh shading 위에 heatmap을 alpha blend, 기본 alpha 0.8 |

## Target Pass Layout

1차 목표는 기존 DoF/overlay 순서를 보존하면서 debug view pass만 삽입하는 것이다.

```text
PreDepth
LightCulling
ShadowMap
Opaque
Decal
AdditiveDecal
ViewModeMesh
Fog
DoFSetup
Transparent
DoFBackgroundBlur
DoFForegroundBlur
DoFBokehScatter
DoF
DebugViewModeResolve
SelectionMask
EditorLines
PostProcess
FXAA
EditorIcon
GizmoOuter
GizmoInner
OverlayFont
UI
GammaCorrection
```

Notes:

- `ViewModeMesh`는 pure mesh debug modes와 bone heatmap overlay에서만 draw command를 가진다.
- 장기적으로 replace debug pass와 overlay debug pass를 분리하는 것이 더 깔끔하지만, 이번 작업에서는 `ViewModeMesh` 하나 안에서 mode별 replace/overlay 정책을 분기한다.
- `DebugViewModeResolve`는 pure fullscreen debug modes에서만 draw command를 가진다.
- Scene view에서는 두 pass가 command 없이 통과해야 한다.
- `Fog -> DoFSetup -> Transparent -> DoF*` 순서는 기존 DoF 문서의 정책을 보존한다.
- Pure debug viewmode에서는 fog/DoF/FXAA/gamma/camera fade/vignette/letterbox 같은 non-overlay post-processing 계통을 스킵한다.
- Pure debug viewmode에서도 selection outline과 `GizmoOuter`/`GizmoInner`는 유지한다.
- Outline은 현재 `PostProcess` pass command이므로, pure debug에서 `PostProcess` pass 자체를 무조건 제거하지 않고 outline command만 허용하는 식으로 처리한다.
- Pure debug viewmode에서는 grid/editor lines/editor icon/overlay font/UI를 스킵하고 gizmo만 유지한다.
- Transparent surface는 pure debug view에 참여하지 않는다.

## Phase Boundary Policy

- Phase는 가능한 한 하나의 observable behavior checkpoint만 책임진다.
- Phase 0 조사는 완료된 상태로 문서화되어 있으며 구현 phase로 남기지 않는다.
- Phase 1만 예외적으로 build-only skeleton phase로 둔다. 새 pass/file/project membership을 먼저 안정화해야 이후 Phase가 behavior 단위로 검증 가능해지기 때문이다.
- Phase 2.5는 Phase 2 작업 중 드러난 기즈모 pass 불안정을 먼저 고정하는 삽입 phase다. 이후 debug viewmode phase는 기즈모가 `ViewModeMesh`/fullscreen debug/post-process 경로에 오염되지 않는다는 전제 위에서 진행한다.
- MRT cleanup은 `WorldNormal`, `LightCulling`, `SceneDepth`, bone heatmap의 새 output owner가 모두 검증된 뒤에 수행한다.

## Phase 1: Shader Path and RenderPass Skeleton

### Goal

새 debug path를 추가할 수 있는 최소 골격을 만든다.

### Tasks

1. `ERenderPass`에 `ViewModeMesh`, `DebugViewModeResolve`를 추가한다.
2. `GetRenderPassName`, `RenderStateStrings::RenderPassMap`에 새 pass를 등록한다.
3. `FViewModeMeshPass`, `FDebugViewModeResolvePass`를 `Render/RenderPass`에 추가하고 `REGISTER_RENDER_PASS`로 등록한다.
4. `ShaderManager.h`에 debug shader paths를 추가한다.
   - `Shaders/Editor/ViewModeMesh.hlsl` 또는 `Shaders/EditorDebug/ViewModeMesh.hlsl`
   - `Shaders/PostProcess/DebugViewModeResolve.hlsl`
5. `.vcxproj`와 `.filters`에 새 C++/HLSL 파일을 등록한다.

### Validation

- 새 pass가 scene view에서 아무 command도 실행하지 않는 상태로 빌드된다.
- `cmd /c "ReleaseBuild.bat < NUL"`

## Phase 2: WorldNormal ViewModeMesh Path

### Goal

`WorldNormal`을 기존 `NormalRTV/NormalSRV + SceneNormal.hlsl` fullscreen path에서 `ViewModeMesh` replace path로 옮긴다.

### Tasks

1. `DrawCommandBuilder`에 viewmode classification helper를 추가한다.
   - `IsSceneViewMode`
   - `IsPureMeshDebugViewMode`
   - `IsFullscreenDebugViewMode`
   - `IsBoneHeatmapOverlayEnabled`
2. `ViewModeMesh.hlsl`의 `WorldNormal` 출력을 구현한다.
   - static mesh와 skeletal mesh entry point를 모두 지원한다.
   - normal map이 있는 material은 tangent-space normal perturbation을 적용한다.
3. `WorldNormal` viewmode에서 opaque surface section만 `ViewModeMesh` command를 생성한다.
   - 기존 `Opaque` scene shading command는 생성하지 않는다.
   - Transparent surface는 참여하지 않는다.
   - graph/generated custom shader도 material shader 대신 `ViewModeMesh`를 사용한다.
4. Pure debug 공통 정책을 이 Phase에서 먼저 적용한다.
   - non-overlay post-processing 계통 command 생성을 막는다.
   - selection outline과 `GizmoOuter`/`GizmoInner`는 유지한다.
   - outline은 현재 `PostProcess` command로 생성되므로 `PostProcess` pass를 전역 차단하지 않는다.
   - grid/editor lines/editor icon/overlay font/UI는 스킵한다.
5. 기존 `WorldNormal` postprocess command 생성을 제거한다.
   - `NormalRTV/NormalSRV` 리소스 자체는 Phase 6 cleanup 전까지 남겨도 된다.

### Validation

- `WorldNormal`이 geometry 정상 윤곽으로 표시된다.
- `WorldNormal`에서 Transparent surface는 표시되지 않는다.
- graph/generated custom shader surface도 `WorldNormal`에서 동일한 debug output을 낸다.
- non-overlay post-processing 계통은 debug output을 덮지 않는다.
- selection outline과 gizmo는 유지된다.
- grid/editor lines/editor icon/overlay font/UI는 표시되지 않는다.
- `Lit_Phong`, `Unlit`, `Wireframe`은 기존처럼 표시된다.

## Phase 2.5: Gizmo Pass Stabilization

### Goal

기즈모를 debug viewmode replacement와 post-process 영향에서 분리하고, 모든 viewmode에서 기존 R/G/B 축 색상과 조작 가능 상태를 안정화한다.

### Tasks

1. `GizmoOuter`/`GizmoInner` section을 일반 mesh routing과 별도 경로로 감지한다.
2. 기즈모 프록시는 `PreDepth`, `Opaque`, `ViewModeMesh` command를 만들지 않고 자기 pass command만 생성한다.
3. pure debug viewmode에서도 기즈모는 `ViewModeMesh` shader를 타지 않고 `Gizmo.hlsl`과 `FGizmoConstants`를 그대로 사용한다.
4. `WorldNormal`, `SceneDepth`, `LightCulling` 같은 pure debug viewmode에서 grid/editor lines/editor icon/overlay font/UI는 계속 스킵하되, 기즈모만 overlay로 유지한다.
5. 이후 phase에서 `ViewModeMesh` 대상은 static/skeletal opaque surface로 한정한다.

### Validation

- `Lit_Phong`, `Unlit`, `Wireframe`에서 기즈모가 보이고 축 색상이 기존 R/G/B로 유지된다.
- `WorldNormal`에서 기즈모가 normal encoding 색(`7fff7f`, `ff7f7f`, `7f7fff`)으로 바뀌지 않고 기존 R/G/B로 보인다.
- `SceneDepth`, `LightCulling`에서 기즈모가 debug fullscreen/post-process 결과에 묻히지 않는다.
- 보이는 기즈모의 hover/drag/selected axis 동작이 기존처럼 작동한다.
- `cmd /c "ReleaseBuild.bat < NUL"`

### Follow-up Invariants

- `FGizmoSceneProxy`가 직접 보유한 transient material/object는 scene proxy GC reference collection에 명시적으로 포함되어야 한다.
- 이후 phase에서 `IsPureMeshDebugViewMode`나 `IsFullscreenDebugViewMode` 범위를 넓혀도 `GizmoOuter`/`GizmoInner` section은 `ViewModeMesh`, `DebugViewModeResolve`, generic `PostProcess` command로 라우팅하지 않는다.
- gizmo가 mesh 뒤에 파묻히지 않게 하는 현재 `GizmoOuter` + `GizmoInner` depth policy는 debug viewmode refactor 범위에서 변경하지 않는다.

## Phase 3: LightCulling ViewModeMesh Path

### Goal

`LightCulling` heatmap 생산자를 `UberLit` MRT에서 `ViewModeMesh`로 옮긴다.

### Tasks

1. `ViewModeMesh.hlsl`에 tile/cluster light-count heatmap 계산을 추가한다.
2. `LightCulling` viewmode에서 opaque surface section만 `ViewModeMesh` command를 생성한다.
   - 기존 `Opaque` scene shading command는 생성하지 않는다.
   - Transparent surface는 참여하지 않는다.
   - graph/generated custom shader도 material shader 대신 `ViewModeMesh`를 사용한다.
3. 기존 fullscreen `LightCulling.hlsl` postprocess command 생성을 제거한다.
   - `CullingHeatmapRTV/CullingHeatmapSRV` 리소스 자체는 Phase 6 cleanup 전까지 남겨도 된다.
4. Phase 2에서 만든 pure debug 공통 정책을 `LightCulling`에도 그대로 적용한다.
5. Phase 2.5의 gizmo routing invariant를 유지한다.
   - `LightCulling`을 `IsPureMeshDebugViewMode`에 추가해도 gizmo proxy는 `ViewModeMesh` command를 만들지 않는다.
   - gizmo pass는 기존 `Gizmo.hlsl`/`FGizmoConstants`와 `GizmoOuter`/`GizmoInner` depth policy를 유지한다.

### Validation

- `LightCulling`이 mesh 표면에서 기존 heatmap과 동등한 의미로 표시된다.
- `LightCulling`에서 Transparent surface는 표시되지 않는다.
- non-overlay post-processing 계통은 debug output을 덮지 않는다.
- selection outline과 gizmo는 유지된다.
- grid/editor lines/editor icon/overlay font/UI는 표시되지 않는다.
- `WorldNormal`과 기존 scene view가 회귀하지 않는다.

## Phase 4: Bone Heatmap Overlay

### Goal

`bWeightBoneHeatMap`을 scene replacement가 아니라 opaque scene 위의 `ViewModeMesh` overlay로 분리한다.

### Tasks

1. `bWeightBoneHeatMap`이 켜진 skeletal mesh는 기존 `Opaque` command를 유지한다.
2. 같은 skeletal mesh section에 대해 `ViewModeMesh` overlay command를 추가 생성한다.
3. `ViewModeMesh` pass는 overlay mode에서 `DepthReadOnly + AlphaBlend` 상태를 사용한다.
4. `ViewModeMesh.hlsl`에 selected bone weight heatmap 계산을 추가한다.
5. overlay alpha를 `FViewportRenderOptions`에 `WeightBoneHeatMapOverlayAlpha`로 추가한다.
   - 기본값은 0.8이다.
   - Mesh Editor debug UI에서 조절 가능한 slider를 연결한다.
6. `bWeightBoneHeatMap`이 기존 `Opaque` command의 scene shader selection이나 skinning mode를 바꾸지 않도록 분리한다.
   - selected-bone constant buffer와 GPU skinning force logic은 `ViewModeMesh` overlay command 쪽으로만 이동한다.
7. `UberLit`의 `WEIGHT_BONE_HEATMAP` define과 selected-bone payload를 제거한다.
   - `ShaderManager`의 `*WeightBoneHeatMap` macro permutations는 제거한다.
   - ViewModeMesh의 static/skeletal 차이는 `ViewModeMesh` shader entry 또는 permutation에서 처리한다.

### Validation

- `bWeightBoneHeatMap`에서 opaque material shading이 먼저 보이고, bone heatmap이 alpha 0.8 기본값으로 위에 덧그려진다.
- Mesh Editor 선택 bone index가 overlay에 반영된다.
- `WorldNormal`, `LightCulling`, scene view가 회귀하지 않는다.
- skeletal GPU skinning path와 CPU fallback path가 둘 다 빌드된다.

## Phase 5: SceneDepth DebugViewModeResolve Path

### Goal

`SceneDepth` fullscreen debug를 generic `PostProcess` path에서 `DebugViewModeResolve`로 분리한다.

### Tasks

1. `DebugViewModeResolve.hlsl`에 `SceneDepth` 시각화를 구현한다.
2. `SceneDepth` viewmode에서 `DebugViewModeResolve` command를 생성한다.
3. depth copy timing을 명시한다.
   - 현재 `PreDepthPass`, `FogPass`, `PostProcessPass`, `DoFSetupPass`가 depth copy를 수행한다.
   - `FDebugViewModeResolvePass::BeginPass`가 실행 시점의 `Frame.DepthTexture`를 `Frame.DepthCopyTexture`로 복사한다.
   - `SceneDepth`는 `DebugViewModeResolve` 실행 직전의 scene depth snapshot을 시각화한다.
4. 기존 `PostProcess`의 `SceneDepth` command 생성을 제거한다.
5. `DoFCoC`는 이번 작업에서 제외한다.
   - 기존 DoF setup + `DoF` debug composite 방식을 유지한다.
   - `DoF` 토글이 꺼져 있고 viewmode만 `DoFCoC`인 경우 viewport가 검게 나오는 현재 동작은 known limitation으로 둔다.
6. Phase 2.5의 gizmo routing invariant를 유지한다.
   - `SceneDepth`가 fullscreen resolve로 바뀌어도 gizmo는 resolve 대상이 아니라 이후 overlay pass로만 렌더링한다.

### Validation

- `SceneDepth` viewmode가 기존 near/far/exponent 설정을 유지한다.
- Pure debug viewmode에서 non-overlay post-processing 계통이 debug output을 덮지 않는다.
- selection outline과 gizmo는 유지된다.
- grid/editor lines/editor icon/overlay font/UI는 표시되지 않는다.
- `DoFCoC` 동작은 이 Phase의 검증 대상이 아니다.

## Phase 6: UberLit Color-Only and MRT Cleanup

### Goal

`UberLit.hlsl`과 opaque graph material generator를 단일 color target으로 정리하고, obsolete MRT resource path를 제거한다.

### Tasks

1. `UberPS_Output`을 단일 color target으로 축소한다.
2. opaque graph material generator도 단일 color target으로 축소한다.
   - `MaterialSurfacePSOutput`의 `Normal : SV_TARGET1`, `Culling : SV_TARGET2`를 제거한다.
   - generated Transparent surface path는 이미 single-target이므로 회귀 확인만 한다.
3. `FOpaquePass`에서 `NormalRTV`, `CullingHeatmapRTV` MRT 바인딩을 제거한다.
4. `SceneNormal.hlsl`, `LightCulling.hlsl`, `NormalRTV`, `NormalSRV`, `CullingHeatmapRTV`, `CullingHeatmapSRV` 의존을 제거한다.
   - `FViewport`
   - `FFrameContext`
   - `RenderConstants` / `SystemResources.hlsli`
   - shader preload / project file membership
   - 단, tile/cluster light culling compute buffer와 `ForwardLightData`는 scene lighting과 `ViewModeMesh` heatmap 계산에 계속 필요하므로 제거 대상이 아니다.
5. `SelectEffectiveShader` / `ResolveSectionShader`에서 scene shader와 debug shader selection을 분리된 구조로 유지한다.
   - pure debug view에서는 graph/custom surface shader보다 `ViewModeMesh`를 우선한다.
   - Transparent surface는 pure debug view routing 대상에서 제외한다.
6. `Wireframe`은 기존 `ApplyWireframe` path를 유지한다.

### Validation

- Lit/Unlit/Gouraud/Lambert/Phong scene view가 기존처럼 표시된다.
- Wireframe 색상과 rasterizer wireframe이 유지된다.
- `WorldNormal`, `LightCulling`, `SceneDepth`, bone heatmap overlay가 계속 동작한다.
- graph/generated opaque material이 정상 렌더링된다.
- D3D debug layer에서 MRT/SRV binding warning이 없어야 한다.

## Phase 7: Transparent Shader Split

### Goal

Week12 #1의 `UberTransparent` 의도를 Week13의 material/domain 구조에 맞게 반영한다.

### Tasks

1. `Shaders/Geometry/UberTransparent.hlsl`을 추가한다.
2. non-graph surface Transparent material은 `UberTransparent.hlsl`을 사용한다.
3. graph/generated Transparent surface material은 generated single-target shader path를 유지한다.
4. Transparent-only `FORWARD_FOG` path를 `UberLit`에서 이동한다.
5. `ResolveSectionShader`가 `SecPass == ERenderPass::Transparent`일 때 Transparent shader permutation을 선택하도록 한다.
6. Transparent shader가 opaque-only MRT/debug output을 갖지 않도록 한다.
7. particle sprite/mesh/beam/ribbon shader는 기존 vertex-factory 전용 path를 유지한다.

### Validation

- Surface Transparent material이 `Transparent` pass에서 정상 blend/depth-read-only로 표시된다.
- Fog 위에 그려지는 Transparent self-fog가 기존과 동등하다.
- Particle Transparent path가 회귀하지 않는다.

## Phase 8: Decal Coupling Cleanup

### Goal

Week12 #2 의도처럼 decal이 `UberLit` 내부 책임으로 되돌아가지 않게 한다.

### Tasks

1. `Shaders/Geometry/Decal.hlsl`은 `UberLit.hlsl`을 include하지 않는 독립 shader로 유지한다.
2. `DecalPass`, `AdditiveDecalPass`의 현재 depth/blend/raster state를 변경하지 않는다.
3. decal proxy가 receiver sections를 decal shader로 다시 그리는 현재 receiver redraw 구조를 유지한다.
4. `UberLit`에 decal-specific branch, macro, texture binding이 남아 있으면 제거한다.

### Validation

- Decal/AdditiveDecal scene이 기존처럼 표시된다.
- Opaque material shader permutation에서 decal-only define이 없어야 한다.

## Phase 8.5: Material Graph Decal Stability

### Goal

Graph material 저장/로드/컴파일이 decal domain에서 깨지지 않게 하고, AdditiveDecal 반복 테스트가 가능한 상태를 만든다.

### Tasks

1. material graph node/pin schema를 node type과 domain 기준으로 복구할 수 있는 repair path를 추가한다.
   - 기존 pin id는 보존해서 링크를 유지한다.
   - 손상된 `FName`/display name만 semantic name으로 되돌린다.
2. material load, editor open, save, preview compile, runtime compile 경로가 모두 graph pin repair를 지나게 한다.
3. Decal domain graph는 Surface와 같은 `BaseColor/Normal/Roughness/Metallic/Emissive/Opacity/OpacityMask` output authoring surface를 유지한다.
4. compile target은 material domain을 우선한다.
   - `Domain=Decal`이면 document target이 stale이어도 generated Decal shader를 사용한다.
   - `Domain=PostProcess`도 같은 방식으로 PostProcess target을 우선한다.
5. Decal proxy는 source material의 stale Surface pass/state를 복제하지 않고 Decal component 기준 pass/state로 라우팅한다.

### Validation

- material 저장 후 에디터를 재시작해도 Material Output pin 이름이 semantic name으로 복구된다.
- Texture Sample, Texture Object, parameter/math node pin 이름도 semantic name으로 복구된다.
- 재시작 후 Compile/Save를 눌러도 material이 검게 망가지지 않는다.
- AdditiveDecal graph material이 receiver 전체를 덮지 않고 decal volume 안에만 투영된다.
- DefaultDecal은 기존처럼 정상 표시된다.

## Phase 9: Common Shader Helper Extraction

### Goal

Week12 #4처럼 scene shader 간 중복을 줄이되, Week13의 기존 common shader layout과 충돌하지 않게 정리한다.

### Tasks

1. 현재 common shader 역할을 기준으로 중복을 분류한다.
   - `ForwardLighting.hlsli`: light accumulation, culling heatmap
   - `ShadowSampling.hlsli`: shadow sampling
   - `Skinning.hlsli`: skeletal skinning
   - `Fog.hlsli`: fog integral
2. 새 helper를 작은 단위로 추가한다.
   - `NormalMapping.hlsli`: tangent basis + normal texture perturbation
   - `LightCullingDebug.hlsli`: debug heatmap only, scene lighting과 분리
3. `UberLit`, `UberTransparent`, `Decal`, `ViewModeMesh`가 같은 helper를 include하게 정리한다.
4. helper extraction 후 shader compile path와 include dependency tracking을 검증한다.

### Validation

- `cmd /c "ReleaseBuild.bat < NUL"`
- shader hot reload 또는 startup shader compile에서 include resolution error가 없어야 한다.

## Phase 10: Final Cleanup

### Goal

Old path와 이름 혼선을 제거한다.

### Tasks

1. `rg`로 잔여 debug coupling 제거를 검증한다.
   - `WEIGHT_BONE_HEATMAP` in `UberLit`
   - `SV_TARGET1` / `SV_TARGET2` in `UberLit`
   - `SceneNormal` / `CullingHeatmap` as required opaque outputs
   - old debug fullscreen command in `PostProcess`
2. 불필요해진 `NormalRTV/NormalSRV/CullingHeatmapRTV/CullingHeatmapSRV`를 `FViewport`, `FFrameContext`, pass begin/end에서 제거한다.
3. comments가 새 responsibility와 맞는지 정리한다.
4. project files를 갱신한다.

### Validation

- `git diff --check`
- `cmd /c "ReleaseBuild.bat < NUL"`
- Runtime smoke:
  - `Lit_Phong`, `Unlit`, `Wireframe`
  - `WorldNormal`, `SceneDepth`, `LightCulling`
  - `DoFCoC`는 회귀 확인만 한다. `DoF` on 상태의 기존 debug composite를 유지하고, `DoF` off + `DoFCoC` black viewport는 accepted known limitation으로 둔다.
  - opaque/decal/Transparent/fog/DoF on/off
  - Mesh Editor bone heatmap

## Risk Notes

- `LightCulling` fullscreen debug와 `UberLit` MRT 의존은 제거됐다. Light culling compute buffer와 `ForwardLightData`는 scene lighting 및 `ViewModeMesh` heatmap 계산에 계속 필요하다.
- `WorldNormal`은 `NormalRTV` fallback을 남기지 않는다. `SceneNormal.hlsl` fullscreen path와 `NormalRTV/NormalSRV`는 제거된 상태를 유지한다.
- `DoFCoC`는 이미 DoF pass chain과 연결되어 있으므로 이번 작업에서 제외한다. `DoF` 토글이 꺼져 있고 viewmode만 `DoFCoC`인 경우 검게 나오는 현재 동작은 당분간 유지한다.
- `Wireframe`은 Week12에서도 debug path로 옮기지 않았다. 이번에도 scene material path에 남기는 것이 낮은 리스크다.
- Transparent self-fog는 `UberTransparent.hlsl` 및 graph/generated Transparent shader 경로가 담당한다. `UberLit`에 `FORWARD_FOG` 책임을 되돌리지 않는다.
- 새 HLSL/C++ 파일은 `.vcxproj`와 `.filters`에 등록해야 한다. 빌드 스크립트가 project membership에 의존한다.

## Implementation Order Recommendation

1. Phase 1에서 build-only skeleton을 먼저 만든다.
2. Phase 2에서 `WorldNormal`과 pure debug 공통 skip/outline/gizmo 정책을 검증한다.
3. Phase 2.5에서 기즈모 pass를 debug replacement/post-process와 분리해 안정화한다.
4. Phase 3에서 `LightCulling` 생산자를 `ViewModeMesh`로 옮긴다.
5. Phase 4에서 bone heatmap을 replacement가 아닌 overlay behavior로 검증한다.
6. Phase 5에서 `SceneDepth` fullscreen debug만 `DebugViewModeResolve`로 옮긴다. `DoFCoC`는 제외한다.
7. Phase 6에서 모든 old MRT consumer가 사라진 뒤 `UberLit`/graph material generator/opaque MRT를 정리한다.
8. Phase 7에서 Transparent shader split을 수행한다.
9. Phase 8~10으로 decal cleanup, shader helper, final cleanup을 마무리한다.

이 순서가 좋은 이유는 `UberLit`을 먼저 지우지 않고, 새 output owner를 먼저 만든 다음 old responsibility를 제거하기 때문이다. 각 checkpoint에서 기존 scene view를 유지하면서 debug path만 하나씩 옮길 수 있다.
