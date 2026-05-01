# ShadowPass 변경점 정리

## 1. 개요

이번 변경의 목표는 D3D11 기반 엔진에 `ShadowPass`를 추가해서, `MainPass` 이전에 shadow map을 생성하고 `UberLit` 기반 Opaque 라이팅에서 hard shadow를 샘플링할 수 있도록 연결하는 것이다.

현재 구현 상태는 아래와 같다.

- `RenderFrame` 흐름에 shadow 전용 선행 패스가 추가되었다.
- `Spot Light` hard shadow가 실제 depth-only shadow map 경로로 연결되었다.
- `Directional Light`는 단일 orthographic shadow 경로가 추가되었다.
- `Point Light`는 `FShadowInfo.Type == 1` 구조와 샘플링 확장 포인트만 열어두었고, 실제 렌더링은 아직 미구현이다.
- PCF/VSM/EVSM/CSM은 전처리 분기 자리만 만들고 실제 동작은 hard shadow 기준으로 유지했다.

## 2. 렌더링 흐름 변경

렌더러의 프레임 실행 순서가 아래처럼 바뀌었다.

```cpp
UpdateFrameBuffer();
BindSystemSamplers();

BuildShadowPassData();
RenderShadowPass();

UpdateLightBuffer(..., ShadowBindingData);
BindShadowResources();

RenderMainPass();
CleanupPassState();
```

핵심 변경점:

- `BuildShadowPassData()`에서 이번 프레임 shadow 대상 라이트와 shadow info를 구성한다.
- `RenderShadowPass()`에서 atlas DSV에 depth-only draw를 수행한다.
- shadow pass가 끝난 뒤 `ShadowInfos`, `ShadowAtlasArray`를 `MainPass`에 SRV로 바인딩한다.
- `CleanupPassState()`에서 shadow SRV를 명시적으로 해제한다.

관련 파일:

- `Source/Engine/Render/Pipeline/Renderer.h`
- `Source/Engine/Render/Pipeline/Renderer.cpp`

## 3. CPU / GPU 데이터 구조 변경

### `FShadowInfo` 추가

CPU와 HLSL 양쪽에 동일 레이아웃의 `FShadowInfo`를 추가했다.

구성:

- `Type`
- `ArrayIndex`
- `LightIndex`
- `Padding0`
- `LightVP`
- `SampleData`

주의점:

- 엔진의 `FMatrix`는 SIMD 정렬이 포함되어 있어 structured buffer에 그대로 넣기 부적합하다.
- 그래서 CPU 쪽에는 packed 64-byte matrix인 `FShadowMatrixGPU`를 별도로 두고 `FShadowInfo.LightVP`에 사용했다.
- 이 구조로 HLSL `row_major float4x4`와 1:1 바이트 정합을 맞췄다.

추가 변경:

- `FLightInfo`에 `ShadowIndex`를 추가했다.
- `FDirectionalLightGPU`에도 `ShadowIndex`를 추가했다.
- `FLightingCBData` 크기가 shadow index 반영에 맞게 갱신되었다.

관련 파일:

- `Source/Engine/Render/Pipeline/ForwardLightData.h`
- `Shaders/Common/ForwardLightData.hlsli`

## 4. Shadow 리소스 슬롯 / 샘플러 추가

### SRV 슬롯

다음 슬롯을 shadow 전용으로 사용한다.

- `t21`: `StructuredBuffer<FShadowInfo>`
- `t22`: `Texture2DArray gShadowAtlasArray`
- `t23`: `TextureCubeArray gShadowCubeArray`

현재 실제 바인딩:

- `t21`: shadow info structured buffer
- `t22`: atlas pool SRV
- `t23`: `nullptr` placeholder

### Sampler 슬롯

- `s3`: `ShadowCmpSampler`
- `s4`: `ShadowPointSampler`

현재 비교 샘플러는 reversed-Z 기준으로 `GREATER_EQUAL` comparison을 사용한다.

관련 파일:

- `Source/Engine/Render/Pipeline/RenderConstants.h`
- `Source/Engine/Render/Resource/SamplerStateManager.h`
- `Source/Engine/Render/Resource/SamplerStateManager.cpp`
- `Shaders/Common/SystemSamplers.hlsli`

