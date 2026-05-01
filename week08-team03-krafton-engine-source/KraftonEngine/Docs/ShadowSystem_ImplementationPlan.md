# Shadow System 구현 계획서

## 1. 현재 엔진 구조 분석

### LightComponent 계층
[현재 구조]
- 관련 파일: `Source/Engine/Component/Light/LightComponentBase.*`, `LightComponent.*`, `DirectionalLightComponent.*`, `PointLightComponent.*`, `SpotLightComponent.*`, `AmbientLightComponent.*`
- 현재 책임: 라이트 속성 저장, `PushToScene()`를 통한 `FSceneEnvironment` 반영, 에디터 프로퍼티 노출
- Shadow 구현 시 영향: 엔진 실제 속성명은 `bCastShadow`이며 요구사항의 `bCastShadows`와 같은 의미로 취급 가능하다. `ULightComponent`에는 `ShadowResolutionScale`, `ShadowBias`, `ShadowSlopeBias`, `ShadowSharpen`가 이미 연결되어 있다.
- 수정 필요 여부: 필요. `ULightComponent`가 `FShadowHandleSet*`를 직접 소유하려는 경로는 이번 설계와 맞지 않으므로, runtime shadow resource는 중앙 관리자 소유로 분리해야 한다.

### RenderCommand 생성 경로
[현재 구조]
- 관련 파일: `Source/Engine/Render/Pipeline/RenderCollector.*`, `DrawCommandBuilder.*`, `DrawCommandList.*`, `Source/Engine/Render/Proxy/PrimitiveSceneProxy.*`
- 현재 책임: 카메라 프러스텀 기준 visible proxy 수집과 MainPass용 draw command 생성
- Shadow 구현 시 영향: 현재 경로를 그대로 재사용하면 화면 밖 shadow caster가 누락된다.
- 수정 필요 여부: 필요. Shadow 전용 caster 수집과 Shadow 전용 draw command list를 별도로 둬야 한다.

### RenderPass / ShadowPass / MainPass 구조
[현재 구조]
- 관련 파일: `Source/Engine/Render/Pipeline/Renderer.*`, `PassRenderStateTable.*`, `PassEventBuilder.*`, `Source/Engine/Render/Types/RenderTypes.h`
- 현재 책임: `PreDepth`, `Opaque`, `PostProcess` 등 기존 pass 실행
- Shadow 구현 시 영향: `EScenePass`에 ShadowPass가 없고, main render loop에도 shadow 단계가 없다.
- 수정 필요 여부: 필요. MainPass와 분리된 ShadowPass 실행 단계를 추가해야 한다.

### SRV 슬롯 바인딩 구조
[현재 구조]
- 관련 파일: `Source/Engine/Render/Pipeline/RenderConstants.h`, `RenderResources.*`, `Shaders/Common/SystemSamplers.hlsli`
- 현재 책임: material SRV `t0-t7`, light buffer `t8-t12`, system texture `t16-t20`, sampler `s0-s2` 관리
- Shadow 구현 시 영향: shadow용 확장 슬롯을 충돌 없이 배치할 수 있다.
- 수정 필요 여부: 필요. shadow 전용 `b5`, `t21+`, `s3+` 정책을 고정해야 한다.

### ConstantBuffer / StructuredBuffer 관리 구조
[현재 구조]
- 관련 파일: `Source/Engine/Render/Pipeline/RenderConstants.h`, `Source/Engine/Render/Resource/RenderResources.*`, `Source/Engine/Render/Pipeline/ForwardLightData.h`, `Shaders/Common/ForwardLightData.hlsli`
- 현재 책임: frame/light constant buffer와 light SRV 갱신, C++/HLSL packing 동기화
- Shadow 구현 시 영향: shadow data도 같은 방식으로 맞추는 것이 안전하다.
- 수정 필요 여부: 필요. 초기에는 ConstantBuffer, 확장 시 StructuredBuffer로 커질 수 있는 구조가 적합하다.

