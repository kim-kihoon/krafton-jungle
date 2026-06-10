# Render Pass / DoF Refactoring Plan

## Context

DoF 품질 개선 과정에서 렌더 패스 구조 변경 요구가 늘어나고 있다. 이 문서는 DoF 알고리즘 자체가 아니라 render pass ordering, pass enum, pass resource hazard, editor overlay 정책을 별도로 추적한다.

Opaque DoF 알고리즘과 CoC 정책은 `Docs/DoF_Opaque_ImplementationPlan.md`에서 관리한다.

## Goals

- DoF 관련 pass가 명확한 순서로 실행되도록 render pass 구조를 정리한다.
- SceneColor, depth, CoC, intermediate blur RT의 SRV/RTV hazard 규칙을 패스 단위로 명시한다.
- Editor overlay, gizmo, UI가 DoF 전후 어디에 위치해야 하는지 정책을 확정한다.
- Phase 4 이후 작업을 opaque DoF 문서에서 분리해 후속 render-pass/refactoring 작업으로 관리한다.

## No Goals

- 이 문서에서 DoF blur kernel 품질을 결정하지 않는다.
- 이 문서에서 opaque CoC encoding, bokeh 품질을 결정하지 않는다.
- 전체 renderer architecture를 교체하지 않는다.

## Target Pass Layout

초기 목표는 다음 구조다.

```text
PreDepth
LightCulling
ShadowMap
Opaque
Decal
AdditiveDecal
Fog
DoFSetup
Translucent
DoFBackgroundBlur
DoFForegroundBlur
DoFComposite
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

이 배치는 정책 초안이다. 실제 적용 시 기존 editor line/gizmo가 scene geometry로 취급되어야 하는지, overlay로 취급되어야 하는지 확인 후 조정한다.

## Phase R1: Pass Boundary Audit

### Tasks

1. 현재 `ERenderPass` 순서와 실제 visual order를 문서화한다.
2. 각 pass가 읽는 SRV와 쓰는 RTV/DSV를 표로 정리한다.
3. `DoFSetup`, `DoF`, `FXAA`, `GammaCorrection`, `PostProcess`의 SceneColor copy 정책을 비교한다.
4. `EditorLines`, `SelectionMask`, `GizmoOuter`, `GizmoInner`, `EditorIcon`, `OverlayFont`, `UI`가 DoF 전/후 어디에 있어야 하는지 정책 후보를 정리한다.

### Validation

- 문서만 변경한다.
- 기존 빌드 영향 없음.

## Phase R2: DoF Intermediate Pass Integration

### Tasks

1. foreground/background split에 필요한 pass enum을 추가한다.
   - 후보: `DoFBackgroundBlur`, `DoFForegroundBlur`, `DoFComposite`
2. DoF pass들이 같은 SceneColor/CoC/intermediate RT를 SRV와 RTV로 동시에 잡지 않도록 Begin/End 규칙을 만든다.
3. 중간 RT가 viewport resize와 `FFrameContext`에 일관되게 연결되도록 한다.
4. DoF pass off 상태에서는 command가 생성되지 않거나 BeginPass가 빠르게 false를 반환하도록 한다.
5. `RenderPassRegistry` 정렬이 pass enum 순서와 일치하는지 확인한다.

### Validation

- `ReleaseBuild.bat < NUL`
- DoF off에서 기존 화면 변화 없음.
- D3D debug layer 사용 시 SRV/RTV hazard warning이 없어야 한다.

## Phase R3: Overlay and Debug Ordering Policy

### Tasks

1. `SelectionMask`와 outline이 DoF 영향을 받아야 하는지 결정한다.
2. `EditorLines`가 scene geometry로 DoF 대상인지, editor overlay로 DoF 제외 대상인지 결정한다.
3. gizmo/icon/font/UI는 기본적으로 DoF 이후 overlay로 유지한다.
4. CoC debug view, depth debug view, normal debug view가 DoF pass와 충돌하지 않도록 view mode 우선순위를 정한다.

### Recommended Policy

- Game scene geometry: DoF 대상
- Translucent surface: 별도 CoC 지원 전까지 SceneColor에는 포함되지만 depth 기반 blur 정확도는 제한
- Editor icon/gizmo/font/UI: DoF 제외
- Debug view mode: DoF보다 우선하며, debug output 자체에는 DoF를 적용하지 않음

## Phase R4: Cleanup and Naming

### Tasks

1. pass 이름을 목적 기준으로 정리한다.
   - `DoFSetup`: depth -> signed CoC
   - `DoFBackgroundBlur`: positive CoC blur
   - `DoFForegroundBlur`: negative CoC blur/mask
   - `DoFComposite`: sharp scene + DoF layers composite
2. pass-specific constant buffers와 shader paths 이름을 일치시킨다.
3. common copy/ping-pong helper가 필요하면 별도 작은 유틸로 추출한다.

### Validation

- `ReleaseBuild.bat < NUL`
- `git diff --check`

## Phase R5: Deferred Translucent Rendering Work

### Scope

기존 DoF 계획서의 Phase 4 이후 작업은 모두 이 문서의 후속 refactoring 범위로 이양한다. Opaque DoF 안정화 문서에서는 translucent를 지원하지 않는다.

### Deferred Tasks

1. `UberTranslucent` split 여부를 render pass/material routing 관점에서 재검토한다.
2. Surface translucent mesh가 `UberLit`에서 분리된 전용 shader path를 타야 하는지 결정한다.
3. Translucent pass가 CoC RTV를 함께 바인딩할지, 별도 translucent CoC pass를 둘지 결정한다.
4. `BlendStateManager`의 independent blend 지원 여부를 검토한다.
5. Surface translucent material flag 정책을 별도 material/rendering 계획에서 정의한다.
6. Particle translucent CoC 지원은 surface translucent 정책이 닫힌 뒤 별도 단계로 분리한다.
7. Opaque DoF debug path와 translucent 후속 path가 같은 CoC 리소스를 SRV/RTV로 동시에 잡지 않도록 ping-pong 정책을 설계한다.

### Validation

- 이 Phase는 후속 작업 정의만 담당한다.
- 실제 구현 착수 전 별도 상세 계획을 작성한다.
