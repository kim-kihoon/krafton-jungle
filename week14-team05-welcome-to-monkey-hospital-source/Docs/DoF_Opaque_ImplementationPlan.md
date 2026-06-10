# Opaque DoF Implementation Plan

## Context

현재 DoF 구현 목표는 translucent 지원 전 단계에서 opaque 기준 DoF를 안정적으로 완성하는 것이다. 반투명 물체의 CoC write, translucent shader split, particle translucent DoF는 이 문서의 범위가 아니다.

이번 계획의 기본 타협안은 다음과 같다.

- Opaque는 기존 depth buffer를 기반으로 CoC를 계산한다.
- Opaque pass의 MRT 출력에는 CoC target을 추가하지 않는다.
- 별도 setup pass에서 depth를 읽어 CoC RT를 채운다.
- 현재 문서에서는 translucent CoC를 지원하지 않는다.
- `force depth` 방식은 채택하지 않는다.

목표 패스 배치는 opaque DoF 관점에서 다음을 기준으로 한다.

```text
Opaque/Decal
Fog
DoFSetup          // depth -> opaque signed CoC
DoFBackgroundBlur // far/background layer
DoFForegroundBlur // near/foreground layer
DoFComposite      // scene color + DoF layers
PostProcess/FXAA/Gamma/UI/Editor overlays
```

실제 `ERenderPass` enum 정리, pass 실행 순서 재배치, overlay/UI 정책은 `Docs/RenderPass_DoF_RefactoringPlan.md`에서 추적한다.

## Goals

- Depth 기반 `DoFSetupPass`와 CoC render target을 추가한다.
- Opaque 기준 DoF를 빌드/화면 검증 가능한 상태로 만든다.
- Opaque DoF의 depth discontinuity artifact를 줄인다.
- Foreground/background blur를 분리해서 focus object silhouette가 뒤쪽 blur에 오염되지 않도록 한다.
- CoC debug 경로를 제공해서 focus distance/range/signed CoC 계산을 검증한다.
- 모든 Phase는 `ReleaseBuild.bat < NUL` 기준으로 빌드 검증 가능해야 한다.

## No Goals

- Translucent surface DoF/CoC는 지원하지 않는다.
- Particle translucent DoF/CoC는 지원하지 않는다.
- `UberTranslucent` split은 하지 않는다.
- 물리적으로 정확한 multi-layer bokeh는 구현하지 않는다.
- OIT, per-object blur accumulation, triangle-level translucent sorting은 하지 않는다.
- Opaque pass의 `UberPS_Output`에 CoC MRT 출력을 추가하지 않는다.
- 기존 depth buffer 의미를 바꾸지 않는다.

## Plan Boundary

이 문서는 opaque DoF 품질, signed CoC encoding, foreground/background split, debug 검증만 다룬다.

다음 작업은 모두 `Docs/RenderPass_DoF_RefactoringPlan.md`의 후속 작업으로 이양한다.

- Phase 4 이후의 작업
- `UberTranslucent` split
- surface translucent CoC
- particle translucent CoC
- material flag 기반 translucent DoF participation
- render pass enum/ordering/overlay policy 정리

## Phase 1: DoF Resource and Setup Pass Skeleton

### Goal

CoC RT와 `DoFSetupPass`의 최소 골격을 추가한다. 이 Phase의 목적은 DoF 데이터 경로를 먼저 만들고, depth를 읽어서 CoC 값을 생성할 수 있음을 검증하는 것이다.

### Tasks

1. Viewport 또는 render resource 계층에 `CoCTexture`, `CoCRTV`, `CoCSRV`를 추가한다.
2. viewport resize 시 CoC RT도 함께 재생성되도록 한다.
3. `FFrameContext::SetViewportInfo()`와 `ClearViewportResources()`에 CoC D3D 포인터를 연결한다.
4. camera/post-process 파라미터 구조에 DoF enable, focus distance, focus range 또는 aperture scale을 추가한다.
5. `DoFSetupPass`를 render pass registry에 등록한다.
6. `DoFSetupPass`는 depth copy 갱신을 자기 책임으로 수행한다.
7. `Shaders/PostProcess/DoFSetup.hlsl`을 추가하고 `ShaderManager`의 shader path/lookup에 등록한다.
8. 새 C++/HLSL 파일은 `KraftonEngine.vcxproj`와 `.filters`에도 반영한다.
9. 첫 출력은 grayscale CoC debug 값으로 단순화한다.