### DepthStencilState 관리 구조
[현재 구조]
- 관련 파일: `Source/Engine/Render/Resource/DepthStencilStateManager.*`, `Source/Engine/Viewport/Viewport.cpp`
- 현재 책임: main scene depth state 생성과 clear convention 유지
- Shadow 구현 시 영향: main renderer는 reversed-Z 기반인데 shadow용 임시 리소스 쪽 clear 관례와 일치 여부를 확인해야 한다.
- 수정 필요 여부: 필요. shadow compare 함수, depth clear 값, bias 정책을 별도로 정리해야 한다.

### SamplerState 관리 구조
[현재 구조]
- 관련 파일: `Source/Engine/Render/Resource/SamplerStateManager.*`, `Shaders/Common/SystemSamplers.hlsli`
- 현재 책임: `PointClamp`, `LinearClamp`, `LinearWrap` 등 기본 sampler 제공
- Shadow 구현 시 영향: PCF를 위해 comparison sampler가 필요하고, VSM 확장 시 linear sampler 사용이 필요하다.
- 수정 필요 여부: 필요. shadow comparison sampler와 debug/VSM용 sampler 추가가 필요하다.

### Editor Property UI
[현재 구조]
- 관련 파일: `Source/Editor/UI/EditorPropertyWidget.cpp`
- 현재 책임: Light shadow settings UI stub, `PSM Depth Map` preview placeholder 표시
- Shadow 구현 시 영향: preview와 property 확장 위치는 이미 확보되어 있다.
- 수정 필요 여부: 필요. preview SRV, memory usage, effective resolution, slice/face selector 연결이 필요하다.

### ViewMode / ShowFlag 구조
[현재 구조]
- 관련 파일: `Source/Engine/Render/Types/ViewTypes.h`, `Source/Editor/Viewport/EditorViewportClient.*`, `Source/Editor/Viewport/FLevelViewportLayout.cpp`
- 현재 책임: viewport별 `ViewMode`, `FShowFlags`, `ShadowMapResolution`, `ShadowFilterMode`, `bOverrideCameraWithSelectedLight` 보관
- Shadow 구현 시 영향: viewport마다 설정이 독립적이므로 shadow resource와 stat도 viewport 단위로 관리해야 한다.
- 수정 필요 여부: 필요. `ShowFlag.Shadows`를 실제 renderer에 연결하고 shadow debug flag를 추가해야 한다.

### Stat 표시 구조
[현재 구조]
- 관련 파일: `Source/Editor/Subsystem/OverlayStatSystem.*`, `Source/Editor/UI/EditorStatWidget.*`
- 현재 책임: FPS, 메모리, draw call, CPU/GPU timing 표시
- Shadow 구현 시 영향: shadow count, shadow memory, resolution 분포는 별도 snapshot 집계가 필요하다.
- 수정 필요 여부: 필요. Shadow stat 수집 구조와 UI 연동이 필요하다.

### Console Command 구조
[현재 구조]
- 관련 파일: `Source/Editor/UI/EditorConsoleWidget.cpp`, `Source/Editor/Settings/EditorSettings.*`
- 현재 책임: 콘솔 명령 파싱과 viewport render option 반영
- Shadow 구현 시 영향: `shadow_filter PCF|VSM`는 이미 명령 형태가 존재한다.
- 수정 필요 여부: 필요. bias/CSM/atlas/debug 관련 명령을 추가해야 한다.

### ImGui / Editor Debug View 구조
[현재 구조]
- 관련 파일: `Source/Editor/UI/EditorMaterialInspector.cpp`, `Source/Editor/UI/EditorPropertyWidget.cpp`
- 현재 책임: ImGui 기반 texture preview와 property panel 렌더링
- Shadow 구현 시 영향: shadow map preview도 같은 방식으로 표시 가능하다.
- 수정 필요 여부: 필요. depth remap debug shader와 preview selector UI가 필요하다.

## 2. 목표 기능 분해

- Directional, Point, Spot은 실제 shadow casting 대상으로 설계하고, 엔진의 네 번째 광원인 Ambient는 shadow map 대상이 아닌 것으로 분리한다.
- `ViewMode == Unlit`이면 ShadowPass와 MainPass shadow 적용을 모두 생략한다.
- `bCastShadow == false` 라이트는 ShadowPass 대상에서 제외한다.
- 모든 Light/Object Mobility는 Movable로 가정하고, ShadowMap은 매 프레임 갱신한다.
- Static Shadow Cache는 이번 계획에서 제외하고, 구조만 추후 확장 가능하게 둔다.
- 오브젝트 측 cast-shadow flag는 현재 없으므로 1차 구현은 월드 primitive를 기본 caster로 간주하되 editor-only, decal, font, gizmo 계열은 제외한다.

