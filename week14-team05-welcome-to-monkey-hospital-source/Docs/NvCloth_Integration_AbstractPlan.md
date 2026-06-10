# NvCloth Integration Abstract Plan

## Context

이 문서는 NvCloth 기반 cloth simulation을 엔진에 도입하기 위한 상위 계획서이다. 현재 문서는 구현 class/API/파일 단위의 세부 계획이 아니라, MVP 범위와 고정 정책을 먼저 정리한다. 세부 구현 계획은 이 문서의 정책을 기준으로 별도 문서에서 구체화한다.

현재 전제는 다음과 같다.

- 엔진의 PhysX 버전은 4.1이다.
- 준비된 NvCloth 버전은 1.1.6이며, 엔진의 PhysX 4.1 조합에서 version compatibility issue는 없는 것으로 확인했다.
- NvCloth/PxShared 관련 ThirdParty 파일 추가 자체는 이미 완료된 상태로 본다.
- 1차 runtime backend는 CPU NvCloth factory만 사용한다.
- DX11 backend는 성능 문제가 확인될 때 선택할 수 있는 2차 옵션으로만 남기며, runtime 자동 선택이 아니라 컴파일타임 선택을 기본 정책으로 둔다.
- CUDA backend는 범용성, 하드웨어 의존성, 안정성 측면에서 no-goal로 두며, CUDA 관련 분기/설정/UI는 만들지 않는다.
- collision은 이번 MVP에서 철저히 제외한다. MVP 완료 후 physics asset/world collision 상태를 다시 확인하고 별도 계획으로 재검토한다.

현재 확인된 상태는 다음과 같다.

- `ThirdParty/NvCloth`, `ThirdParty/PxShared` 파일 배치는 완료되어 있다.
- 임시 검증 코드로 NvCloth CPU factory 생성, fabric cooking, solver simulation, particle readback이 동작하는 것을 확인했다.
- Release x64 빌드에서 NvCloth import lib link와 `NvCloth_x64.dll` post-build copy가 동작하는 것을 확인했다.
- 위 검증을 위한 임시 소스 코드는 discard되었으므로, 실제 엔진 소스에는 아직 cloth runtime 구현이 남아 있지 않다.

## Goals

- 이미 추가된 NvCloth 1.1.6 ThirdParty dependency를 엔진 빌드/런타임 코드에서 정식으로 받아들인다.
- CPU backend를 구현하되, DX11 backend를 나중에 컴파일타임 대체 backend로 붙일 수 있는 backend interface를 둔다.
- `USkeletalMesh`가 LOD별 material section cloth data, per-vertex paint data, default simulation config를 소유하도록 한다.
- material section 단위로 cloth 대상을 지정하고, section마다 독립 cloth instance를 생성할 수 있게 한다.
- MVP에서는 render mesh cloth vertex와 NvCloth particle을 1:1로 매핑한다.
- cloth simulation 결과를 `USkeletalMesh` 원본 render buffer에 직접 쓰지 않고, component별 dynamic vertex buffer 또는 동등한 per-instance render data에 반영한다.
- cloth 설정과 paint data를 skeletal mesh package에 저장/로드한다.
- Mesh Editor에서 cloth section 지정, `MaxDistance` paint, heatmap visualization을 수행할 수 있게 한다.

## No Goals

- CUDA backend 지원은 하지 않는다.
- MVP에서 DX11 backend를 구현하지 않는다.
- MVP에서 physics asset collision, world collision, self collision, tearing, cloth LOD 자동 생성은 구현하지 않는다.
- MVP에서 render mesh와 별도의 단순화 simulation mesh 또는 barycentric render mapping을 구현하지 않는다.
- MVP에서 shader/vertex factory/render command 전반을 크게 바꾸는 GPU-resident cloth render path를 구현하지 않는다.
- MVP에서 GPU skinning mode와 cloth simulation을 동시에 지원하지 않는다.
- MVP 직후에도 physics/world collision을 바로 붙이지 않는다. GPU skinning cloth support를 먼저 안정화한 뒤 collision phase로 넘어간다.
- MVP에서 cooked fabric data를 package에 실제 저장하지 않는다.
- MVP에서 render visibility만으로 cloth simulation을 pause하는 최적화를 구현하지 않는다.
- Unreal 수준의 완전한 clothing authoring workflow를 한 번에 구현하지 않는다.
- FBX/Alembic 등 외부 DCC cloth authoring format import는 초기 범위에 포함하지 않는다.

