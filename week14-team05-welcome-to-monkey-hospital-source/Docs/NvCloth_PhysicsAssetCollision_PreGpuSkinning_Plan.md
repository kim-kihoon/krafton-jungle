# NvCloth Physics Asset Collision Pre-GPU Skinning Plan

## Context

현재 cloth runtime은 CPU skinning 기반의 component-local NvCloth simulation으로 동작한다. `FSkeletalClothRuntime::Tick`은 animation pose 평가 뒤 component-local skinned vertices를 받아 NvCloth particle을 갱신하고, world gravity/wind는 component-local vector로 변환해서 적용한다.

기존 `Docs/NvCloth_Integration_AbstractPlan.md`에서는 collision을 MVP에서 명시적으로 제외했고, physics asset/world collision 상태를 확인한 뒤 별도 계획으로 재검토하기로 했다. 이 문서는 그중 GPU skinning cloth support 전에 구현할 physics asset collision 범위만 다룬다.

이 문서의 목적은 현재 CPU cloth runtime 위에 skeletal mesh physics asset collision을 붙여, GPU skinning support 이전에도 skeletal mesh에 필요한 기본 cloth collision 기능을 완성하는 것이다.

## Goals

- skeletal mesh의 effective physics asset에 포함된 sphere, capsule, box와 cloth가 충돌하게 한다.
- component-local cloth simulation 정책을 유지하면서 physics asset collision primitive만 cloth component-local space로 변환한다.
- physics asset collision은 cloth section config에 저장된 body/shape participation filter로 필요한 collider만 참여시킨다.
- sphere/capsule은 NvCloth sphere/capsule API로 직접 반영한다.
- box는 NvCloth plane/convex API로 변환한다.
- collision 후보가 budget을 초과하면 deterministic truncation과 debug reason을 남긴다.
- physics asset collision gather/scoring/upload 구조는 이후 world collision에서도 재사용할 수 있게 만든다.

## No Goals

- GPU skinning cloth support는 이 문서 범위에 포함하지 않는다.
- world static/dynamic collision은 이 문서 범위에 포함하지 않는다. GPU skinning support 이후 별도 문서에서 진행한다.
- self collision, cloth-to-cloth collision, tearing은 포함하지 않는다.
- triangle mesh collider나 arbitrary convex mesh collider는 초기 범위에 포함하지 않는다.
- gameplay physics response를 cloth가 rigidbody에 다시 전달하는 two-way coupling은 구현하지 않는다.
- collision event, hit callback, overlap callback을 cloth에서 발생시키지 않는다.
- non-uniform scale collision을 지원하지 않는다. 기존 cloth runtime scale 정책처럼 reset 또는 skip 대상으로 둔다.

## Implementation Policy

### Ownership

- collision 후보 수집, scoring, budget truncation은 별도 `FClothCollisionGatherer` 계층이 담당한다.
- `USkeletalMeshComponent`는 skeletal mesh, component transform, current bone pose, effective physics asset 입력을 제공한다.
- `FSkeletalClothRuntime`은 gather 결과의 selected primitive를 NvCloth에 upload하고 simulation을 수행한다.
- `FSkeletalClothRuntime`은 physics asset candidate 수집 로직을 직접 소유하지 않는다.
- `FClothCollisionCandidate`는 gather/scoring/debug 전용 frame-local transient 구조체다. asset payload에 저장하지 않는다.
- `FClothCollisionPrimitiveSet`은 selected candidate만 변환한 순수 geometry 구조체다. NvCloth upload는 이 구조만 소비한다.

### Authoring And Config

- physics asset collision enable은 cloth asset/section config로 저장한다.
- 저장 위치는 현재 cloth ownership 기준으로 `FSkeletalClothConfig` 또는 section-level cloth payload 확장이다.
- physics asset body/shape participation filter도 cloth section config에 저장한다.
- physics asset body/shape filter는 cloth authoring 데이터다. gameplay/ragdoll용 `UPhysicsAsset` 자체를 cloth-only 설정으로 오염시키지 않는다.
- body/shape participation filter 기본값은 authoring 편의를 위해 전체 참여로 시작할 수 있지만, 실제 캐릭터 cloth에서는 필요한 body/shape만 include하도록 Mesh Editor에서 편집 가능해야 한다.