## 3. Shadow Rendering Pipeline

- Frame Begin에서 `ShowFlag.Shadows`와 `ViewMode != Unlit`를 확인한다.
- Shadow casting light를 `FSceneEnvironment`에서 수집한다.
- viewport 기준 `FShadowResourceManager`가 shadow map 리소스를 할당 또는 재사용한다.
- light type별 ViewProjection을 계산한다.
- ShadowPass를 실행한다.
- Directional Light는 1회 shadow render를 수행한다.
- Spot Light는 light당 1회 shadow render를 수행한다.
- Point Light는 light당 6 face shadow render를 수행한다.
- Shadow SRV와 `ShadowLightData`를 준비한다.
- MainPass에서 shadow SRV, shadow buffer, sampler를 바인딩한다.
- Lighting shader에서 shadow factor를 direct light term에 곱한다.
- Debug preview, stat, editor view를 갱신한다.

정책:
- Unlit ViewMode에서는 ShadowPass와 shadow sampling을 모두 생략한다.
- `bCastShadow == false` 라이트는 ShadowPass 대상에서 제외한다.
- 모든 오브젝트와 라이트는 Movable로 간주한다.
- ShadowMap은 매 프레임 갱신한다.
- Static Shadow Cache는 이번 계획 범위에서 제외한다.

## 4. Light Type별 설계

### Directional Light
- 현재 엔진 구조는 사실상 global directional 1개 소비 구조와 잘 맞는다.
- 1차 구현은 단일 orthographic shadow map 기반 PSM으로 시작한다.
- 추후 CSM 확장을 고려해 `Cascade ViewProj`, `SplitDepth`, `Slice/AtlasRect`를 담을 수 있는 데이터 구조로 시작한다.

### Spot Light
- perspective shadow map 1장 기반으로 설계한다.
- light position, direction, cone angle, attenuation radius를 이용해 projection을 구성한다.

### Point Light
- cube shadow가 필요하다.
- 현재 엔진의 `Texture2DArrayPool`가 cube-array SRV와 6-slice cluster를 지원하므로 1차는 이 경로를 우선 채택한다.
- face 순서는 `+X, -X, +Y, -Y, +Z, -Z`로 고정한다.
- 추후 `TextureCubeArray` 소비를 더 다듬을 수 있게 face index와 slice index를 함께 관리한다.

### 네 번째 광원
- 엔진의 네 번째 광원 타입은 `Ambient Light`다.
- Ambient는 위치/방향 기반 shadow map을 만들 수 없으므로 PSM shadow 대상이 아니다.
- 요구사항상 반드시 shadow 근사가 필요해지는 경우에는 AO 또는 sky occlusion류 기능으로 별도 제안하는 것이 맞고, 이번 ShadowMap 계획 범위에서는 제외한다.

## 5. ShadowMap Resource 설계

- ShadowMap 리소스는 `ULightComponent`가 직접 소유하지 않는다.
- 중앙 구조는 `FShadowFrameResources`, `FShadowLightEntry`, `FShadowStatsSnapshot`, `FShadowResourceManager`로 정리한다.
- resource는 light 단위가 아니라 viewport/frame 단위로 관리한다.
- 1차는 atlas 없이 시작한다.
- Directional/Spot은 `Texture2DArray` 기반으로 묶고, Point는 cube-array 기반으로 관리한다.
- 기본 해상도는 viewport `ShadowMapResolution`을 사용한다.
- 최종 해상도는 `BaseResolution * ShadowResolutionScale`을 power-of-two bucket으로 정규화한다.
- 권장 초기 multiplier는 Directional `1.0`, Spot `1.0`, Point face당 `0.5`다.
- 해상도 변경 시 기존 texture resize가 아니라 다음 프레임에 새로운 bucket을 재요청하는 정책으로 간다.
- ShadowMap memory stat은 `Width * Height * BytesPerTexel * SliceCount`로 계산한다.
- Point Light는 face 6배를 포함해 집계한다.
- depth-only와 VSM은 포맷별 메모리를 별도 집계한다.
- SRV 최소화는 1차에서 `2D shadow array SRV 1개 + cube shadow array SRV 1개`를 목표로 한다.

