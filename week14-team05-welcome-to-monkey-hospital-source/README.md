# Week 12 작업 설명문서 — 파티클 시스템 · 반투명 렌더링 · 머티리얼 시스템

## 1. 개요

### 1.1 작업 목표
자체 게임 엔진(KraftonEngine)에 **파티클 시스템**을 신규 구축하고, 이를 올바르게 출력하기 위한 **반투명(Translucent) 렌더링 패스**와 **머티리얼 시스템**을 함께 정비했다. 불·연기·스파크 같은 비정형 효과를 다량의 입자로 시뮬레이션·렌더링하고, 반투명 물체가 올바른 순서로 합성되도록 하는 것이 핵심 목표였다.

### 1.2 역할 분담 (3인)
| 파트 | 담당 영역 |
|---|---|
| P1 | 파티클 시뮬레이션 · 데이터 마샬링(GT→RT) · 충돌 |
| P2 | 파티클 모듈 · Distribution · 파티클/머티리얼 에디터 |
| P3 | 파티클 렌더 파이프라인 · 반투명 패스 · 머티리얼 렌더 연동 |

### 1.3 핵심 성과 요약
- **파티클 시스템**: System–Emitter–LODLevel–Module 계층 구조, 게임/렌더 스레드 분리 파이프라인, Sprite·Mesh·Beam·Ribbon 4종 타입, GPU 인스턴싱, LOD·충돌·관찰성(stat) 구현.
- **반투명 렌더링**: 통합 TranslucentPass와 카메라 거리 기반 back-to-front 정렬, 블렌드 모드(AlphaBlend/Additive/Modulate) 지원.
- **머티리얼 시스템**: Domain/BlendMode 단일 소스에서 렌더 상태를 도출하는 UE 스타일 구조, 셰이더 디커플링, `.uasset` 바이너리 직렬화, 머티리얼 에디터.

---

## 2. 파티클 시스템

### 2.1 아키텍처 — 데이터 구조
파티클 에셋은 4계층으로 구성된다.

- **UParticleSystem** (`.uasset` 에셋): 여러 Emitter의 묶음. Looping·거리 기반 LOD 설정 보유.
- **UParticleEmitter**: 하나의 효과 요소(예: 불꽃/연기). 거리별 품질 단계인 **LODLevels[]** 를 가진다.
- **UParticleLODLevel**: 한 LOD 단계의 모듈 묶음. 네 종류로 구성된다.
  - `RequiredModule` (필수·1개): Material, SubImages, SortMode, ScreenAlignment 등
  - `SpawnModule` (필수·1개): Rate/RateScale, BurstList
  - `TypeDataModule` (택1, 없으면 Sprite): Sprite / Mesh / Beam / Ribbon
  - `Modules[]` (0..N, 자유): Lifetime, Location, Velocity, Acceleration, Color, Size, SubUV, Collision, EventGenerator 등
- 각 모듈의 수치는 **Distribution**(Constant / Uniform / Curve)으로 제어한다.

런타임에서는 `AParticleSystemActor → UParticleSystemComponent(PSC) → UParticleSystem(Template)` 을 참조하고, 실제 시뮬레이션은 `FParticleEmitterInstance` 가 담당한다. 즉 **에셋은 설계도, 인스턴스는 실행**이다.

### 2.2 시뮬레이션 파이프라인 (GT → RT)
한 프레임의 흐름은 게임 스레드(GT)와 렌더 스레드(RT)로 분리된다.

1. **게임 스레드**: `UParticleSystemComponent::TickComponent` 가 TickInterval 단위로 누적해 `FParticleEmitterInstance::Tick` 을 호출한다. Tick은 substep 루프 안에서 **Spawn → Update(모듈 적용·충돌·수명/압축)** 단계를 수행한다.
2. **마샬링**: `BuildDynamicData()` 가 활성 입자를 **SnapshotStorage**(Replay 데이터)로 복사해 SceneProxy에 전달한다(`SetDynamicData`). 시뮬레이션 권한은 GT가 갖고(authoritative), RT는 받은 스냅샷만 방어적으로 소비한다(defensive)는 계약을 코드 주석으로 명시했다.
3. **렌더 스레드**: SceneProxy의 `PrepareDrawBuffer` 가 emitter별 `VertexFactory::BuildDraw` 를 호출해 정점·인덱스를 만들고, `FMeshSectionDraw` + `BufferOverride` 로 DrawCommand를 생성한다.