## Implementation Policy

### Backend

- backend 선택은 기본적으로 컴파일타임 선택으로 한다.
- CPU/DX11 전환 가능성을 위해 thin-to-medium backend interface를 둔다.
- backend interface는 NvCloth factory/solver/fabric/cloth lifecycle과 simulation output transport까지만 책임진다.
- CPU backend는 CPU position output을 제공한다.
- DX11 backend를 도입할 경우 CPU readback을 강제하지 않고 GPU-resident output path를 제공할 수 있어야 한다.
- material section/LOD mapping, paint data interpretation, editor state, tick gate, render proxy policy는 backend가 아니라 component-owned cloth runtime/render layer가 책임진다.

### Asset Data

- cloth data의 주 owner는 `USkeletalMesh`로 둔다.
- 단, cloth payload의 실제 저장 위치는 `USkeletalMesh` 객체 멤버가 아니라 `FSkeletalMesh` payload 내부로 둔다.
- 이 결정은 Phase 1 backend skeleton 구현 전의 잠정 자료구조 설계이며, Phase 1 완료 후 실제 serialization/resource ownership 제약을 다시 확인한다.
- 별도 `UClothAsset`은 기본 runtime/authoring 소유자로 두지 않는다.
- 별도 cloth asset을 만들더라도 export/import 또는 data exchange 역할로 제한하고, 최종 데이터 소유권은 `USkeletalMesh`에 둔다.
- cloth section mapping, paint data, config는 skeletal mesh package 내부에 저장한다.
- cloth 대상은 material section 단위로 지정한다.
- 하나의 cloth asset data는 하나의 section range만 참조한다.
- section마다 별도의 cloth instance를 둔다.
- LOD별 cloth data는 독립적으로 저장한다. LOD0의 cloth 설정은 LOD1 이상에 영향을 주지 않는다.
- paint data는 per-vertex로 둔다.
- paint data는 전체 skeletal mesh vertex array 기준이 아니라, cloth-local particle order 기준으로 저장한다.
- cloth-local particle과 render vertex 사이의 mapping은 명시적으로 저장한다.
- NvCloth fabric cooking과 stale 검증을 위해 cloth-local triangle index buffer 또는 동등한 topology payload를 명시적으로 저장한다.
- cloth-local mapping은 section index range에서 뽑은 unique render vertex subset과 그 subset 기준 triangle index로 구성한다.
- 다른 section과 공유된 render vertex, duplicated seam vertex, degenerate triangle 처리는 Phase 2에서 validation 항목으로 고정한다.
- NvCloth `Factory`, `Fabric`, `Cloth`, `Solver` pointer나 backend runtime object는 asset payload에 저장하지 않는다.
- cooked fabric data 저장 slot/header는 예약하되, MVP에서는 비워둔다.

잠정 자료구조 방향:

```cpp
struct FSkeletalClothSectionBinding
{
    uint32 LODIndex = 0;
    int32 SectionIndex = -1;
    FString MaterialSlotName;
    uint32 FirstIndex = 0;
    uint32 IndexCount = 0;
    uint32 SourceVertexCount = 0;
    uint32 SourceIndexCount = 0;
};

struct FSkeletalClothPaintData
{
    TArray<float> MaxDistanceValues; // cloth-local particle order
    float ViewMin = 0.0f;
    float ViewMax = 100.0f;
};

struct FSkeletalClothConfig
{
    float GravityScale = 1.0f;
    float SolverFrequency = 120.0f;
    float Stiffness = 1.0f;
    float Damping = 0.04f;
};

struct FSkeletalClothData
{
    FString Name;
    bool bEnabled = true;
    FSkeletalClothSectionBinding Binding;
    TArray<uint32> RenderVertexIndices;
    TArray<uint32> ParticleToRenderVertex;
    TArray<uint32> ClothLocalIndices;
    FSkeletalClothPaintData Paint;
    FSkeletalClothConfig Config;
    TArray<uint8> CookedFabricData; // reserved, empty in MVP
};

struct FSkeletalClothLODData
{
    uint32 LODIndex = 0;
    TArray<FSkeletalClothData> Cloths;
};

struct FSkeletalMeshClothPayload
{
    uint32 Version = 1;
    TArray<FSkeletalClothLODData> LODs;
};
```

