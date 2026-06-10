# NvCloth World Forces, Wind Preview, And Inertia Plan

## Context

현재 cloth MVP는 CPU skinning 기반의 component-local simulation으로 동작한다. 이 방식은 render data, bounds, picking, editor authoring과 잘 맞지만, actor/component가 world에서 이동하거나 회전할 때 cloth가 world-space에서 시뮬레이션되는 것처럼 보이려면 별도의 world force 및 transform-history 보정이 필요하다.

이 문서는 기존 `Docs/NvCloth_Integration_AbstractPlan.md` 이후의 후속 작업 계획서다. 목적은 collision을 붙이기 전에 world gravity, wind, component motion inertia, teleport handling을 정리해 component-local cloth의 체감 품질을 끌어올리는 것이다.

## Goals

- component-local cloth simulation을 유지하면서 world gravity 방향을 올바르게 반영한다.
- Mesh Editor에서 gravity와 wind 값을 조절하며 preview cloth 반응을 즉시 확인할 수 있게 한다.
- NvCloth wind API를 사용해 ambient wind, drag, lift 반응을 지원한다.
- actor/component 이동과 회전에 따른 관성을 local simulation에 보정한다.
- teleport, 큰 transform jump, scale 변경 시 cloth state를 안정적으로 reset 또는 snap한다.
- cloth 결과 반영 후 skinned revision, bounds, picking 경로가 계속 갱신되도록 유지한다.

## No Goals

- physics asset collision, world collision, self collision은 이 문서 범위에 포함하지 않는다.
- GPU skinning cloth support는 이 문서 범위에 포함하지 않는다.
- 완전한 world-space cloth simulation으로 solver space를 전환하지 않는다.
- wind actor, global wind field, volume 기반 wind authoring은 초기 범위에 포함하지 않는다.
- cloth asset에 복잡한 per-vertex wind mask, aero map, material별 wind profile을 추가하지 않는다.
- gameplay physics velocity와 cloth inertia를 완전히 통합하지 않는다.

## Implementation Policy

### Accepted Decisions

- gravity source는 world setting에서 제공한다. world setting이 유효하지 않은 editor/bootstrap 상황에서만 fallback gravity를 사용한다.
- Mesh Editor gravity control은 cloth-only override가 아니라 preview world의 world setting gravity를 조절한다.
- Mesh Editor wind direction/speed는 preview-only override로 둔다. preview 값을 바꿨다는 이유만으로 skeletal mesh asset dirty를 만들지 않는다.
- cloth별 wind response 값은 asset config에 저장한다.
- preview wind 값 변경 시 cloth simulation state는 기본적으로 유지하고, 큰 변화나 튜닝 중 불안정 상태는 reset control로 복구한다.
- inertia는 transform-history 기반 보정을 기본안으로 채택한다.
- teleport distance/rotation threshold는 개발 초반에 자주 조정될 수 있으므로 runtime/debug config로 분리한다.
- non-uniform scale은 no-goal이다. uniform scale을 포함한 scale 변경 순간에는 cloth simulation을 reset/recreate한다.
- component movement induced wind는 default off로 두고, inertia가 안정된 뒤 별도 옵션으로 검토한다.

### Simulation Space

- NvCloth particle position은 계속 component-local skinned space에 둔다.
- world vector를 cloth에 적용할 때는 current component world inverse로 vector 변환한다.
- position 변환과 vector 변환을 명확히 분리한다. gravity/wind는 translation 영향을 받지 않아야 한다.
- non-uniform scale은 지원 범위 밖으로 둔다. scale이 바뀌면 inertia 보정을 적용하지 않고 reset/recreate한다.
- cloth simulation force/velocity 단위계는 meter 기준으로 둔다.
- position은 m, velocity/wind velocity는 m/s, gravity/acceleration은 m/s^2를 기본 단위로 사용한다.
- 기존 코드나 asset/import path에 cm 기반 값이 남아 있으면 cloth force 입력 전에 m 단위계로 정규화한다.