Atlas 없이 시작할 경우:
- 장점: 구조 단순, DSV 관리 명확, preview/debug 용이
- 단점: 일부 해상도 낭비와 slice fragmentation 가능성

Atlas 도입 시 고려 항목:
- rect allocator
- UV scale/offset
- padding
- bleeding 제어
- atlas debug preview

## 6. ShadowPass 설계

- MainPass RenderCommand와 ShadowPass RenderCommand는 섞지 않는다.
- `FDrawCommand` 형식은 재사용 가능하지만, `ShadowDrawCommandList`는 별도로 유지한다.
- ShadowPass는 depth-only shader 경로를 사용한다.
- `PreDepth`처럼 null PS 경로를 재사용할 수 있으면 같은 패턴으로 맞춘다.
- 각 light 또는 face/slice 시작 시 DSV clear를 수행한다.
- Shadow viewport는 shadow target 해상도 기준으로 설정한다.
- `ShadowSlopeBias`는 rasterizer state의 `SlopeScaledDepthBias`에 우선 연결한다.
- `ShadowBias`는 receiver compare bias와 필요 시 constant depth bias로 분리한다.
- Point Light는 6 face를 정해진 순서로 반복 렌더링한다.
- 반복 구조는 Directional 1회, Spot light당 1회, Point light당 6회다.
- DSV를 SRV로 소비하기 전에 OM 바인딩을 해제한다.
- DSV와 SRV가 동시에 같은 리소스를 참조하지 않도록 pass 경계를 명확히 둔다.

## 7. MainPass / Shader Binding 설계

- 슬롯 정책 제안:
- `b5`: Shadow globals
- `t21`: Directional/Spot shadow array 또는 atlas
- `t22`: Point cube shadow array
- `s3`: Comparison sampler
- `s4`: Linear sampler for VSM/debug
- Material SRV와 shadow SRV는 슬롯 충돌 없이 분리한다.
- 초기 `ShadowLightData`는 ConstantBuffer로 시작한다.
- Point/CSM 확장으로 matrix 수가 늘어나면 StructuredBuffer로 확장한다.
- `ForwardLighting.hlsli`에서 direct lighting 계산 직전에 `ShadowFactor`를 적용한다.
- sampler 구분은 `PointClamp`, `LinearClamp`, `ComparisonSampler`를 명확히 나눈다.
- Reversed-Z 사용 여부는 ShadowSystem 설계 초기에 먼저 확정한다.
- 구현 단계는 HardCompare → PCF → VSM 순서로 진행한다.

## 8. PCF 계획

- 기본 필수 기능은 3x3 PCF다.
- shadow texel size는 실제 shadow map resolution 기준으로 계산한다.
- 해상도에 따라 radius가 과도하게 달라지지 않도록 texel-space 기반 반경으로 유지한다.
- 2차 확장으로 5x5 또는 Poisson Disk를 고려한다.
- `ShadowSharpen`은 1차에서 PCF weight 또는 radius 조정에 연결할 수 있다.
- acne와 peter-panning 조정 포인트는 rasterizer slope bias, receiver compare bias, 필요 시 normal offset이다.
- 적용 우선순위는 Directional과 Spot 먼저, Point는 cube sampling 안정화 후 적용 가능하게 단계화한다.

## 9. Editor 기능 계획

- Light Property 패널에 다음 항목을 노출 또는 정리한다.
- `bCastShadow`
- `ShadowResolutionScale`
- `ShadowBias`
- `ShadowSlopeBias`
- `ShadowSharpen`
- Shadow filter type
- Effective shadow resolution
- ShadowMap memory usage
- ShadowMap preview
- `PSM Depth Map` preview는 depth remap 또는 linearize debug shader를 거쳐 표시한다.
- Directional CSM 확장 시 cascade selector를 둔다.
- Point Light는 face selector가 필요하다.
- atlas 도입 시 slice/rect selector가 추가로 필요하다.
- `Override Camera with Light’s Perspective`는 viewport별 camera snapshot을 저장한 뒤 selected light 기준으로 전환한다.
- Directional은 orthographic/light basis 기준, Spot은 perspective 기준, Point는 6 face 중 선택 기반으로 처리한다.
- override 해제 시 원래 editor camera로 복귀하는 경로가 필요하다.
- Directional shadow fitting에 사용하는 기준 카메라와 editor debug camera override는 분리해야 한다.