`FSkeletalMeshClothPayload`는 `FSkeletalMesh` 내부에 포함하는 방향으로 설계한다. 이유는 cloth section binding, paint, particle mapping이 `FSkeletalMesh::Vertices`, `Indices`, `Sections`, `MeshRanges`와 같은 생명주기를 가지기 때문이다. `USkeletalMesh`는 해당 payload를 포함한 mesh asset의 load/save, dirty state, reimport preservation, editor API를 조율한다.

### Simulation And Render Data

- MVP는 render mesh cloth vertex와 NvCloth particle을 1:1로 매핑한다.
- paint 값으로 fixed particle과 움직임 허용 범위를 제어한다.
- cloth 대상이 아닌 skinned 영역과 cloth 영역의 경계 처리는 paint 값을 기준으로 결정한다.
- cloth particle 결과는 `USkeletalMesh` 원본 render buffer에 직접 쓰지 않는다.
- cloth 결과는 component별 cloth dynamic vertex buffer 또는 동등한 per-instance render data에 반영한다.
- MVP에서도 cloth section의 deformed triangle 기준으로 normal/tangent를 재계산해 lighting artifact를 줄인다.
- render proxy 수정은 허용하되, MVP에서 shader/vertex factory/render command 전반을 크게 바꾸는 방식은 피한다.
- 단순화 simulation mesh와 barycentric render mapping은 후속 최적화로 분리한다.
- MVP cloth simulation space는 component-local skinned space로 고정한다.
- world-space simulation은 actor 이동/회전에 따른 관성을 더 직접적으로 표현할 수 있지만, 고정점/skinned vertex와 자유 particle 사이의 frame mismatch로 cloth가 skinned mesh를 파고드는 문제가 커질 수 있으므로 MVP 범위에서 제외한다.
- actor 이동/회전 관성, teleport handling, world force 변환은 component-local simulation 위의 후속 고도화 항목으로 분리한다.
- 단, gravity는 MVP에서도 world gravity vector를 component-local simulation space로 변환해 적용한다. actor 이동/회전 관성과 teleport 보정은 후속으로 미루더라도 gravity 방향은 world 기준을 유지한다.
- MVP cloth simulation은 CPU skinning mode에서만 지원한다.
- GPU skinning mode에서는 cloth simulation을 활성화하지 않고 기존 skeletal mesh render path로 fallback한다.
- cloth-enabled component의 skinning mode를 runtime에서 암묵적으로 강제 전환하지 않는다. 사용자가 GPU skinning mode를 선택한 상태라면 MVP cloth는 비활성 상태로 남기고, editor/runtime 표시나 로그로 지원 범위를 알린다.
- MVP cloth 활성 여부는 `SkinningModeRuntime::Get() == ESkinningMode::CPU` 같은 명시 조건으로 판단한다. `SkinnedVertices` 존재 여부나 morph target 때문에 생성된 CPU cache만으로 cloth를 활성화하지 않는다.
- CPU skinning mode에서 GPU skinning mode로 전환되면 MVP cloth runtime은 suspend/release한다. 이후 CPU skinning mode로 돌아오면 기존 simulation state를 재개하지 않고 cloth instance를 reset/recreate해서 초기 상태부터 시작한다.
- GPU skinning mode와 cloth simulation의 동시 지원은 후속 고도화 phase에서 section-level per-instance buffer, section별 skinning mode, vertex factory/shader 선택 문제를 함께 해결한다.

### Paint Semantics

- 기본 paint 채널은 `MaxDistance`로 둔다.
- 저장값은 vertex별 float array로 시작한다.
- runtime에서는 `MaxDistance <= 0`을 fixed particle 또는 inverse mass 0으로 변환한다.
- cloth section 경계는 기본적으로 fixed 또는 낮은 `MaxDistance` paint로 authoring한다. section별 독립 cloth instance 정책 때문에 경계 paint가 없으면 section seam이 벌어질 수 있다.
- 시각화 옵션은 `ViewMin`, `ViewMax`를 먼저 제공한다.
- `bAutoViewRange`는 후순위로 둔다.

### Serialization