### Source Pipeline

- gather 이후에는 `FClothCollisionCandidate` 배열로 후보를 표현한다.
- selected candidate만 `FClothCollisionPrimitiveSet`으로 변환한다.
- NvCloth에 넣기 직전에는 sphere/capsule/box-derived convex upload path를 사용한다.
- physics asset sphere/capsule collision을 먼저 구현하고, box convex collision은 그 다음에 붙인다.
- world collision에서 재사용할 수 있도록 candidate source id, score, selected/truncated/skipped state를 Phase 0부터 유지한다.

### Debug Policy

- immediate debug draw는 `FClothCollisionGatherResult::Candidates`를 소비한다.
- NvCloth upload는 `FClothCollisionGatherResult::SelectedPrimitives`만 소비한다.
- Phase 1부터 gathered/selected/skipped/truncated candidate count를 source/type별로 기록한다.
- physics asset body/shape participation filter로 빠진 candidate와 invalid transform/scale로 skip된 candidate는 debug reason을 남긴다.

## Data Shape

Runtime geometry structures:

```cpp
enum class EClothCollisionSource : uint8
{
    PhysicsAsset,
    WorldStatic,
    WorldDynamic
};

struct FClothCollisionSphere
{
    FVector LocalCenter;
    float Radius = 0.0f;
};

struct FClothCollisionCapsule
{
    FVector LocalPoint0;
    FVector LocalPoint1;
    float Radius = 0.0f;
};

struct FClothCollisionBox
{
    FTransform LocalTransform;
    FVector HalfExtent = FVector::ZeroVector;
};

struct FClothCollisionPrimitiveSet
{
    TArray<FClothCollisionSphere> Spheres;
    TArray<FClothCollisionCapsule> Capsules;
    TArray<FClothCollisionBox> Boxes;
};
```

Gather/scoring/debug transient structures:

```cpp
enum class EClothCollisionPrimitiveType : uint8
{
    Sphere,
    Capsule,
    Box
};

enum class EClothCollisionSelectState : uint8
{
    Gathered,
    Selected,
    RejectedByParticipationFilter,
    TruncatedByBudget,
    SkippedInvalidTransform,
    SkippedNonUniformScale,
    SkippedFilter
};

struct FClothCollisionSourceId
{
    EClothCollisionSource Source = EClothCollisionSource::PhysicsAsset;
    uint32 OwnerActorId = 0;
    uint32 OwnerComponentId = 0;
    FName BoneName = FName::None;
    int32 BodyIndex = -1;
    int32 ShapeIndex = -1;
};

struct FClothCollisionCandidate
{
    EClothCollisionPrimitiveType Type = EClothCollisionPrimitiveType::Sphere;
    FClothCollisionSourceId SourceId;

    FTransform LocalTransform;
    FVector HalfExtent = FVector::ZeroVector;
    float Radius = 0.0f;
    float CapsuleHalfHeight = 0.0f;

    FBoundingBox WorldBounds;

    int32 TypeCost = 1;
    uint64 StableTieBreaker = 0;

    EClothCollisionSelectState State = EClothCollisionSelectState::Gathered;
};

struct FClothCollisionGatherResult
{
    TArray<FClothCollisionCandidate> Candidates;
    FClothCollisionPrimitiveSet SelectedPrimitives;
};
```

NvCloth upload transient buffer:

```cpp
struct FNvClothCollisionUploadBuffer
{
    TArray<physx::PxVec4> Spheres;
    TArray<uint32_t> CapsuleSphereIndices;
    TArray<physx::PxVec4> Planes;
    TArray<uint32_t> ConvexMasks;
};
```

## Space Policy

- cloth particle position은 계속 component-local skinned space다.
- physics asset body shape는 bone component-space transform과 body/shape local transform을 합성해 component-local collision primitive로 만든다.
- position 변환과 vector 변환을 분리한다.
- sphere radius와 capsule radius는 uniform scale만 허용한다.
- box는 local rotation과 half extent를 유지한 뒤 plane/convex 변환 단계에서 6개 plane으로 변환한다.
- non-uniform scaled collider는 skip하고 debug log/stat로 표시한다.
- physics shape 단위와 cloth simulation 단위는 NvCloth upload 직전까지 동일한 meter 기준으로 유지한다.