## 10. ShowFlag / Stat / Console Command 계획

### ShowFlag
- `ShowFlag.Shadows`
- `ShowFlag.ShadowMaps`
- `ShowFlag.ShadowDebug`
- `ShowFlag.CSM`
- `ViewMode == Unlit`에서는 자동 비활성화한다.

### Stat
- Shadow casting light count
- Directional shadow count
- Spot shadow count
- Point shadow count
- ShadowMap total memory
- ShadowMap resolution distribution
- ShadowPass draw calls
- ShadowPass triangle count
- Shadow filter mode
- CSM cascade count

### Console Command
- `shadow_filter PCF`
- `shadow_filter VSM`
- `shadow_csm_enable 0/1`
- `shadow_csm_count N`
- `shadow_csm_resolution N`
- `shadow_show_atlas 0/1`
- `shadow_bias VALUE`
- `shadow_slope_bias VALUE`

## 11. 선택 기능 확장 계획

### VSM
- shadow map 포맷은 `RG32F` 계열로 `depth`, `depth^2`를 저장한다.
- 기존 depth-only shadow map과 resource 경로를 분리한다.
- blur/filter pass가 추가로 필요하다.
- light bleeding 완화는 variance clamp 또는 reduction term으로 조절한다.
- `ShadowSharpen`은 clamp 또는 filter 강도에 연동할 수 있다.
- `shadow_filter VSM`, `shadow_filter PCF`로 전환한다.
- 공존 방식은 소규모 permutation 또는 uniform branch 중 선택한다.

### CSM
- Directional Light 확장 기능으로 설계한다.
- cascade split 계산, cascade별 ViewProjection, cascade별 resolution, cascade selection in shader, cascade transition blending을 포함한다.
- slice 기반 또는 atlas 영역 기반 저장 둘 다 확장 가능하도록 데이터 구조를 둔다.
- 콘솔 명령으로 cascade count, distance, split lambda, resolution을 제어한다.
- Stat에는 cascade 수, cascade별 resolution, cascade memory/slice 수를 포함한다.
- 참고 문서: <https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps>

### Shadow Atlas
- 현재 엔진 atlas 경로는 미완성 흔적이 있으므로 1차 구현 기반으로 쓰지 않는다.
- 2차 확장에서 rect packing, padding, bleeding, preview 체계를 갖춘 뒤 도입한다.

### 추가 확장
- 최소 SRV 바인딩
- TextureCubeArray 고도화
- Adaptive/Variable Shadow Resolution

## 12. 구현 Phase

이 섹션은 실제 구현 착수 시 바로 사용할 수 있도록 작업을 Task ID 단위로 세분화한 체크리스트다.

### Phase 1: 구조 정리

| Task ID | 작업 | 관련 파일 | 완료 기준 |
| --- | --- | --- | --- |
| P1-01 | Shadow 중앙 구조체 초안 정의 | `Source/Engine/Render/Shadow/*` 신규, `Source/Engine/Render/Pipeline/FrameContext.*` | `FShadowFrameResources`, `FShadowLightEntry`, `FShadowStatsSnapshot`, `FShadowResourceManager` 역할이 문서와 코드 구조상 분리됨 |
| P1-02 | LightComponent의 runtime shadow ownership 제거 방향 정리 | `Source/Engine/Component/Light/LightComponent.*`, `DirectionalLightComponent.*`, `SpotLightComponent.*` | `ULightComponent`는 authoring property만 보유하고 runtime resource 소유 경로가 끊어짐 |
| P1-03 | viewport 단위 shadow option과 frame resource 연결 | `Source/Engine/Render/Types/ViewTypes.h`, `Source/Engine/Render/Pipeline/FrameContext.*` | viewport별 `ShadowMapResolution`, `ShadowFilterMode`, `bShadows`가 shadow frame context로 전달됨 |
| P1-04 | Shadow 슬롯 정책 고정 | `Source/Engine/Render/Pipeline/RenderConstants.h`, `RenderResources.*`, `Shaders/Common/SystemSamplers.hlsli` | `b5`, `t21`, `t22`, `s3`, `s4` 슬롯 정책이 코드 상수와 문서에 일치하게 반영됨 |
| P1-05 | Shadow data packing 규칙 정의 | `Source/Engine/Render/Pipeline/ForwardLightData.h`, `Shaders/Common/ForwardLightData.hlsli`, `Shadow*.h/.hlsli` 신규 | shadow CB/SB의 C++/HLSL packing 규칙과 검증 방식이 확정됨 |
| P1-06 | Reversed-Z 적용 여부 확정 | `Source/Engine/Viewport/Viewport.cpp`, `DepthStencilStateManager.*`, `Texture2DArrayPool.*` | shadow depth clear 값, compare 함수, bias 방향이 하나의 convention으로 정리됨 |