- cloth 관련 정보는 skeletal mesh package에 저장한다.
- 기존 skeletal mesh package는 cloth data가 없는 상태로 정상 로드되어야 한다.
- 필요하다면 `EAssetPackageSerializationVersion`에 skeletal cloth payload 버전을 추가한다.
- 이번 NvCloth 도입 과정에서 `EAssetPackageSerializationVersion` 증가는 최대 1회만 허용한다.
- 개발 중 임시 포맷 변경이 필요하더라도 최종 merge 전에는 하나의 version 추가로 압축한다.
- cloth payload는 `USkeletalMesh::SerializeCurrentPayload()`의 version gate 뒤에 두되, 실제 구현은 `SerializeClothPayload()` 같은 내부 sub-payload 함수로 분리한다.
- `SerializeClothPayload()` 호출 여부는 package header version으로 gate한다.
- cloth payload 추가 전에 `USkeletalMesh::SerializeCurrentPayload(FArchive&, uint32 PackageVersion)`처럼 package version을 명시적으로 전달하는 API로 바꾼다.
- 기존 v4~v6 skeletal mesh package가 cloth payload를 읽으려 하지 않도록 load path를 명시적으로 분기한다.
- cloth payload struct는 `FString`/`TArray` 멤버를 포함하므로 각 struct에 명시적인 `operator<<`를 둔다.
- cooked fabric data는 초기에는 저장하지 않고 load/import 시 재생성한다.
- MVP에서는 component local lazy cook 방식을 사용한다.
- MVP 이후 고도화 단계에서는 cooked fabric data를 skeletal mesh package 내부에 저장한다.
- cooked fabric data는 안정화 이후 skeletal mesh package 내부 저장으로 확장할 수 있게 payload slot/header를 예약한다.
- 초기 cloth payload의 cooked fabric slot은 비어 있어도 정상 로드되어야 한다.

### Runtime Tick

- `USkeletalMesh`는 authoring/static data만 소유한다.
- cloth instance와 per-instance simulation state는 component 단위 runtime state로 소유한다.
- `USkeletalMeshComponent`는 cloth runtime state를 직접 멤버로 키우지 않고, component-owned runtime object를 소유한다.
- tick 순서는 animation pose 평가 -> cloth kinematic particle 업데이트 -> cloth simulate -> render data 반영으로 둔다.
- cloth simulation timestep은 component-owned runtime object의 fixed substep accumulator로 관리한다.
- `SolverFrequency`는 fixed substep size를 결정하는 값으로 사용하고, large frame hitch는 max delta clamp로 제한한다.
- cloth 결과를 component-local dynamic vertex data에 반영한 뒤 render upload revision과 world bounds dirty state를 갱신한다.
- editor picking/paint hit test가 skinned vertex data를 사용하는 경로라면 cloth 결과 반영 후 picking data도 같은 vertex data를 보도록 유지한다.
- game runtime에서는 `USkeletalMeshComponent` component tick 흐름에 cloth update를 통합한다.
- MVP에서는 render visibility만으로 cloth simulation을 중단하지 않는다. Component tick이 수행되는 동안 cloth도 계속 simulate한다.
- visibility 기반 pause는 다시 보일 때 cloth state discontinuity를 만들 수 있으므로 후속 최적화 옵션으로 분리한다.
- editor에서는 active tab이 1개라는 전제를 두지 않는다.
- editor document/tab layer가 tick 가능한 document/view 집합을 결정하고, Mesh Editor preview는 그 집합에 포함될 때만 preview scene/component를 tick한다.
- 현재 asset editor tick 흐름이 open editor 전체를 순회할 수 있으므로, implementation phase에서는 tick-eligible document/view set이 preview component tick 전에 실제로 적용되는지 확인한다.
- detached tab, floating window, pinned preview 같은 후속 기능은 tick-eligible document/view set 정책으로 흡수한다.
- cloth runtime은 editor tab/window state를 직접 참조하지 않는다. Editor preview에서 cloth simulation이 돌지 말지는 preview component tick 호출 여부로 결정한다.
- editor cloth simulation play/pause/reset/single-step controls는 MVP 이후 필수 후속 phase로 추가한다.

## Phase Outline

### Phase 1: ThirdParty And Backend Skeleton

NvCloth 1.1.6 ThirdParty 파일 추가와 version compatibility 확인은 이미 완료되었다. 이 phase에서는 해당 dependency를 정식 빌드 설정에 고정하고, CPU factory를 생성/파괴할 수 있는 최소 backend wrapper를 만든다. 실제 skeletal mesh와는 아직 연결하지 않는다. DX11 backend는 컴파일타임 대체 backend로 확장 가능한 interface만 남기고 구현하지 않는다.