## NvCloth Mapping Policy

- sphere는 `Cloth::setSpheres`에 `PxVec4(center.x, center.y, center.z, radius)`로 넣는다.
- capsule은 NvCloth sphere 두 개를 endpoint로 추가하고 `Cloth::setCapsules`에 sphere index pair를 넣는다.
- capsule endpoint sphere는 capsule마다 독립 생성한다. sphere reuse 최적화는 초기 목표가 아니다.
- box는 6개 plane을 만들고 `Cloth::setPlanes`와 `Cloth::setConvexes`를 사용한다.
- convex mask는 box 1개당 plane 6개를 묶는 bitmask로 만든다.
- NvCloth convex mask는 plane index bitmask이므로 plane count budget을 명시적으로 제한한다.
- 기존 primitive 수가 줄어든 frame에는 NvCloth의 기존 range를 clear해야 한다. stale sphere/capsule/plane/convex가 남으면 안 된다.

## Collision Budget

초기 기본값:

- max authored spheres: 32
- max authored capsules: 16
- max authored boxes: 5
- max NvCloth planes: 32
- max NvCloth convexes: 5

Capsule은 upload 단계에서 endpoint sphere 2개를 추가로 사용한다. `max authored spheres`는 source primitive budget이고, 실제 `setSpheres`에 들어가는 sphere 수는 authored sphere 수와 capsule endpoint sphere 수의 합이다.

Box는 1개당 plane 6개를 사용한다. NvCloth convex mask가 32-bit plane bitmask이므로 초기 구현에서는 independent box 5개, plane 30개를 상한으로 둔다.

Physics asset 후보는 cloth section config의 body/shape participation filter를 먼저 적용한 뒤 budget selection에 들어간다. 한 cloth section의 physics asset budget이 다른 section의 budget을 밀어내지 않도록 section별로 selection/truncation한다.

## Phase 0: Current Code Contract And Instrumentation

### Goal

physics asset collision 구현 전에 runtime data contract, gatherer ownership, debug stat, empty-collision behavior를 고정한다.

### Tasks

- `ClothCollisionTypes.h` 또는 동등한 runtime-only header에 `FClothCollisionCandidate`, `FClothCollisionGatherResult`, `FClothCollisionPrimitiveSet`, budget/debug stat 구조를 추가한다.
- `FClothCollisionGatherer`를 추가하고 physics asset source gather, section-level budget truncation을 이 계층에 둔다.
- `USkeletalMeshComponent::TickClothSimulation`에서 gatherer에 필요한 mesh/component/bone pose 입력을 구성한다.
- `FSkeletalClothRuntime::Tick`은 force context와 별개로 selected collision primitive view를 받는다.
- NvCloth collision upload helper는 runtime-side helper로 둔다. 첫 구현에서는 `SkeletalClothRuntime.cpp` 내부 helper로 시작하고, Phase 2 이후 복잡해지면 별도 `ClothCollisionUpload.*`로 분리한다.
- debug stat: gathered spheres/capsules/boxes, selected spheres/capsules/boxes, uploaded spheres/capsules/planes/convexes, skipped non-uniform scale, rejected count, truncated count를 만든다.

### Validation

- collision context가 비어 있으면 기존 cloth simulation 결과가 달라지지 않는다.
- cloth runtime reset/rebuild 시 collision buffer도 같이 초기화된다.
- GPU skinning mode에서는 기존처럼 cloth runtime이 실행되지 않는다.
- gather result의 `Candidates`는 immediate debug draw에 사용할 수 있고, `SelectedPrimitives`는 NvCloth upload에만 사용된다.

## Phase 1: Physics Asset Sphere/Capsule Collision

### Goal

skeletal mesh의 effective physics asset에 포함된 selected sphere/capsule과 cloth가 충돌한다.

### Tasks

