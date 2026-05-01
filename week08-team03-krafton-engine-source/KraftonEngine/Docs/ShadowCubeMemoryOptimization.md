# Shadow Cube Memory Optimization

## 목적

Point light shadow는 cube map 기반이라 light 1개당 6개 face가 필요하다.
VSM 모드에서는 여기에 moment texture와 blur용 temporary texture까지 추가되므로, point light 수와 shadow resolution이 증가할 때 메모리 사용량이 빠르게 커진다.

이 문서는 cube shadow memory를 줄이기 위해 적용한 변경 사항과 그에 따른 동작 차이를 정리한다.

## 기존 구조의 문제

기존 `FTextureCubeShadowPool`은 초기화 시점에 모든 resolution tier의 cube texture array를 미리 생성했다.

```text
tier 0: base / 4
tier 1: base / 2
tier 2: base
tier 3: base * 2
```

point light가 실제로 특정 tier를 사용하지 않아도 해당 tier의 texture가 생성되었다.
VSM으로 전환하면 생성된 tier마다 depth texture와 moment texture들이 함께 만들어져 메모리 비용이 커졌다.

VSM point light 기준으로 기존에는 tier capacity마다 다음 리소스가 필요했다.

```text
DepthTexture             : cube array, D32
RawMomentTexture         : cube array, R32G32_FLOAT
TempMomentTexture        : cube array, R32G32_FLOAT
FilteredMomentTexture    : cube array, R32G32_FLOAT
```

1024 해상도 point light 1개 기준으로 대략 다음 크기다.

```text
Depth cube    = 1024 * 1024 * 4 bytes * 6 = 약 24 MiB
Moment cube 1 = 1024 * 1024 * 8 bytes * 6 = 약 48 MiB
Moment cube 3 = 약 144 MiB
Total         = 약 168 MiB
```

## 변경 1: tier lazy allocation

`FTextureCubeShadowPool::Initialize()`에서 모든 tier를 미리 생성하지 않도록 변경했다.

현재 초기화는 tier별 resolution만 계산하고, 실제 D3D texture는 해당 tier가 처음 할당될 때 생성한다.

```cpp
void Initialize(..., uint32 InitialCubeCapacity = 0);
```

동작 흐름은 다음과 같다.

```text
Initialize
  tier별 Resolution만 설정
  CubeCapacity는 0

Allocate(ResolutionScale)
  ResolutionScale에 맞는 TierIndex 선택
  해당 tier capacity가 0이면 capacity 1로 최초 생성
  free slot이 없으면 해당 tier만 resize
```

효과:

```text
사용하지 않는 resolution tier는 texture를 생성하지 않음
VSM 전환 시에도 이미 생성된 tier만 rebuild
point light 추가 시 해당 tier만 capacity 증가
```

주의점:

```text
처음 사용하는 tier에서는 런타임 중 texture 생성 비용이 발생할 수 있음
한 번 생성된 tier capacity는 자동으로 shrink하지 않음
미생성 tier의 SRV는 nullptr일 수 있음
```

## 변경 2: RawMomentTexture 제거

VSM blur 입력용 raw moment texture를 별도로 보관하지 않도록 변경했다.

기존 흐름:

```text
shadow pass : RawMomentTexture에 moment 기록
blur X      : RawMomentTexture -> TempMomentTexture
blur Y      : TempMomentTexture -> FilteredMomentTexture
lighting    : FilteredMomentTexture 샘플
```

변경 후 흐름:

```text
shadow pass : FilteredMomentTexture에 raw moment 기록
blur X      : FilteredMomentTexture -> TempMomentTexture
blur Y      : TempMomentTexture -> FilteredMomentTexture
lighting    : FilteredMomentTexture 샘플
```

즉 `FilteredMomentTexture`는 shadow pass 직후에는 raw moment 저장소이고, blur가 끝난 뒤에는 filtered result 저장소가 된다.

제거한 리소스:

```text
MomentTexture
RawVSMArraySRV
raw face RTV array
```

관련 변경:

```text
GetFaceVSMRTV()는 filtered face RTV를 반환
RenderVSMBlurPass()의 cube source SRV는 filtered moment array SRV 사용
UpdateMemoryStats()에서 raw moment texture 계산 제거
```

효과:

```text
VSM point cube capacity 1개당 moment texture 1벌 절감
1024 point light 1개 기준 약 48 MiB 절감
```

side effect:

```text
blur 전 raw moment와 blur 후 filtered moment를 동시에 비교할 수 없음
blur pass가 실패하면 lighting이 raw moment 상태의 FilteredMomentTexture를 샘플할 수 있음
D3D11 SRV/RTV hazard 방지를 위해 blur pass 전후 unbind가 중요함
```

## 변경 3: VSM shared depth

VSM 모드에서는 lighting이 depth cube를 직접 샘플하지 않는다.
lighting shader는 최종 moment cube array를 샘플한다.