Tasks:

1. NvCloth include/lib 경로와 post-build dll copy를 정식 빌드 설정에 연결한다.
2. CPU factory/solver lifecycle을 감싸는 backend skeleton을 만든다.
3. simulation output interface가 CPU position output과 future GPU-resident output을 구분할 수 있게 한다.
4. CUDA 관련 dependency, 분기, 설정이 들어가지 않도록 확인한다.
5. checked-in vcxproj와 `GenerateProjectFiles.py` 양쪽에 NvCloth include/lib/dll copy 설정이 유지되는지 확인한다.
6. 이미 완료된 임시 검증 결과를 기준으로 version compatibility 재조사는 하지 않고, 정식 wrapper 구현 검증에 집중한다.

Validation:

- editor/game target이 NvCloth include/link 상태로 빌드된다.
- CPU backend wrapper가 초기화/종료 lifecycle을 통과한다.
- backend output interface가 CPU position output과 future GPU-resident output을 구분할 수 있다.
- engine `FVector`와 NvCloth/PhysX vector 사이 handedness, axis, unit scale 변환이 기존 PhysX 변환 정책과 일치한다.
- NvCloth allocator/error callback/factory/solver/fabric/cloth release 순서가 shutdown, component destroy, mesh 변경, editor preview close에서 안정적으로 동작한다.
- NvCloth CPU factory, fabric cooking, solver simulation, particle readback 중 하나라도 실패하면 ThirdParty 배치 문제가 아니라 정식 wrapper 구현 문제로 우선 분류한다.

### Phase 2: Skeletal Mesh Cloth Data Model

`USkeletalMesh`가 LOD별 cloth 대상 material section, vertex paint data, default simulation config를 표현할 수 있게 한다. 이 단계의 핵심은 simulation보다 data shape을 안정화하는 것이다.

주의: 아래 자료구조 방향은 Phase 1 backend skeleton이 아직 구현되지 않은 상태에서 세운 잠정 설계이다. Phase 1 완료 후 NvCloth wrapper/resource ownership과 serialization 제약을 확인하고, 필요한 경우 이름과 세부 필드는 조정한다. 단, `FSkeletalMesh` payload 내부에 cloth authoring data를 둔다는 방향은 현재 기본안으로 유지한다.

Data candidates:

- cloth data array
- LOD별 target material section/range binding
- per-vertex `MaxDistance` paint array
- cloth-local particle order paint data
- render vertex index와 cloth particle index 사이의 explicit mapping
- cloth-local triangle index buffer 또는 fabric cooking용 topology payload
- section index range에서 생성한 unique render vertex subset mapping
- source vertex/index/section signature 또는 stale detection key
- default solver/config parameters
- reserved cooked fabric payload slot/header
- future backend extension fields

Validation:

- LOD별 cloth data가 서로 독립적으로 생성/수정된다.
- 하나의 cloth data가 하나의 material section range만 참조한다.
- cloth data가 없는 skeletal mesh도 기존 경로로 동작한다.
- cloth payload가 `FSkeletalMesh`의 vertex/index/section 변경에 대해 stale 상태를 감지한다.
- section index가 stale 되었을 때 `MaterialSlotName`, `FirstIndex`, `IndexCount` 기반 검증 또는 재매칭 경로가 존재한다.
- reimport 후 section signature 또는 rematch가 성공하면 paint/config를 보존하고, 실패하면 cloth data를 stale/disabled 상태로 표시해 사용자가 다시 authoring하도록 한다.
- section index range에서 뽑은 cloth-local vertex/index mapping이 duplicated seam vertex와 shared render vertex를 안정적으로 처리한다.
- invalid/degenerate triangle이 fabric cooking 전에 제거되거나 명확한 에러로 보고된다.

### Phase 3: Runtime Simulation MVP

한 개 skeletal mesh component에서 cloth 대상 material section의 vertex를 NvCloth particle로 만들고, CPU simulation 결과를 화면에 반영한다. 충돌과 복잡한 editor authoring은 제외한다.

Tasks:

1. component-owned cloth runtime object를 추가한다.
2. section별 cloth instance를 생성/해제한다.
3. animation pose 평가 이후 cloth kinematic particle을 갱신한다.
4. fixed substep accumulator와 max delta clamp로 NvCloth simulation timestep을 안정화한다.
5. CPU simulation 결과를 component별 dynamic vertex buffer 또는 동등한 per-instance render data에 반영한다.
6. cloth 결과 반영 후 skinned revision, world bounds dirty, editor picking data 경로가 함께 갱신되는지 연결한다.
7. cloth section triangle 기준으로 normal/tangent를 재계산해 dynamic vertex data에 반영한다.
8. render proxy가 cloth-enabled section에 대해 per-instance render data를 사용할 수 있게 한다.
9. GPU skinning mode에서는 MVP cloth simulation이 비활성화되고 기존 skeletal mesh render path로 fallback되는지 확인한다.
10. CPU skinning mode에서 GPU skinning mode로 전환되면 cloth runtime을 release하고, CPU skinning mode로 돌아올 때 reset/recreate한다.
11. mesh 변경, cloth payload 변경, material section 변경, skinning mode 변경, LOD 변경에 대한 cloth runtime rebuild trigger를 연결한다.
12. Phase 5 editor paint 이전에도 runtime 검증이 가능하도록 임시 debug/bootstrap command로 section cloth data와 기본 paint를 생성하는 경로를 둔다.
13. render visibility만으로 simulation을 pause하지 않는다.

Validation:

- animation pose가 바뀐 뒤 cloth가 따라간다.
- 고정점 paint가 적용된다.
- fixed substep accumulator와 max delta clamp가 적용되어 frame spike에서도 simulation step이 폭주하지 않는다.
- cloth 비활성화 시 기존 skeletal render path로 fallback된다.
- `USkeletalMesh` 원본 render buffer가 per-instance cloth 결과로 오염되지 않는다.
- component별 cloth dynamic vertex buffer 또는 동등한 per-instance render data가 정상 갱신된다.
- cloth section의 normal/tangent가 deformed triangle 기준으로 갱신되어 기본 lighting이 깨지지 않는다.
- CPU skinning mode에서만 cloth simulation이 활성화된다.
- GPU skinning mode에서 morph target 때문에 CPU skinned cache가 존재해도 cloth simulation은 활성화되지 않는다.
- GPU skinning mode에서는 cloth simulation이 실행되지 않고 기존 skeletal mesh rendering이 유지된다.
- skinning mode가 CPU -> GPU -> CPU로 바뀌면 cloth simulation state가 재개되지 않고 reset/recreate된다.
- mesh/material section/cloth payload/LOD 변경 시 stale cloth runtime이 남지 않는다.
- cloth 결과가 component bounds/culling과 editor picking 또는 paint hit test에 반영된다.
- section별 cloth instance가 독립적으로 생성/해제된다.
- component destroy/mesh 변경 시 cloth runtime resource가 누수 없이 정리된다.

### Phase 4: Serialization And Reload

cloth section mapping, paint data, config를 skeletal mesh package에 저장/로드한다. 기존 package는 cloth data 없이 계속 로드되어야 한다. 이 도입 과정에서 package serialization version 증가는 최대 1회로 제한한다.

Tasks:

1. skeletal cloth payload version gate를 추가한다.
2. cloth payload serialization을 `SerializeClothPayload()` 같은 내부 sub-payload 함수로 분리한다.
3. `USkeletalMesh::SerializeCurrentPayload(FArchive&, uint32 PackageVersion)` 형태로 package header version을 cloth payload gate에 명시적으로 전달한다.
4. cloth section mapping, paint data, config를 저장/로드한다.
5. cooked fabric payload slot/header를 예약하되 실제 cooked data는 비워둔다.
6. cloth data가 없는 legacy package의 default load path를 유지한다.

Validation:

- 저장 후 editor 재시작 또는 asset reload에서 cloth 설정이 유지된다.
- package version 증가가 다른 asset type 로드에 영향을 주지 않는다.
- cloth data가 없는 legacy skeletal mesh는 기본값으로 로드된다.
- v4~v6 skeletal mesh package가 cloth payload를 읽으려 하지 않는다.
- NvCloth 도입 관련 `EAssetPackageSerializationVersion` 추가가 1개 이하임을 확인한다.
- cloth serialization 구현이 `SerializeClothPayload()` 같은 내부 sub-payload 함수로 분리되어 있다.
- 초기 cooked fabric payload는 비어 있어도 정상 로드되며, load/import 시 재생성된다.

### Phase 5: Editor Visualization And Basic Paint