### Phase 2: 기본 PSM

| Task ID | 작업 | 관련 파일 | 완료 기준 |
| --- | --- | --- | --- |
| P2-01 | ShadowPass enum 및 renderer 실행 단계 추가 | `Source/Engine/Render/Types/RenderTypes.h`, `Source/Engine/Render/Pipeline/Renderer.*`, `PassEventBuilder.*` | MainPass 이전에 ShadowPass가 독립 단계로 실행됨 |
| P2-02 | Shadow caster 수집 경로 분리 | `Source/Engine/Render/Pipeline/RenderCollector.*`, `Source/Engine/Collision/SpatialPartition.*`, `Source/Engine/Render/Proxy/PrimitiveSceneProxy.*` | camera visible 여부와 무관하게 light frustum 기준 caster 수집 가능 |
| P2-03 | ShadowDrawCommandList 구성 | `DrawCommandBuilder.*`, `DrawCommandList.*` | MainPass command와 분리된 shadow 전용 draw command 생성 가능 |
| P2-04 | Directional shadow map 생성 및 1회 렌더 | `Renderer.*`, `RenderResources.*`, `Source/Engine/Render/Proxy/SceneEnvironment.*` | Directional light 1개에 대해 depth map이 생성되고 갱신됨 |
| P2-05 | Spot shadow map 생성 및 1회 렌더 | `Renderer.*`, `SceneEnvironment.*` | Spot light마다 depth map과 ViewProjection 계산이 동작함 |
| P2-06 | Depth-only shadow shader 추가 | `Source/Engine/Render/Resource/ShaderManager.*`, `Shaders/Geometry/ShadowDepth.hlsl` 신규 | ShadowPass에서 color write 없이 depth-only draw 가능 |
| P2-07 | HardCompare shadow sampling 연결 | `Shaders/Common/ForwardLighting.hlsli`, `Shaders/Geometry/UberLit.hlsl`, `RenderResources.*` | Lit ViewMode에서만 hard shadow가 적용되고 Unlit에서는 완전히 생략됨 |
| P2-08 | ShadowMap preview 1차 연결 | `Source/Editor/UI/EditorPropertyWidget.cpp` | selected directional/spot light의 depth map을 property panel에서 확인 가능 |

### Phase 3: Point Light

| Task ID | 작업 | 관련 파일 | 완료 기준 |
| --- | --- | --- | --- |
| P3-01 | Point shadow resource 경로 확정 | `Source/Engine/Render/Resource/Texture2DArrayPool.*`, `Render/Shadow/*` 신규 | cube-array 또는 6-slice cluster 방식이 실제 allocator 구조와 일치하게 연결됨 |
| P3-02 | Point light 6 face ViewProjection 계산 | `Renderer.*`, `SceneEnvironment.*`, `PointLightComponent.*` | point light당 6개의 face matrix가 생성됨 |
| P3-03 | Point ShadowPass 6회 렌더 루프 추가 | `Renderer.*`, `DrawCommandList.*` | point light shadow map이 6 face 순서대로 렌더됨 |
| P3-04 | Point shadow sampling shader 추가 | `Shaders/Common/ForwardLighting.hlsli`, `Shaders/Common/Shadow*.hlsli` 신규 | main shader에서 point shadow compare가 가능 |
| P3-05 | Point shadow preview selector 추가 | `Source/Editor/UI/EditorPropertyWidget.cpp` | face 선택 UI를 통해 6개 면을 미리보기 가능 |