## 5. ShadowInfo 버퍼 업로드 경로

`RenderResources`에 shadow info 전용 structured buffer 리소스를 추가했다.

동작:

- `ShadowInfos.Create()`로 초기 리소스를 만든다.
- 프레임마다 `UpdateLightBuffer(..., ShadowBindingData)`에서 shadow info 배열을 업로드한다.
- spot / directional light의 `ShadowIndex`를 light GPU 데이터에 기록한다.
- `BindShadowResources()`에서 `t21`, `t22`, `t23`를 pixel shader에 바인딩한다.

구현 포인트:

- shadow info 개수 변화 시 structured buffer를 재할당할 수 있다.
- shadow 데이터가 없더라도 빈 structured buffer를 유지한다.
- shadow atlas SRV와 shadow info SRV 바인딩은 `MainPass` 직전에 수행한다.

관련 파일:

- `Source/Engine/Render/Resource/RenderResources.h`
- `Source/Engine/Render/Resource/RenderResources.cpp`

## 6. ShadowPass 구현 내용

### 6.1 라이트 선택 정책

현재 shadow 업데이트 대상은 아래 기준으로 고른다.

- `ViewMode != Unlit`
- `SpotLightComponent::IsCastShadow() == true`
- 카메라 프러스텀과 라이트 반경이 교차
- `DirectionalLightComponent::IsCastShadow() == true`면 항상 포함

현재는 모든 shadow map을 매 프레임 갱신한다.

### 6.2 shadow handle / atlas 할당

라이트 컴포넌트의 `GetShadowHandleSet()`를 사용해 atlas handle을 확보한다.

구현 포인트:

- spot / directional light는 `FTextureAtlasPool`의 `Texture2DArray` atlas를 사용한다.
- atlas resize가 발생할 수 있어서, 먼저 모든 라이트의 handle을 한 번 확보하고 이후 최종 UV/DSV를 다시 조회하는 2-pass 방식을 쓴다.
- `SpotLightComponent`, `DirectionalLightComponent`의 `GetShadowHandleSet()`는 invalid handle을 감지하면 새로 할당하도록 보강되었다.

관련 파일:

- `Source/Engine/Component/Light/LightComponent.h`
- `Source/Engine/Component/Light/SpotLightComponent.cpp`
- `Source/Engine/Component/Light/DirectionalLightComponent.cpp`
- `Source/Engine/Render/Resource/TexturePool/TextureAtalsPool.cpp`
- `Source/Engine/Render/Resource/TexturePool/TexturePool.cpp`

### 6.3 LightVP 생성

현재 구현된 행렬 생성:

- `Spot`: light local axes + reversed-Z perspective
- `Directional`: camera 중심 기준 single orthographic shadow

구현 보조:

- `FShadowUtil::MakeAxesViewMatrix`
- `FShadowUtil::MakeReversedZPerspective`
- `FShadowUtil::MakeReversedZOrthographic`
- `FShadowUtil::MakeAtlasViewport`

### 6.4 shadow caster 정책

이번 단계에서는 primitive별 `bCastShadow`를 추가하지 않았다.

현재 shadow caster 판정은 아래와 같다.

- `Proxy->IsVisible()`
- `ERenderPass::Opaque`
- 유효한 mesh buffer 보유
- shadow frustum과 proxy bounds가 교차

즉, 현재 정책은 "보이는 opaque mesh는 모두 caster 후보"다.

### 6.5 depth-only draw

shadow pass에서는 color RTV를 바인딩하지 않는다.

실행 흐름:

- atlas 영역 DSV 바인딩
- viewport를 atlas UV 영역으로 설정
- `ShadowClear` 상태로 shadow 영역만 draw-clear
- `ShadowDepth` 상태로 depth-only draw
- `PerObjectBuffer`와 shadow 전용 `LightVP` constant buffer를 VS에 바인딩
- mesh section별 `DrawIndexed`

관련 파일:

- `Source/Engine/Render/Pipeline/Renderer.cpp`
- `Source/Engine/Render/Types/RenderStateTypes.h`
- `Source/Engine/Render/Resource/DepthStencilStateManager.h`
- `Source/Engine/Render/Resource/DepthStencilStateManager.cpp`
- `Shaders/Geometry/ShadowDepth.hlsl`
- `Shaders/Geometry/ShadowClear.hlsl`

## 7. MainPass shadow 샘플링 연동

`UberLit` 경로에서 shadow를 direct lighting에 곱하도록 수정했다.

구현 내용:

- `SampleAtlasShadow()`
- `SampleCubeShadow()`
- `SampleShadowInfo()`
- `GetDirectionalShadow()`
- `GetLightShadow()`

적용 위치:

- directional diffuse
- directional specular
- point / spot diffuse
- point / spot specular
- toon directional / toon point-spot diffuse

전처리 확장 자리:

- `SHADOW_MODE_HARD`
- `SHADOW_ENABLE_PCF`
- `SHADOW_ENABLE_VSM`
- `SHADOW_ENABLE_EVSM`
- `SHADOW_ENABLE_CSM`

현재 실동작은 hard shadow만 연결되어 있다.

관련 파일:

- `Shaders/Common/ForwardLighting.hlsli`
- `Shaders/Geometry/UberLit.hlsl`

## 8. Shadow DSV / SRV hazard 처리

이번 변경에서 중요한 부분은 같은 shadow texture를 DSV와 SRV로 동시에 물지 않도록 하는 것이다.

현재 처리 방식:

- `RenderShadowPass()` 시작 전에 `Resources.UnbindShadowResources()` 호출
- shadow draw 동안은 DSV로만 사용
- shadow pass 종료 후 `MainPass` 직전에 `Resources.BindShadowResources()` 호출
- 프레임 종료 시 `CleanupPassState()`에서 다시 shadow SRV 해제

즉 shadow atlas는 같은 프레임에서 다음 순서만 허용한다.

1. `DSV`로 기록
2. 바인딩 해제
3. `SRV`로 샘플링

## 9. 상태 / 셰이더 / 샘플러 추가

추가된 상태:

- `EDepthStencilState::ShadowDepth`
- `EDepthStencilState::ShadowClear`

추가된 셰이더:

- `Shaders/Geometry/ShadowDepth.hlsl`
- `Shaders/Geometry/ShadowClear.hlsl`

추가된 셰이더 경로 등록:

- `EShaderPath::ShadowDepth`
- `EShaderPath::ShadowClear`

추가된 샘플러:

- `ShadowCmpSampler`
- `ShadowPointSampler`

## 10. 지원 범위

현재 지원:

- Spot Light 여러 개
- Directional Light 1개
- Opaque + UberLit 기반 hard shadow
- reversed-Z depth compare
- atlas 기반 shadow map 샘플링

현재 미지원 또는 placeholder:

- Point Light 실제 shadow render
- TextureCubeArray 실제 resource 바인딩
- PCF 구현
- VSM / EVSM 구현
- CSM 분할 구현
- primitive별 cast shadow flag
- shadow cache / dirty update 최적화

## 11. 검증 내용

검증 완료:

- `Debug|x64` 솔루션 빌드 성공
- `ShadowDepth.hlsl` VS/PS 정적 컴파일 성공
- `ShadowClear.hlsl` VS/PS 정적 컴파일 성공
- `UberLit.hlsl`의 `Phong`, `Gouraud`, `Toon`, `Unlit` 경로 정적 컴파일 성공
- `FShadowInfo` CPU/HLSL 레이아웃 96-byte 정합 확인

주의:

- 이 문서는 빌드 / 셰이더 컴파일 기준 정리다.
- 실제 화면에서 shadow 품질, bias 값, peter-panning, acne 조정은 씬 테스트로 추가 확인이 필요하다.


요 다음 부턴 코덱스가 맘대로 쓴거라 읽어보기만
## 12. 이후 확장 권장 순서

추천 후속 작업 순서:

1. Spot Light bias 튜닝과 실제 씬 검증
2. Point Light 6-slice shadow render 구현
3. spot shadow에 PCF 추가
4. directional shadow를 CSM 구조로 확장
5. primitive별 cast-shadow flag 추가
6. shadow dirty tracking / update 최적화