- cloth section config에 physics asset collision enable과 body/shape participation filter를 추가한다.
- `FClothCollisionGatherer`에서 `USkeletalMeshComponent`의 effective physics asset과 current bone component-space transforms를 사용해 physics asset body별 component-space bone transform을 만든다.
- `FPhysicsAssetBodySetup::BodyLocalFrame`과 `FPhysicsAssetShapeSetup::LocalTransform` 합성 규칙을 `PhysicsAssetInstance`/`PhysicsAssetPreviewUtils`의 creation/preview path와 동일하게 맞춘다.
- participation filter를 통과한 sphere shape를 component-local center/radius candidate로 수집한다.
- participation filter를 통과한 capsule shape를 component-local endpoint/radius candidate로 수집한다.
- selected sphere/capsule candidate를 `FClothCollisionPrimitiveSet`으로 변환하고 각 section cloth에 업로드한다.
- radius가 0 이하이거나 transform이 invalid한 shape는 skip하고 candidate state/debug stat에 reason을 남긴다.

### Validation

- character limb에 붙은 sphere/capsule이 cloth particle을 밀어낸다.
- animation pose가 움직이면 collision primitive도 같은 frame에 따라 움직인다.
- physics asset이 없으면 기존 cloth simulation과 동일하게 동작한다.
- body에 box만 있는 physics asset은 Phase 1에서 skip되며 crash하지 않는다.
- body/shape participation filter를 끄면 해당 collider가 cloth collision 후보에서 빠진다.

## Phase 2: Physics Asset Box Collision

### Goal

physics asset box shape를 NvCloth convex collision으로 변환해 cloth와 충돌하게 한다.

### Tasks

- participation filter를 통과한 component-local oriented box candidate를 수집한다.
- component-local oriented box에서 6개 plane을 생성한다.
- plane normal은 normalized local axis를 사용한다.
- plane equation은 `dot(N, X) + d = 0` 형식으로 만들고, box 내부를 올바르게 표현하는 sign을 검증한다.
- box 1개당 convex mask 1개를 생성한다.
- plane count budget을 초과하면 deterministic tie-breaker 기준으로 일부 box를 truncate한다.
- sphere/capsule과 box가 함께 있을 때 clear/update 순서가 stale primitive를 남기지 않게 한다.

### Validation

- cloth가 oriented box의 6면 바깥으로 밀려난다.
- 회전한 box collider도 시각화와 충돌 방향이 일치한다.
- box 수가 budget을 초과해도 deterministic하게 유지된다.
- sphere/capsule collision 결과가 box 추가 후 퇴행하지 않는다.

## Risks

- box plane sign이 틀리면 cloth가 box 안쪽으로 빨려 들어가거나 모든 방향에서 밀려날 수 있다.
- NvCloth plane/convex budget은 bitmask 구조 때문에 단순 primitive count보다 제약이 강하다.
- physics asset transform 합성 규칙이 ragdoll body creation/preview path와 달라지면 preview와 runtime 충돌 위치가 어긋난다.
- physics asset capsule axis가 PhysX shape local pose 보정과 다르게 해석되면 capsule 충돌 위치가 debug draw와 달라질 수 있다.
- non-uniform scale을 조용히 받아들이면 sphere/capsule radius와 box plane이 실제 physics shape와 달라진다.
- physics shape 단위와 cloth simulation 단위가 어긋나면 collision radius/extent가 100배 차이 나는 식의 치명적인 스케일 문제가 생길 수 있다.

## Recommended First Implementation Slice

첫 구현은 Phase 0과 Phase 1만 묶는다.

- `ClothCollisionTypes.h` 또는 동등한 runtime-only header 추가
- `FClothCollisionGatherer` 추가
- cloth section config에 physics asset collision enable과 body/shape participation filter 추가
- `USkeletalMeshComponent`에서 gatherer 입력 구성
- `FSkeletalClothRuntime::Tick`에 selected collision primitive 입력 연결
- NvCloth `setSpheres` / `setCapsules` upload
- debug count와 empty-context no-regression 확인

이 slice는 world query API나 box convex 변환을 건드리지 않고도 가장 중요한 목표인 skeletal mesh physics asset collision을 검증할 수 있다. Phase 1이 안정되면 box plane/convex 변환을 붙이고, 그 다음 GPU skinning cloth support로 넘어간다.