입자 데이터는 **RuntimeStorage**라는 단일 블록에 stride 간격으로 저장하고 모듈 payload도 같은 블록에 offset으로 배치해 할당을 최소화하고 캐시 효율을 높였다. 죽은 입자는 살아있는 것만 앞으로 당겨 압축(compaction)한다.

### 2.3 모듈 시스템
모듈을 조립해 다양한 효과를 만든다.
- **Spawn**: 초당 방출량(Rate)과 순간 방출(Burst).
- **Distribution**: Constant/Uniform/Curve로 값을 시간/난수 기반으로 제어(에디터에서 커브 편집 지원).
- **LOD**: 거리별 단계 전환, hysteresis·switch delay로 경계 깜빡임 억제, 하위 LOD 비용 감쇠.
- **Collision**: CPU 기반 입자 충돌, bounce/tangential damping, 예산(budget) 기반 우선순위 검사.
- 그 외 Lifetime/Location/Velocity/Acceleration/Color/Size/SubUV/EventGenerator.

### 2.4 렌더링 — VertexFactory & 타입
`FParticleVertexFactory::BuildDraw` 추상 인터페이스 아래 **Sprite·Mesh·Beam·Ribbon** 4종을 같은 방식으로 처리한다.
- **Sprite**: 카메라를 향하는 빌보드(ScreenAlignment 4모드).
- **Mesh**: StaticMesh를 입자마다 인스턴싱.
- **Beam / Ribbon**: source–target tessellation / 시간순 strip.

per-instance 버퍼(transform·color·sub-image)와 정적 geometry(quad/mesh)를 조합해 **`DrawIndexedInstanced` 로 다수 입자를 한 번의 드로우콜에** 그린다. 파티클은 별도 경로가 아니라 엔진의 일반 DrawCommand 파이프라인(`FMeshSectionDraw`)을 재사용한다.

### 2.5 최적화 & 관찰성
- **렌더**: GPU 인스턴싱(N개 → 1 드로우콜), frustum culling(화면 밖 컬링), 반투명 파티클 그림자 캐스팅 제외(중복 드로우 제거), 조건부 정렬(AlphaBlend일 때만 정렬).
- **시뮬**: LOD 비용 감쇠, TickInterval로 호출 빈도 절감, Collision 예산화(coarse pruning + nearby-collider preselection cache + 우선순위 선택 + LOD-aware 상한).
- **관찰성**: `stat particles` 오버레이로 입자 수·메모리·드로우콜·정점/인덱스를 단계별 집계, ShowFlags(bParticles/bParticleBounds)로 시각 검증.

---

## 3. 반투명(Translucent) 렌더링

### 3.1 별도 패스가 필요한 이유 — 정렬
불투명 물체는 깊이 테스트로 앞의 것이 뒤를 가리므로 그리는 순서가 결과에 영향을 주지 않는다. 그러나 반투명은 색이 **섞이기 때문에 그리는 순서가 결과를 바꾼다**(블렌딩 비가환). 따라서 반투명만 따로 모아 **카메라에서 먼 것부터(back-to-front) 정렬해 그리는 별도 패스**(TranslucentPass)가 필요하다. 이 패스는 **DepthReadOnly** — 깊이 테스트는 하되 깊이 버퍼에 쓰지 않아 반투명끼리 서로 가리지 않는다.

### 3.2 정렬 — depth-first SortKey
`FDrawCommand::ComputeTranslucentSortKey` 는 정렬 키를 `Pass(5) | DepthBucket(28, 역양자화) | Shader(16) | User(15)` 로 구성한다. 카메라 거리²를 28비트로 양자화하되 **멀수록 작은 키**가 되도록 반전해, 정렬 시 자연스럽게 먼 것부터 그려진다. 같은 깊이 구간에서는 셰이더로 묶어 상태 전환을 최소화한다.