### Force Ownership

- cloth asset에는 cloth가 force에 반응하는 계수를 저장한다.
- editor preview에는 scene/runtime override force 값을 둔다.
- 초기 runtime world wind는 editor preview-local 설정으로 시작하고, global wind system은 후속 phase로 분리한다.
- gravity magnitude와 direction은 world setting에서 world 기준 입력으로 제공한다.
- cloth runtime은 world setting gravity에 asset config의 `GravityScale`을 곱해 section별 반응을 조절한다.
- editor preview world는 editor main world와 독립된 `FWorldSettings`를 가질 수 있다. Mesh Editor preview control은 반드시 해당 preview world의 setting을 읽고 써야 한다.

### Wind Policy

- NvCloth `setWindVelocity`, `setDragCoefficient`, `setLiftCoefficient`, `setFluidDensity`를 사용한다.
- `WindVelocity`는 world-space authoring 값으로 표현하고, cloth runtime에서 component-local vector로 변환한다.
- `DragCoefficient`, `LiftCoefficient`, `FluidDensity`, `WindScale`은 cloth config 또는 runtime override로 표현한다.
- 처음에는 ambient wind만 적용한다.
- component movement로 인한 상대풍은 inertia phase가 안정된 뒤 별도 scale로 추가한다.
- wind preview control은 Mesh Editor에 일찍 추가하되, 저장 정책은 asset response config와 preview-only override를 분리한다.

### Inertia Policy

- runtime은 previous/current component transform을 보관한다.
- 첫 tick, runtime rebuild, skinning mode 전환, mesh/LOD 변경, cloth payload 변경 후에는 previous/current transform을 같은 값으로 초기화한다.
- free particle은 previous world position을 current component-local space로 되돌리는 방식으로 actor 이동/회전 관성을 보정한다.
- fixed particle은 animation pose가 최종 기준이다. fixed particle의 current/previous는 animated skinned position에 snap한다.
- 큰 이동/회전은 teleport로 분류하고 inertia를 적용하지 않는다.

## Data Shape

초기 확장 후보:

```cpp
struct FSkeletalClothConfig
{
    float GravityScale = 1.0f;
    float SolverFrequency = 120.0f;
    float Stiffness = 1.0f;
    float Damping = 0.04f;

    float WindScale = 1.0f;
    float DragCoefficient = 0.0f;
    float LiftCoefficient = 0.0f;
    float FluidDensity = 1.225f;
    float InertiaLinearScale = 1.0f;
    float InertiaAngularScale = 1.0f;
};

struct FWorldSettings
{
    FString GameModeClassName;
    FVector Gravity = FVector(0.0f, 0.0f, -9.81f);
};

struct FClothWorldForceContext
{
    FVector WorldGravity = FVector(0.0f, 0.0f, -9.81f);
    FVector WorldWindVelocity = FVector::ZeroVector;
    bool bUsePreviewWindOverride = false;
};

struct FClothDebugRuntimeConfig
{
    float TeleportDistanceThreshold = 300.0f;
    float TeleportRotationThresholdDegrees = 45.0f;
    bool bResetOnScaleChange = true;
};
```

주의:

- `FSkeletalClothConfig` 필드 추가는 cloth payload serialization version gate와 함께 처리한다.
- package version을 추가로 올릴지, `FSkeletalMeshClothPayload::Version` sub-version만 올릴지는 구현 직전에 결정한다. 이미 cloth payload package version이 존재하므로, 가능하면 cloth payload 내부 version gate로 wind config 확장을 제한한다.
- `FWorldSettings::Gravity`는 scene JSON의 `WorldSettings`에 저장하고, 기존 scene에는 fallback gravity를 적용한다.
- Mesh Editor preview-only 값은 skeletal mesh asset 저장값으로 바로 섞지 않는다.
- preview 값 저장이 필요하면 editor settings 또는 preview document state로 별도 관리한다.
- `FClothDebugRuntimeConfig`의 threshold 값은 개발 초반 튜닝 대상으로 보고, asset serialization 대상에서 제외한다.