### Validation

- `ReleaseBuild.bat < NUL`
- DoF off 상태에서 기존 화면 변화가 없어야 한다.
- 임시 debug view 또는 blit으로 CoC RT가 depth에 따라 달라지는지 확인한다.

### No Goal

- blur 적용은 하지 않는다.
- translucent 반영은 하지 않는다.
- material/shader routing 변경은 하지 않는다.

## Phase 2: Opaque DoF Composite

### Goal

SceneColor와 CoC RT를 사용해서 opaque 기준 DoF blur/composite를 구현한다.

### Tasks

1. `DoFPass`를 추가하거나 기존 post-process 체인에 DoF composite pass를 추가한다.
2. DoF pass는 SceneColor를 읽으면서 같은 RTV에 쓰지 않는다. 기존 FXAA/Gamma처럼 `SceneColorCopyTexture`에 복사한 뒤 SRV로 읽거나, 별도 ping-pong RT를 사용한다.
3. Pass 종료 시 DoF가 바인딩한 SceneColor/CoC SRV를 명시적으로 unbind해서 이후 RTV 바인딩 hazard를 피한다.
4. 초기 구현은 full-res small kernel 또는 간단한 separable blur로 시작한다.
5. CoC 값 기반 blur 강도를 계산한다.
6. editor/game camera 설정에서 focus distance를 조정할 수 있는 최소 입력 경로를 만든다.
7. DoF on/off 토글을 추가한다.

### Validation

- `ReleaseBuild.bat < NUL`
- opaque 물체 기준으로 focus plane 근처는 선명하고 앞/뒤는 blur되어야 한다.
- DoF off 상태에서 기존 post-process 결과와 동일해야 한다.

### No Goal

- 고품질 bokeh shape 구현은 하지 않는다.
- translucent 물체의 별도 CoC 처리는 하지 않는다.
- `UberLit`/`UberTranslucent` 구조 변경은 하지 않는다.
- editor line/gizmo/UI가 DoF에 포함될지 여부를 이 Phase에서 최종 해결하지 않는다.

## Phase 2.5: Opaque DoF Stability - Foreground/Background Split

### Problem

초점 물체 A는 내부가 선명하지만, A 뒤쪽 배경 blur가 A의 silhouette 근처에 섞이면 외곽선만 초점이 어긋난 것처럼 보인다. 이는 single-layer full-res gather blur가 depth discontinuity를 존중하지 않기 때문이다.

### Goal

Signed CoC와 foreground/background 분리 blur를 도입해 focus object silhouette bleeding을 줄인다. A가 focus plane에 있을 때 A의 edge가 뒤쪽 background blur에 오염되지 않아야 한다.

### Proposed Rendering Model

```text
DoFSetup
  depth -> signed CoC
  negative: foreground, positive: background, near zero: focus

DoFBackgroundBlur
  scene color + depth + positive CoC
  far/background blur only
  nearer/focus samples reject or strongly down-weight

DoFForegroundBlur
  scene color + depth + negative CoC
  foreground blur/mask only
  optional dilation to avoid thin foreground holes

DoFComposite
  sharp scene + background blur + foreground blur + signed CoC
  focus pixels prefer sharp scene
  foreground blur overlays background when foreground CoC is significant
```

초기 구현은 고품질 bokeh가 아니라 안정적인 edge 정책을 우선한다. pass 수는 늘어나도 된다. 단, pass enum/실행 순서 정리 자체는 `RenderPass_DoF_RefactoringPlan.md`의 범위다.

### Tasks

1. CoC encoding을 signed로 변경한다.
   - `0`: focus plane
   - `> 0`: background/far blur
   - `< 0`: foreground/near blur
   - 권장 포맷은 우선 `R16_FLOAT` 유지