Mesh Editor에서 cloth target section과 paint value를 확인하고 수정할 수 있게 한다.

Initial scope:

- cloth section enable/disable
- LOD별 cloth data 선택/편집
- `MaxDistance` heatmap visualization
- `ViewMin`, `ViewMax`
- brush paint
- optional smoothing

Deferred editor authoring:

- `bAutoViewRange`
- flood fill
- mirror paint
- paint preset
- advanced LOD paint tools
- cloth simulation play/pause/reset/single-step controls

Validation:

- 현재 편집 중인 LOD가 UI에서 명확히 드러난다.
- LOD0의 cloth/paint 수정이 LOD1 이상에 자동 전파되지 않는다.
- paint 값 변경이 runtime fixed particle 또는 movement range에 반영된다.
- heatmap visualization이 `ViewMin`, `ViewMax` 기준으로 표시된다.

## Deferred Collision Re-Planning

Collision은 이번 MVP의 구현 범위가 아니다. MVP 완료 후 main 브랜치의 physics asset 및 world collision 기능 상태를 다시 확인하고, cloth collision을 별도 계획으로 재검토한다. 이 섹션은 현재 확정 구현 계획이 아니라 임시 방향성이다.

Collision phase는 GPU skinning cloth support 이후에 진행한다. GPU skinning 지원이 section-level render data, shader/vertex factory 선택, shadow/predepth/selection pass, bounds 반영 같은 cloth 렌더 기반을 더 크게 바꾸기 때문에, collision은 그 기반이 안정화된 뒤 얹는다.

현재 엔진의 physics shape는 sphere, capsule, box 3종류를 기준으로 한다. NvCloth는 sphere/capsule/plane/convex 계열 collision을 지원하므로, box는 plane 기반 convex로 변환하는 방향을 검토할 수 있다. 다만 NvCloth의 local space 기반 simulation, primitive budget, world shape 후보 추출 문제 때문에 world collision은 MVP 이후 별도 설계가 필요하다.

임시 후보 단계:

- Collision Phase 1: physics asset only, sphere/capsule 우선.
- Collision Phase 2: physics asset box support, box를 plane 기반 convex로 변환.
- Collision Phase 3: world collision static opt-in, cloth bounds 주변 static shape만 제한적으로 사용.
- Collision Phase 4: world dynamic collision, moving shape/teleport/velocity handling 포함.

예상 리스크:

- world shape는 cloth instance 기준 local space로 매 프레임 변환해야 한다.
- world collision 후보를 cloth마다 제한하지 않으면 query/transform 비용과 primitive budget이 빠르게 커진다.
- box는 plane 기반 convex로 표현 가능하지만 plane budget을 많이 사용한다.
- 후보 shape가 갑자기 추가/제거되면 cloth popping이 발생할 수 있다.
- dynamic world shape는 previous/current transform, velocity, teleport handling을 별도로 고려해야 한다.

## Main Risks