## Phase Outline

### Phase 1: World Force Context And Transform History

#### Goal

`FSkeletalClothRuntime::Tick`이 current/previous component transform과 world force context를 받을 수 있게 한다. 이 phase는 gravity/wind/inertia의 공통 입력 계약을 만드는 단계다.

#### Tasks

1. `FWorldSettings`에 gravity field를 추가하고 scene save/load에서 기존 scene fallback을 유지한다.
2. `USkeletalMeshComponent::TickClothSimulation`에서 current component world matrix를 cloth runtime에 전달한다.
3. `FSkeletalClothRuntime` 내부에 previous/current component transform history를 추가한다.
4. reset/rebuild 시 transform history와 accumulated simulation time을 함께 초기화한다.
5. world vector를 component-local vector로 변환하는 helper를 추가한다.
6. scale validity check를 추가하고 scale 변경은 reset/recreate 대상으로 둔다.
7. `FClothWorldForceContext`가 component가 속한 world의 world setting gravity를 받을 수 있는 입력 경로를 만든다.
8. editor preview component는 editor main world가 아니라 preview world setting에서 gravity를 가져오게 한다.
9. 기존 cloth runtime의 cm/s^2 gravity 상수 또는 cm/s wind 가정을 m 단위계로 전환한다.

#### Validation

- cloth runtime rebuild 후 첫 frame에서 inertia가 적용되지 않는다.
- component transform이 없어도 기존 CPU cloth path가 깨지지 않는다.
- legacy scene load 시 gravity field가 없어도 fallback gravity가 적용된다.
- Mesh Editor preview world gravity와 level editor main world gravity가 서로 섞이지 않는다.
- bounds/picking/revision 갱신 경로가 기존과 동일하게 호출된다.

#### No Goal

- wind UI, wind config 저장, inertia 보정은 이 phase에서 구현하지 않는다.

### Phase 2: World Gravity And Early Mesh Editor Wind Preview

#### Goal

world gravity를 component-local로 변환해 적용하고, Mesh Editor에서 gravity/wind preview 값을 조절할 수 있게 한다. 사용자가 현재 gravity 수치를 조절하듯 wind direction/speed도 즉시 preview할 수 있어야 한다.

#### Tasks

1. 기존 fixed `-Z` gravity를 world setting gravity vector 변환 기반으로 바꾼다.
2. Mesh Editor cloth panel에 preview wind enable, direction, speed control을 추가한다.
3. preview wind 값을 `FClothWorldForceContext`로 전달한다.
4. preview wind 변경 시 cloth runtime은 기본 유지하고, 사용자가 직접 reset할 수 있는 control을 제공한다.
5. gravity/wind preview control은 현재 선택된 preview component에만 영향을 주게 한다.
6. gravity와 wind는 cloth 생성 시 1회 설정이 아니라 simulate 직전에 매 frame 갱신한다.

#### Validation

- actor/component를 회전해도 cloth는 world down 방향으로 떨어진다.
- world setting gravity 변경이 cloth gravity에 반영된다.
- runtime 중 world setting gravity나 preview wind를 바꿔도 cloth runtime rebuild 없이 다음 tick에 반영된다.
- Mesh Editor에서 wind speed를 올리면 cloth 반응이 즉시 달라진다.
- preview wind를 끄면 wind가 없는 상태로 돌아간다.
- preview value 변경이 skeletal mesh asset dirty를 불필요하게 만들지 않는다.

#### No Goal

- wind response coefficient 저장 UI를 완성하지 않는다.
- level-wide wind actor나 runtime global wind system을 만들지 않는다.
- actor 이동에 따른 상대풍은 이 phase에서 넣지 않는다.