### Phase 4: PCF

| Task ID | 작업 | 관련 파일 | 완료 기준 |
| --- | --- | --- | --- |
| P4-01 | Comparison sampler 추가 | `Source/Engine/Render/Resource/SamplerStateManager.*`, `Shaders/Common/SystemSamplers.hlsli` | shadow compare 전용 sampler를 시스템 sampler로 바인딩 가능 |
| P4-02 | Directional/Spot 3x3 PCF 적용 | `Shaders/Common/ForwardLighting.hlsli`, `Shaders/Common/Shadow*.hlsli` 신규 | directional/spot shadow가 3x3 PCF로 필터링됨 |
| P4-03 | Point PCF 적용 여부 분리 | `Shaders/Common/ForwardLighting.hlsli` | point는 hard compare 유지 또는 PCF 적용을 선택 가능한 구조가 됨 |
| P4-04 | Bias 튜닝 경로 정리 | `DepthStencilStateManager.*`, `Renderer.*`, `LightComponent.*` | `ShadowBias`, `ShadowSlopeBias`, `ShadowSharpen`의 반영 위치가 정리됨 |
| P4-05 | ShowFlag 연동 | `Source/Engine/Render/Types/ViewTypes.h`, `Renderer.*`, `Source/Editor/Viewport/FLevelViewportLayout.cpp` | `ShowFlag.Shadows` off 시 ShadowPass와 shadow sampling이 모두 비활성화됨 |

### Phase 5: Editor / Stat

| Task ID | 작업 | 관련 파일 | 완료 기준 |
| --- | --- | --- | --- |
| P5-01 | Light Property shadow UI 완성 | `Source/Editor/UI/EditorPropertyWidget.cpp` | preview, effective resolution, memory usage, filter mode가 표시됨 |
| P5-02 | Override camera with selected light 구현 | `Source/Editor/Viewport/EditorViewportClient.*`, `Source/Editor/Viewport/FLevelViewportLayout.cpp`, `Source/Editor/Settings/EditorSettings.*` | viewport camera snapshot 저장/복귀가 정상 동작함 |
| P5-03 | Shadow stat snapshot 집계 | `Source/Engine/Render/Shadow/*` 신규, `Renderer.*` | shadow count, memory, draw call, triangle count가 프레임마다 계산됨 |
| P5-04 | Overlay 또는 stat widget 표시 연결 | `Source/Editor/Subsystem/OverlayStatSystem.*`, `Source/Editor/UI/EditorStatWidget.*` | shadow stat이 editor UI에서 확인 가능 |
| P5-05 | Shadow console command 확장 | `Source/Editor/UI/EditorConsoleWidget.cpp` | bias, slope bias, CSM, atlas, filter 명령이 viewport 설정에 반영됨 |
| P5-06 | ShadowMapResolution editor 변경 연동 | `Source/Editor/Settings/EditorSettings.*`, `Source/Editor/UI/EditorPropertyWidget.cpp` | 해상도 변경 시 다음 프레임 shadow resource 재할당이 반영됨 |

### Phase 6: 선택 확장

| Task ID | 작업 | 관련 파일 | 완료 기준 |
| --- | --- | --- | --- |
| P6-01 | VSM resource 및 blur pass 추가 | `Render/Shadow/*`, `Renderer.*`, `Shaders/Common/Shadow*.hlsli`, `Shaders/PostProcess/*` | `shadow_filter VSM` 동작과 blur pass가 연결됨 |
| P6-02 | CSM 데이터 구조와 directional 분기 추가 | `Renderer.*`, `SceneEnvironment.*`, `Shaders/Common/Shadow*.hlsli` | directional shadow가 cascade 수 기준으로 분할 렌더 가능 |
| P6-03 | CSM console/stat 연결 | `EditorConsoleWidget.cpp`, `OverlayStatSystem.*`, `EditorStatWidget.*` | cascade count, distance, resolution 조절과 표시가 가능 |
| P6-04 | Shadow atlas 도입 | `Source/Engine/Render/Resource/TexturePool/*`, `Render/Shadow/*` | atlas rect allocation, UV remap, preview가 동작함 |
| P6-05 | 최소 SRV 바인딩/TextureCubeArray 고도화 | `RenderResources.*`, `Texture2DArrayPool.*`, `Shaders/Common/Shadow*.hlsli` | SRV 수 축소와 cube shadow sampling 고도화가 반영됨 |
| P6-06 | Adaptive shadow resolution | `Renderer.*`, `Render/Shadow/*`, `LightComponent.*` | light importance 또는 거리 기반 해상도 정책이 적용됨 |