따라서 VSM의 depth texture는 shadow pass 중 depth test에만 필요하다.
이 점을 이용해 VSM 모드에서는 point light별 depth cube array를 보관하지 않고, tier별 1장짜리 shared depth texture를 재사용하도록 변경했다.

기존 VSM depth:

```text
DepthTexture.ArraySize = CubeCapacity * 6
DepthTexture.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE
FaceDSVs.size()        = CubeCapacity * 6
```

변경 후 VSM depth:

```text
DepthTexture.ArraySize = 1
DepthTexture.MiscFlags = 0
FaceDSVs.size()        = 1
```

VSM moment texture는 여전히 cube array로 유지한다.

```text
TempMomentTexture.ArraySize     = CubeCapacity * 6
FilteredMomentTexture.ArraySize = CubeCapacity * 6
MomentTexture.MiscFlags         = D3D11_RESOURCE_MISC_TEXTURECUBE
```

렌더링 흐름:

```text
point light A face 0
  shared depth DSV clear
  filtered moment slice 0 RTV에 기록

point light A face 1
  같은 shared depth DSV clear
  filtered moment slice 1 RTV에 기록

point light B face 0
  같은 shared depth DSV clear
  filtered moment slice 6 RTV에 기록
```

`GetFaceDSV()`는 VSM 모드일 때 항상 tier의 shared DSV를 반환한다.

```text
VSM  : FaceDSVs[0]
PCF  : FaceDSVs[CubeIndex * 6 + FaceIndex]
```

효과:

```text
VSM point light 수가 증가해도 depth texture memory는 point light 수에 비례해서 증가하지 않음
1024 기준 depth cube 약 24 MiB / point light 비용을 tier별 약 4 MiB 수준으로 줄임
```

side effect:

```text
VSM에서는 영구 보관된 depth cube map preview를 제공할 수 없음
property preview는 filtered moment map 기반 preview를 제공
각 face render 전에 shared depth clear가 반드시 필요함
동시에 여러 cube face를 렌더링하는 구조로 바꾸면 shared depth 동기화가 필요함
PCF는 depth cube를 lighting에서 직접 샘플하므로 shared depth를 사용할 수 없음
```

## 현재 PCF와 VSM 리소스 구조

PCF:

```text
DepthTexture
  ArraySize = CubeCapacity * 6
  BindFlags = DEPTH_STENCIL | SHADER_RESOURCE
  MiscFlags = TEXTURECUBE

SRV
  TextureCubeArray

DSV
  face별 DSV
```

VSM:

```text
DepthTexture
  ArraySize = 1
  BindFlags = DEPTH_STENCIL
  MiscFlags = 0

FilteredMomentTexture
  ArraySize = CubeCapacity * 6
  BindFlags = RENDER_TARGET | SHADER_RESOURCE
  MiscFlags = TEXTURECUBE

TempMomentTexture
  ArraySize = CubeCapacity * 6
  BindFlags = RENDER_TARGET | SHADER_RESOURCE
  MiscFlags = TEXTURECUBE

SRV
  lighting용 TextureCubeArray SRV는 FilteredMomentTexture를 가리킴
  blur/debug용 Texture2DArray SRV도 FilteredMomentTexture 또는 TempMomentTexture를 가리킴

DSV
  tier별 shared DSV 1개

RTV
  moment face별 RTV
```

## 메모리 증가 방식

cube pool capacity는 tier별로 관리된다.
point light를 추가해도 모든 tier가 커지는 것이 아니라, 해당 point light의 `ShadowResolutionScale`이 선택한 tier만 커진다.

```text
scale <= 0.25 : tier 0
scale <= 0.5  : tier 1
scale <= 1.0  : tier 2
scale > 1.0   : tier 3
```

capacity 증가는 현재 다음 방식이다.

```text
0 -> 1 -> 2 -> 4 -> 8 ...
```

D3D11 texture array는 array size를 직접 늘릴 수 없으므로, capacity가 증가하면 해당 tier의 texture들을 새 capacity로 다시 생성한다.

## 남은 개선 여지

현재도 VSM point light의 moment texture는 point light 수에 비례해서 증가한다.
더 줄이려면 다음 선택지가 있다.

```text
VSM 모드에서 capacity 증가 정책을 2배 증가 대신 +1로 변경
point light VSM 사용을 옵션화하고 기본은 PCF로 유지
debug preview가 필요할 때만 별도 depth cube를 임시 생성
사용하지 않는 tier 또는 capacity를 shrink하는 정책 추가
```

다만 `+1` 증가는 메모리는 아끼지만 resize/rebuild 빈도를 늘린다.
에디터에서는 메모리 절감을 우선할 수 있지만, 런타임에서는 scene load 시 필요한 tier를 prewarm하는 방식이 더 적합할 수 있다.

## 검증

변경 후 다음 빌드를 통과했다.

```powershell
msbuild KraftonEngine.sln /p:Configuration=Debug /p:Platform=x64
```

결과:

```text
warning 0
error 0
```