### Phase 3: Wind Response Config And Serialization

#### Goal

cloth asset이 wind에 얼마나 반응하는지 저장할 수 있게 한다. preview wind 값 자체가 아니라 cloth response coefficient를 저장한다.

#### Tasks

1. `FSkeletalClothConfig`에 `WindScale`, `DragCoefficient`, `LiftCoefficient`, `FluidDensity`를 추가한다.
2. cloth payload serialization version gate 또는 package version gate를 갱신한다.
3. Mesh Editor에서 현재 cloth section의 wind response 값을 편집할 수 있게 한다.
4. NvCloth cloth 생성 또는 per-frame update 시 `setWindVelocity`, `setDragCoefficient`, `setLiftCoefficient`, `setFluidDensity`를 적용한다.
5. invalid coefficient 입력을 clamp하거나 default로 복구한다.

#### Validation

- asset save/reload 후 wind response 값이 유지된다.
- wind response 값이 0이면 wind preview가 켜져 있어도 cloth 반응이 제한된다.
- 여러 cloth section이 서로 다른 wind response 값을 가질 수 있다.
- legacy skeletal mesh package는 cloth wind field가 없어도 정상 로드된다.
- 기존 cloth payload version asset은 wind response field가 없어도 default config로 로드된다.

#### No Goal

- per-vertex wind mask나 material별 aero profile은 만들지 않는다.

### Phase 4: Linear And Angular Inertia

#### Goal

component-local simulation을 유지하면서 actor/component 이동 및 회전에 따른 inertia를 free particle에 반영한다.

#### Tasks

1. previous/current component transform 차이를 계산한다.
2. free particle의 previous/current local state를 previous world -> current local 기준으로 보정한다.
3. fixed particle은 animated skinned pose에 current/previous 모두 snap한다.
4. linear inertia scale과 angular inertia scale을 config 또는 runtime debug 값으로 둔다.
5. inertia 보정 순서를 animated pins update 전후 중 하나로 고정하고 문서화한다.
6. wind와 inertia가 동시에 적용될 때 중복 효과가 과하지 않도록 default scale을 보수적으로 잡는다.
7. LOD 변경 또는 active cloth section 변경 시 transform history를 유지할지 reset할지 결정하고, 기본은 reset/recreate로 둔다.

#### Validation

- actor가 이동하면 cloth가 이동 반대 방향으로 자연스럽게 남는다.
- actor가 회전하면 free particle이 회전 관성을 보인다.
- fixed particle은 skeleton pose에서 떨어져 나가지 않는다.
- wind만 켠 경우와 inertia만 켠 경우를 Mesh Editor에서 분리해 확인할 수 있다.
- LOD/section 변경 뒤 이전 cloth particle state가 새 mapping에 재사용되지 않는다.

#### No Goal

- gameplay rigidbody velocity와 cloth inertia를 완전히 동기화하지 않는다.
- component movement induced wind는 이 phase의 필수 구현으로 보지 않는다.

### Phase 5: Teleport, Reset, And Stability Rules

#### Goal

큰 transform jump, editor drag, asset reload, skinning mode 변경에서 cloth popping과 폭주를 막는다.

#### Tasks

1. teleport distance threshold와 rotation threshold를 debug runtime config로 둔다.
2. teleport로 판단되면 inertia를 적용하지 않고 cloth state를 reset 또는 animated pose 기준으로 snap한다.
3. scale 변경, non-uniform scale, invalid transform은 reset 대상으로 둔다.
4. CPU -> GPU -> CPU skinning mode 전환 시 기존 정책대로 reset/recreate한다.
5. Mesh Editor에 cloth reset control을 노출한다.
6. debug runtime config를 editor/dev UI 또는 console command 중 하나에서 조정할 수 있게 한다.

#### Validation