### 구현 순서 게이트

- Gate A: Phase 1 완료 후 directional/spot hard shadow 구현 착수
- Gate B: Phase 2 완료 후 point shadow 경로 착수
- Gate C: Phase 3 완료 후 PCF와 bias 튜닝 착수
- Gate D: Phase 4 완료 후 editor/stat/console 연결 착수
- Gate E: Phase 5 완료 후 선택 확장 착수

### 작업 기록 템플릿

아래 표를 구현 중 지속적으로 갱신한다.

| 날짜 | 작업 ID | 상태 | 변경 대상 파일 | 결과 | 이슈/후속 조치 |
| --- | --- | --- | --- | --- | --- |
| YYYY-MM-DD | P1-01 | `Not Started / In Progress / Done / Blocked` | `예: Source/Engine/Render/Shadow/ShadowTypes.h` | 한 줄 요약 | blocker 또는 다음 작업 |

### 작업 기록 규칙

- 상태 값은 `Not Started`, `In Progress`, `Done`, `Blocked` 네 가지만 사용한다.
- 한 작업 ID당 최소 1회 기록하고, 상태가 바뀔 때마다 같은 ID로 새 줄을 추가한다.
- 구현 중 설계가 바뀌면 해당 Task ID 결과 칸에 이유를 남기고, 필요하면 새 Task ID를 추가한다.
- `Blocked` 상태가 나오면 blocker 해소 전까지 다음 Phase로 넘어가지 않는다.
- 파일 경로는 가능한 한 실제 수정 파일 기준으로 기록한다.

## 13. 리스크와 확인 포인트

- D3D11 리소스 생명주기: editor는 viewport별 shadow 옵션이 다르므로 shadow resource는 viewport/frame 스코프가 적합하다. `ULightComponent`가 리소스를 직접 소유하면 수명 관리와 멀티 뷰포트 동기화가 깨질 위험이 크다.
- DSV/SRV 동시 바인딩 문제: 같은 shadow texture를 DSV와 SRV로 동시에 바인딩할 수 없으므로 ShadowPass 종료 후 unbind 순서를 강제해야 한다.
- Reversed-Z: main renderer는 reversed-Z 기반인데 shadow 관련 임시 리소스 쪽 clear convention은 다를 가능성이 있다. shadow도 reversed-Z로 통일할지, shadow만 별도 convention으로 둘지 먼저 확정해야 한다.
- Shader packing: shadow CB/SB는 `ForwardLightData`와 동일하게 C++/HLSL 간 16-byte 정렬과 packing 일치를 강제해야 한다.
- 현재 shadow 관련 기존 코드 상태: `FShadowHandleSet`, atlas allocator, debug SRV 경로는 미완성 상태로 보이며 1차 구현 기반으로 사용하기에는 위험하다.
- 카메라 가시성 기반 collector 재사용 위험: 현재 collector는 camera visible primitive만 수집하므로 그대로 쓰면 화면 밖 caster가 누락된다.
- 오브젝트 cast-shadow flag 부재: 초기 정책을 명확히 정하지 않으면 ShadowPass draw call이 과도하게 증가할 수 있다.
- Point Light 비용: light당 6회 렌더가 필요하므로 draw call, triangle count, memory 증가가 크다. stat과 resolution policy를 함께 넣어야 한다.
- Directional debug camera override: editor camera를 light view로 바꾸는 기능이 실제 directional shadow fitting 기준과 섞이면 shadow coverage가 흔들릴 수 있으므로 분리 설계가 필요하다.