파티클은 proxy 단위 거리가 부정확하므로, `FMeshSectionDraw` 의 **SortWorldPos**(emitter 대표 위치)로 섹션별 깊이를 따로 계산해 정렬한다.

### 3.3 블렌드 모드 — 한 패스에 모으는 근거
| 모드 | 수식 | 깊이 쓰기 | 순서 의존 |
|---|---|---|---|
| AlphaBlend | `src·α + dst·(1−α)` | X | O |
| Additive | `src + dst` | X | X |
| Modulate | `src · dst` | X | X |

세 모드 모두 **깊이를 쓰지 않고, 불투명 이후 화면색(dst) 위에 섞는다**는 공통점 때문에 한 패스에 모은다. 블렌드 상태는 DrawCommand별로 교체하고, 순서에 민감한 **AlphaBlend만 정렬**한다(덧셈·곱셈은 교환법칙이 성립해 순서 무관). 실제 적용 예로, 캐릭터 메시의 일부 섹션에 반투명 머티리얼을 적용해 부분 반투명을 구현했다.

---

## 4. 머티리얼 시스템

### 4.1 Domain / BlendMode 단일 소스 (UE 스타일)
머티리얼이 저수준 렌더 상태를 일일이 지정하는 대신, **두 가지 고수준 의도(EMaterialDomain, EBlendMode)** 만 정하면 **RenderPass와 RenderState(Blend·Depth·Rasterizer)가 자동으로 도출**되도록 했다. 예를 들어 `Surface + Translucent` 조합은 Translucent 패스 + 알파 블렌드 + DepthReadOnly로 매핑된다. 머티리얼 에디터에서도 이 둘만 선택하면 된다.

### 4.2 셰이더 디커플링
`DrawCommandBuilder::ResolveSectionShader` 가 **(VertexFactory × Domain × Pass × ViewMode)** 로 셰이더를 도출한다. 머티리얼은 셰이더를 직접 모르며(shader-agnostic), 정점 팩토리(StaticMesh/Skeletal/Particle 등)에 맞는 셰이더를 엔진이 고른다. 비표준 셰이더가 필요한 경우 custom override 경로로 강제할 수 있다.

### 4.3 직렬화 — `.uasset` 바이너리
머티리얼을 JSON에서 **바이너리 `.uasset`** 단일 포맷으로 전환하고 lazy 마이그레이션을 적용했다. 파라미터 → 상수 버퍼 오프셋 매핑은 **셰이더 리플렉션(D3D Reflection)** 에서 자동 추출되어, 파라미터 이름으로 값을 설정하면 올바른 CB 위치에 기록된다.

### 4.4 에디터 & MaterialInstance
- **머티리얼 에디터**: Domain/Blend 편집, 셰이더 선택 드롭다운, 텍스처 슬롯(t0~t7) 리플렉션 기반 노출 및 텍스처 피커, Two-Sided 토글, Create Material.
- **UMaterialInstance**: Parent 머티리얼의 Template·상수 버퍼·텍스처를 복제해 인스턴스별로 파라미터를 오버라이드한다(예: SubUV atlas 분할 시연 자산).

---

## 5. 마무리

이번 주에 파티클 시스템의 **데이터 구조·시뮬레이션·렌더링·최적화** 전 구간과, 이를 받쳐주는 **반투명 패스·머티리얼 시스템**을 통합적으로 구축했다. 게임/렌더 스레드 분리, GPU 인스턴싱, 단일 소스 기반 머티리얼 등 상용 엔진(언리얼 Cascade/머티리얼)의 핵심 설계를 자체 엔진에 맞게 이식한 점이 주요 성과다.

**남은 과제**: 메시 파티클의 섹션별(멀티 머티리얼) 렌더링과 모듈이 소유한 Distribution 객체의 정리 정책(현재 일부 미완)은 후속 작업으로 남아 있다.