- actor를 큰 거리로 이동해도 cloth가 장면을 가로질러 날아가지 않는다.
- editor transform 조작 중 cloth가 불안정해지면 reset으로 즉시 복구 가능하다.
- teleport threshold 값을 debug config에서 조정할 수 있다.
- scale 변경 순간 cloth simulation이 reset/recreate된다.
- debug runtime config 변경은 skeletal mesh asset dirty를 만들지 않는다.
- skinning mode 전환 후 stale cloth state가 재개되지 않는다.

#### No Goal

- physics teleport event와 engine-wide movement API를 모두 통합하지 않는다.

### Phase 6: Runtime Wind Source Hook

#### Goal

Mesh Editor preview wind를 넘어 game/runtime에서 wind source를 공급할 최소 hook을 만든다.

#### Tasks

1. `FClothWorldForceContext`를 editor preview와 game runtime 양쪽에서 채울 수 있게 한다.
2. 초기 runtime default는 zero wind로 둔다.
3. global wind actor 또는 wind field가 들어올 수 있는 extension point만 둔다.
4. component movement induced wind를 옵션으로 검토하되 default off로 둔다.

#### Validation

- game runtime에서 wind가 없어도 기존 cloth 동작이 유지된다.
- editor preview wind와 game runtime wind가 서로 섞이지 않는다.
- future wind actor 도입 시 cloth runtime 내부를 다시 크게 바꾸지 않아도 된다.

#### No Goal

- wind actor, wind volume, curve/turbulence authoring을 구현하지 않는다.

## Verification Plan

- `git diff --check`
- `cmd /c "ReleaseBuild.bat < NUL"`
- Mesh Editor preview에서 다음 시나리오를 수동 확인한다.
  - gravity direction with rotated actor
  - wind off/on and wind speed change
  - drag/lift coefficient change
  - actor translate inertia
  - actor rotate inertia
  - teleport/reset behavior
  - CPU -> GPU -> CPU skinning mode reset behavior

## Main Risks

- gravity/wind vector 변환에서 position transform을 쓰면 component translation이 force에 섞인다.
- 기존 runtime의 `-980` gravity 같은 cm 기반 상수를 남기면 m 단위계 cloth에서 force가 100배 강해진다.
- world setting gravity와 editor preview override의 우선순위가 불명확하면 editor와 game runtime 결과가 달라진다.
- preview component가 editor main world setting을 잘못 읽으면 Mesh Editor preview와 level editor 결과가 서로 오염된다.
- NvCloth wind는 triangle 기반 force라 normal/tangent 및 triangle winding 문제가 wind 반응 품질에 영향을 줄 수 있다.
- wind response config를 preview value와 섞으면 asset dirty state가 예측하기 어려워진다.
- force 값을 cloth 생성 시점에만 설정하면 world setting gravity나 preview wind 변경이 runtime 중 반영되지 않는다.
- inertia와 component movement induced wind를 동시에 강하게 적용하면 효과가 중복된다.
- angular inertia는 fixed particle과 free particle 사이 frame mismatch를 크게 만들 수 있으므로 default scale을 보수적으로 둬야 한다.
- teleport threshold가 없거나 debug config 기본값이 너무 크면 editor drag, spawn 이동, scene load에서 cloth가 폭주할 수 있다.
- non-uniform scale을 무시하면 distance constraint와 wind/drag 계산이 모두 왜곡된다.
- scale 변경 reset을 빠뜨리면 old particle state와 new component transform scale이 섞여 constraint가 튈 수 있다.
- LOD 또는 section mapping 변경 시 previous particle state를 재사용하면 엉뚱한 render vertex에 cloth state가 붙을 수 있다.
- cloth payload config 필드 추가는 legacy skeletal mesh package load path와 package version gate를 다시 검증해야 한다.

## Deferred Work

- GPU skinning cloth support
- physics asset collision
- world collision
- self collision
- global wind actor
- wind volume and turbulence field
- per-vertex wind mask
- advanced cloth material/aero profile
