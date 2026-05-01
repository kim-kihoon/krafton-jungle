# Editor & Rendering Shadow Feature Summary

## 1. 개요

본 문서는 Editor & Rendering 파트에서 구현한 그림자 렌더링 기능을 정리한 설명 문서이다.

구현 목표는 에디터에서 눈에 보이는 3D 월드에 대해 여러 종류의 광원이 만드는 실시간 그림자를 처리하고, 그림자 품질과 디버깅 정보를 에디터에서 확인 및 제어할 수 있게 만드는 것이다.

대상 광원은 다음과 같다.

- Directional Light
- Point Light
- Spot Light
- Ambient Light

이 중 Ambient Light는 방향이나 위치가 없는 전역 조명으로 취급하므로 Shadow Map을 생성하지 않는다. 실제 그림자 생성 대상은 Directional, Point, Spot Light이다.

## 2. 요구사항 요약

### 기본 그림자 렌더링

- `bCastShadow`가 켜진 라이트만 Shadow Map을 생성한다.
- `ViewMode`가 `Unlit`일 때는 그림자 렌더링과 그림자 샘플링을 수행하지 않는다.
- 모든 라이트와 모든 오브젝트는 Static이 아닌 Movable로 간주한다.
- 따라서 Shadow Map은 정적 캐시 없이 매 프레임 갱신된다.
- 월드에 1개의 Directional Light와 여러 개의 Point Light, Spot Light가 동시에 존재하는 상황을 처리한다.

### Directional Light

- Directional Light는 Standard Shadow Map, Perspective Shadow Map, Cascade Shadow Map 모드를 지원한다.
- PSM 모드에서는 카메라 frustum을 post-perspective 공간으로 변환한 뒤, 해당 공간에서 라이트 기준 투영을 구성해 Shadow Map을 생성한다.
- CSM 모드에서는 카메라 frustum을 여러 cascade 구간으로 나누고 각 구간마다 별도의 Light ViewProjection을 생성한다.

### Point Light

- Point Light는 모든 방향으로 그림자를 드리우기 위해 cube map 방식의 Shadow Map을 사용한다.
- GPU 리소스는 `TextureCubeArray` 계열 SRV로 전달한다.
- 하나의 Point Light는 6개 face 방향에 대해 shadow pass를 수행한다.

### Spot Light

- Spot Light는 원근 투영 기반의 단일 Shadow Map을 사용한다.
- 라이트 위치와 방향, outer cone angle을 기준으로 Light ViewProjection을 만든다.

## 3. Light Component Shadow 속성

라이트 컴포넌트에는 그림자 제어를 위한 속성이 포함되어 있다.

```cpp
class ULightComponentBase : public USceneComponent
{
	...
	bool bCastShadow;
	...
};

class ULightComponent : public ULightComponentBase
{
	...
	float ShadowResolutionScale;
	float ShadowBias;
	float ShadowSlopeBias;
	float ShadowSharpen;
	...
};
```

각 속성의 의미는 다음과 같다.

- `bCastShadow`: 해당 라이트가 그림자를 생성할지 결정한다.
- `ShadowResolutionScale`: 라이트별 Shadow Map 해상도 배율이다.
- `ShadowBias`: constant depth bias 조절값이다.
- `ShadowSlopeBias`: 표면 기울기에 따른 slope-scaled bias 조절값이다.
- `ShadowSharpen`: VSM 필터에서 light bleeding을 줄이기 위한 보정값이다.

Primitive Component에도 `CastShadow` 속성이 존재하며, 이 값이 꺼진 오브젝트는 shadow caster에서 제외된다.

## 4. 렌더링 파이프라인

그림자 렌더링은 main lighting pass 전에 별도 shadow pass로 수행된다.

```text
Frame Begin
  -> ViewMode / ShowFlag 검사
  -> shadow casting light 수집
  -> shadow atlas / cube shadow 리소스 할당
  -> light type별 shadow view-projection 생성
  -> Shadow Depth Pass 실행
  -> ShadowInfo GPU buffer 갱신
  -> shadow SRV 바인딩
  -> Main Lighting Pass에서 shadow sampling
  -> debug preview / stat 갱신
```

핵심 정책은 다음과 같다.

- `ViewMode == Unlit`이면 Shadow Depth Pass를 생략한다.
- `ShowFlag.Shadows == false`이면 Shadow Map 샘플링을 하지 않는다.
- `bCastShadow == false`인 라이트는 shadow task를 만들지 않는다.
- 화면 밖 오브젝트라도 그림자를 드리울 수 있으므로, shadow caster 수집은 main camera visible list에만 의존하지 않는다.

## 5. Shadow Depth Pass

Shadow Depth Pass는 라이트 시점에서 장면의 깊이를 Shadow Map에 기록하는 단계이다.