2. `DoFSetup.hlsl`에서 focus distance 대비 signed distance를 계산한다.
3. `DoFComposite.hlsl`의 단일 abs-CoC blur를 제거하고, background/foreground 경로로 분리한다.
4. background blur에서 sample depth가 current pixel보다 더 가까운 경우 contribution을 줄인다.
5. focus 또는 foreground object pixel은 background blur 결과를 직접 받지 않도록 한다.
6. foreground blur는 별도 mask/blur로 계산하고 composite에서 sharp/background 위에 합성한다.
7. 필요한 중간 RT를 추가한다.
   - 최소 후보: `DoFBackgroundTexture`, `DoFForegroundTexture`
   - foreground alpha/mask가 필요하면 `R16_FLOAT` 또는 `RG16_FLOAT` 후보를 검토
8. SceneColor를 SRV로 읽고 같은 RTV에 쓰는 hazard를 피하기 위해 copy 또는 ping-pong 규칙을 명시한다.
9. CoC debug view에서 signed CoC를 확인할 수 있게 한다.
   - background: 한 색상 계열
   - foreground: 다른 색상 계열
   - focus: black 또는 neutral
10. DoF off 상태에서는 기존 post-process 결과와 동일해야 한다.

### Acceptance Criteria

- focus object A 내부와 silhouette가 모두 선명하게 유지된다.
- A 뒤쪽 background는 blur되지만 A edge 안쪽으로 번지지 않는다.
- A 앞쪽 foreground object는 focus plane과 분리되어 blur된다.
- foreground object 주변에 명백한 unblurred halo가 없어야 한다.
- sky/background depth가 없는 영역의 signed CoC 정책이 debug view에서 예측 가능하다.
- `ReleaseBuild.bat < NUL` 통과.

### No Goal

- physically correct multi-layer DoF는 하지 않는다.
- polygonal bokeh shape, aperture blade, high quality scatter bokeh는 하지 않는다.
- translucent CoC write는 하지 않는다.
- render pass framework 전반을 정리하지 않는다.

## Phase 3: CoC Debug and Encoding Policy

### Goal

Opaque DoF의 CoC 데이터가 신뢰 가능한지 확인할 수 있는 debug/정책 경로를 정리한다.

### Tasks

1. CoC RT debug view mode 또는 post-process debug output을 추가한다.
2. focus distance/range 값이 화면에서 예측 가능하게 보이도록 조정한다.
3. Phase 2.5에서 도입한 signed CoC 정책을 문서화하고 debug view와 일치시킨다.
4. CoC RT 포맷을 확정한다. opaque signed CoC에는 우선 `R16_FLOAT`를 유지한다.
5. sky/background처럼 depth가 없는 영역의 CoC 정책을 확정한다.
6. foreground/background blur intermediate RT 포맷과 clear policy를 확정한다.

### Validation

- `ReleaseBuild.bat < NUL`
- CoC debug 화면에서 focus plane 값이 0 근처여야 한다.
- foreground/background 영역의 CoC 부호와 강도가 예측 가능해야 한다.

### No Goal

- translucent CoC write는 하지 않는다.
- `UberLit`/`UberTranslucent` 구조 변경은 하지 않는다.

## Deferred Work

Phase 4 이후의 모든 작업은 이 문서에서 제거하고 `Docs/RenderPass_DoF_RefactoringPlan.md`로 이양한다. 현재 문서의 완료 기준은 opaque DoF 안정화와 debug 검증까지다.

## Recommended Commit Boundaries

1. `DoFSetupPass + CoC RT`
2. `Opaque DoF composite`
3. `Opaque DoF stability - foreground/background split`
4. `CoC debug/view controls`

## Implementation Notes

- `force depth`는 구현이 쉽지만 depth buffer의 의미를 깨뜨리므로 제외한다.
- Opaque pass에 CoC MRT를 추가하면 모든 opaque draw가 불필요한 target을 부담하므로 제외한다.
- CoC RT는 DoF가 켜진 경우에만 준비/사용하는 방향이 바람직하다.
- Focus object silhouette bleeding은 kernel 크기만 줄여서는 해결하지 않는다. signed CoC와 depth-aware foreground/background 분리로 해결한다.
- D3D11에서 같은 리소스를 SRV와 RTV로 동시에 사용하는 경로는 피한다. SceneColor와 CoC 모두 copy 또는 ping-pong 정책을 명시한다.
- Editor overlay, gizmo, UI의 DoF 전후 정책은 `RenderPass_DoF_RefactoringPlan.md`에서 결정한다.