- skeletal mesh render vertex와 NvCloth particle을 어떻게 안정적으로 매핑할지 초기에 잘못 정하면 이후 paint와 serialization이 모두 흔들릴 수 있다.
- GPU skinning mode는 MVP cloth 지원 범위가 아니므로, editor/runtime에서 비활성 상태를 명확히 드러내지 않으면 사용자가 cloth 설정이 고장난 것으로 오해할 수 있다.
- package version 증가는 여러 asset load/save 경로에 영향을 줄 수 있으므로, skeletal mesh payload에만 좁게 게이트해야 한다.
- `SerializeCurrentPayload()`가 package version을 모르는 상태에서 cloth payload를 추가하면 기존 v4~v6 skeletal mesh package load가 깨질 수 있다.
- LOD별 cloth data를 독립 저장하므로, editor UI가 현재 편집 중인 LOD를 명확히 표시하지 않으면 LOD0 수정이 다른 LOD에 적용된다는 오해가 생길 수 있다.
- CPU simulation 결과를 component별 cloth dynamic vertex buffer 또는 per-instance render data에 반영하는 위치가 명확하지 않으면 animation, bounds, render proxy update가 서로 꼬일 수 있다.
- component-local simulation에서 gravity를 component-local로 변환하지 않으면 캐릭터 회전 시 천이 잘못된 방향으로 떨어진다.
- GPU skinning mode에서 morph target 등으로 CPU skinned cache가 존재하는 경우, cloth 활성 조건을 cache 존재 여부로 잡으면 MVP 지원 범위를 넘어 cloth가 실행될 수 있다.
- skinning mode 전환 시 기존 cloth simulation state를 재개하면 animation pose와 cloth particle state가 벌어져 popping 또는 파고듦이 발생할 수 있다.
- cloth result 반영 후 bounds/picking 갱신을 빠뜨리면 렌더링은 보여도 culling, selection, paint brush hit test가 이전 pose를 기준으로 동작할 수 있다.
- NvCloth/PhysX vector 변환의 axis/handedness/unit scale이 엔진 좌표계와 어긋나면 gravity, normal, collision 후보 확장까지 모두 잘못된다.
- normal은 cloth triangle 기준으로 갱신할 수 있지만 tangent는 UV, mirrored UV, handedness 영향을 받으므로 기존 tangent builder 유틸 재사용 또는 동일 로직 복제가 필요하다.
- mesh 변경, material section 변경, cloth payload 변경, LOD 변경에 대한 runtime rebuild trigger가 빠지면 stale fabric/cloth/mapping이 남을 수 있다.
- NvCloth runtime object release 순서가 꼬이면 component destroy, editor preview close, shutdown 시 누수나 crash가 발생할 수 있다.
- section별 독립 cloth instance는 section 경계 paint가 부족하면 seam 벌어짐이나 경계 떨림을 만들 수 있다.
- timestep을 raw frame delta에 직접 묶으면 FPS와 hitch에 따라 cloth 안정성과 감각이 크게 달라질 수 있다.
- render visibility만으로 cloth simulation을 pause하면 다시 보일 때 cloth state discontinuity가 발생할 수 있다. MVP에서는 visibility pause를 하지 않고, 후속 최적화 옵션으로만 다룬다.
- editor tick policy가 active tab 1개를 전제하면 detached/floating/pinned preview 기능을 추가할 때 cloth preview tick 정책이 다시 깨질 수 있다.
- DX11 backend 가능성을 열어두더라도 MVP 코드가 CPU concrete type에 직접 의존하면 나중에 컴파일타임 backend 전환 비용이 커진다.
- backend output interface가 CPU position 반환만 전제하면 DX11 backend 도입 시 GPU simulation result를 CPU로 readback했다가 다시 GPU로 올리는 비효율적 경로에 묶일 수 있다.

## Deferred GPU Skinning Cloth Support

MVP는 CPU skinning mode에서만 cloth simulation을 지원한다. GPU skinning mode를 선택한 상태에서는 cloth simulation을 실행하지 않고 기존 skeletal mesh render path를 유지한다. 이는 cloth가 있는 component의 skinning mode를 runtime에서 암묵적으로 강제 전환하지 않기 위한 정책이다. GPU skinning cloth support는 이번 MVP 구현에 포함하지 않는 별도 후속 phase다.

후속 작업 순서는 GPU skinning cloth support를 collision보다 먼저 진행한다. GPU skinning 지원은 cloth result가 모든 주요 render pass에 안정적으로 반영되는 기반 작업이고, collision은 그 기반 위에 얹는 기능으로 둔다.

GPU skinning mode에서 cloth simulation을 지원하려면 다음 항목을 별도 고도화 phase에서 함께 설계한다.

- cloth-enabled section만 section-level per-instance buffer를 사용할지, component 전체를 dynamic buffer path로 전환할지 결정한다.
- section별 GPU skinning on/off, vertex factory type, shader permutation 선택을 command build 단계에서 표현한다.
- compact cloth-local index buffer를 쓸 경우 `FMeshSectionDraw::FirstIndex`와 `BufferOverride`의 index offset 정책을 정리한다.
- PreDepth, ShadowMap, SelectionMask 등 모든 skeletal render pass에서 cloth 결과가 동일하게 반영되게 한다.
- cloth result bounds가 culling과 shadow clipping에 반영되게 한다.

## Temporary Debug Authoring Policy

Phase 3의 임시 debug/bootstrap command는 runtime 검증용 cloth data와 기본 paint를 만들기 위한 editor-only 개발 도구로 둔다. Phase 5의 정식 Mesh Editor authoring 경로가 들어온 뒤에는 유지 여부를 다시 판단한다. 유지할 경우 일반 사용자 workflow가 아니라 dev/debug command로 명확히 분리한다.