일반 렌더링에서는 카메라 기준으로 color와 depth를 렌더링하지만, shadow pass에서는 라이트를 카메라처럼 사용한다. 이때 기록된 depth는 이후 main lighting pass에서 현재 픽셀이 라이트에서 보이는 위치인지 비교하는 기준이 된다.

비교 과정은 다음과 같다.

1. Shadow Map 생성 시 라이트 기준 depth를 저장한다.
2. Main Pass에서 현재 픽셀의 world position을 같은 light space로 변환한다.
3. 변환된 depth와 Shadow Map에 저장된 depth를 비교한다.
4. 현재 픽셀이 Shadow Map에 저장된 표면보다 뒤에 있으면 그림자 영역으로 판단한다.

## 6. Bias

Shadow Map에 기록된 depth와 main pass에서 다시 계산한 depth는 부동소수점 오차, rasterization 차이, depth 해상도 한계 때문에 완전히 일치하지 않는다. 이 차이 때문에 표면이 자기 자신을 가리는 Shadow Acne가 발생할 수 있다.

이를 줄이기 위해 두 종류의 bias를 사용한다.

- Constant Bias: 모든 픽셀에 일정하게 적용하는 depth offset
- Slope-Scaled Bias: 표면이 라이트 방향에 대해 비스듬할수록 더 크게 적용하는 offset

구현에서는 라이트별 `ShadowBias`, `ShadowSlopeBias` 값을 shadow 정보에 포함하고, shadow compare 단계에서 receiver bias로 사용한다. 필요한 경우 shadow depth pass의 rasterizer state에도 depth bias를 반영해 depth map 기록 자체를 조정한다.

목표는 다음 두 문제 사이의 균형을 잡는 것이다.

- bias가 너무 작으면 Shadow Acne가 생긴다.
- bias가 너무 크면 그림자가 물체에서 떨어져 보이는 Peter Panning이 생긴다.

## 7. PCF

PCF, Percentage-Closer Filtering는 그림자 경계를 부드럽게 만들기 위한 필터링 방식이다.

단일 texel만 비교하면 그림자의 가장자리가 계단처럼 날카롭게 보인다. PCF는 주변 여러 texel에서 shadow compare를 수행한 뒤 평균을 내어 경계를 부드럽게 만든다.

현재 구현에서는 atlas shadow와 cube shadow 모두 weighted PCF 샘플링을 수행할 수 있다.

## 8. VSM

VSM, Variance Shadow Map은 depth 하나만 저장하지 않고 depth의 1차, 2차 moment를 저장해 확률 기반으로 그림자 여부를 계산하는 방식이다.

장점:

- 일반 texture filtering과 blur pass를 활용할 수 있다.
- 넓고 부드러운 그림자를 만들기 쉽다.

주의점:

- light bleeding이 발생할 수 있다.
- `ShadowSharpen` 값을 이용해 light bleeding reduction을 조절한다.

에디터 콘솔에서 필터를 전환할 수 있다.

```text
shadow_filter PCF
shadow_filter VSM
shadow_filter None
```

## 9. Shadow Atlas

Spot Light와 Directional Light 계열 shadow map은 Shadow Atlas에 배치된다.

Shadow Atlas를 사용하는 이유는 다음과 같다.

- 여러 Shadow Map을 하나의 `Texture2DArray` SRV로 묶어 pixel shader에 전달할 수 있다.
- 라이트 개수가 늘어나도 shader resource slot 사용량을 줄일 수 있다.
- 라이트별 해상도와 atlas 배치를 중앙에서 관리할 수 있다.

Point Light는 cube shadow가 필요하므로 별도 `TextureCubeArray` 리소스 풀을 사용한다.

## 10. Cascade Shadow Map

CSM, Cascade Shadow Map은 Directional Light용 그림자 품질 개선 방식이다.

카메라에서 가까운 영역과 먼 영역을 같은 해상도의 shadow map 하나에 모두 담으면 가까운 그림자의 해상도가 부족해진다. CSM은 view frustum을 거리별 cascade로 나누고, 가까운 cascade에는 더 촘촘한 shadow texel을 배치한다.

구현 흐름:

1. 카메라 frustum을 cascade split 거리 기준으로 분할한다.
2. 각 cascade frustum corner를 world space로 복원한다.
3. 각 구간을 light view space로 변환한다.
4. cascade 구간을 포함하는 orthographic projection을 만든다.
5. cascade별 shadow map을 atlas에 배치한다.
6. main pass에서 현재 픽셀의 camera depth에 맞는 cascade를 선택해 샘플링한다.

참고 자료:

- <https://learn.microsoft.com/ko-kr/windows/win32/dxtecharts/cascaded-shadow-maps>

## 11. Perspective Shadow Map

PSM, Perspective Shadow Map은 카메라에 가까운 영역에 shadow map 해상도를 더 많이 배분하기 위해 camera projection을 shadow projection 구성에 포함하는 방식이다.

기본 아이디어:

```text
World
  -> Camera View
  -> Camera Projection
  -> Post-Perspective Space
  -> Light View in Post-Perspective Space
  -> Light Projection in Post-Perspective Space
  -> Shadow Map
```

Directional Light는 world space에서는 평행광이지만, camera projection을 통과한 post-perspective space에서는 상황에 따라 finite light position처럼 취급될 수 있다. 그래서 PSM은 단순히 directional light의 orthographic projection만 쓰지 않고, 카메라와 라이트 방향 관계에 따라 post-perspective space에서 적절한 light view/projection을 구성한다.

PSM 구현에서 중요한 지점은 다음과 같다.

- camera와 light 방향 관계에 따라 projection 형태가 달라진다.
- light 방향과 view 방향이 비슷한 경우 reverse 또는 inverse projection 처리가 필요할 수 있다.
- light 방향과 view 방향이 수직에 가까울 때는 수치적으로 불안정한 구간이 생길 수 있다.
- debug line을 통해 post-perspective unit cube, light frustum, virtual camera 구성을 확인할 수 있다.

PSM debug line은 기본적으로 꺼져 있고, 콘솔 명령어로 제어한다.

```text
stat psm on
stat psm off
stat psm toggle
```

## 12. 에디터 기능

### Light Property

라이트 선택 시 Property Panel에서 shadow 관련 값을 조절할 수 있다.

- Cast Shadow
- Shadow Map Resolution
- Shadow Bias
- Shadow Slope Bias
- Shadow Sharpen
- Shadow Method: Standard / PSM / CSM

### Shadow Map Preview

Light Property에서 Shadow Map을 시각화할 수 있다.

- Directional / Spot Light는 atlas layer 또는 atlas 영역을 확인한다.
- Point Light는 cube face를 선택해 확인한다.
- PSM 모드에서는 post-perspective projection 결과를 확인할 수 있다.

### Light Perspective Override

선택한 라이트의 시점으로 editor camera를 전환할 수 있다.

이 기능은 다음 상황에서 유용하다.

- Shadow Map에 실제로 어떤 caster가 들어가는지 확인할 때
- Spot Light cone과 shadow projection이 일치하는지 확인할 때
- Directional / PSM / CSM의 light frustum을 디버깅할 때

## 13. ShowFlag와 Stat

구현된 주요 ShowFlag / Stat 기능은 다음과 같다.

- `ShowFlag.Shadows`: 그림자 표시 여부
- `ShowFlag.PSMDebugLines`: PSM debug line 표시 여부
- `stat fps`: FPS overlay 표시
- `stat memory`: 메모리 overlay 표시
- `stat light`: 라이트 통계 overlay 표시
- `stat psm on/off/toggle`: PSM debug line 표시 제어
- `stat none`: overlay stat 비활성화

통계 정보에는 다음 항목이 포함될 수 있다.

- 라이트 종류별 개수
- shadow casting light 개수
- shadow atlas 사용량
- cube shadow 사용량
- shadow map 해상도
- shadow 관련 GPU 메모리 사용량

## 14. 콘솔 명령어

현재 shadow 관련 콘솔 명령어는 다음과 같다.

```text
shadow_filter None
shadow_filter PCF
shadow_filter VSM

shadow_mode standard
shadow_mode psm
shadow_mode csm

stat light
stat memory
stat psm on
stat psm off
stat psm toggle
stat none
```

## 15. 핵심 키워드

- Depth Buffer
- Shadow Depth Pass
- Bias
- Constant Bias
- Slope-Scaled Bias
- Depth Bias
- Cast Shadow
- Cube Map
- TextureCubeArray
- Visualize Depth Map
- PCF
- VSM
- ESM
- Cascaded Shadow Map
- Cascaded Resolution
- Adaptive Shadow Maps
- Variable Shadow Resolution
- Shadow Atlas
- Perspective Shadow Map
- Light Bleeding
- Peter Panning
- Shadow Acne

## 16. 정리

이번 구현은 단순히 한 개의 Directional Light 그림자만 처리하는 것이 아니라, editor viewport에서 여러 종류의 movable light가 동시에 존재하는 상황을 대상으로 한다.

렌더링 측면에서는 Shadow Depth Pass, Shadow Atlas, Cube Shadow, PSM, CSM, PCF, VSM을 통합했고, 에디터 측면에서는 라이트별 속성 조절, shadow map preview, light perspective override, show flag, stat, console command를 연결했다.

결과적으로 사용자는 에디터에서 그림자 품질과 성능 상태를 확인하면서 Standard / PSM / CSM 및 PCF / VSM을 전환해 비교할 수 있다.
