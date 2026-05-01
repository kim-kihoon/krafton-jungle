# Point Light Cube Shadow 구현 정리

## 목표

Point light가 shadow를 만들 때 2D atlas가 아니라 cube map array를 사용하도록 렌더링 경로를 추가했다.

최종 확인 결과:

- point light별 cube shadow handle이 할당된다.
- point light 하나당 6개 face shadow pass가 생성된다.
- 각 face는 cube texture array의 개별 slice DSV에 렌더링된다.
- main forward lighting pass에서 `TextureCubeArray` SRV를 샘플링한다.
- point light shadow가 화면에 정상 렌더링되는 것을 확인했다.

## 1. Cube Shadow Pool

추가된 리소스 풀:

- `KraftonEngine/Source/Engine/Render/Resource/TexturePool/TextureCubeShadowPool.h`
- `KraftonEngine/Source/Engine/Render/Resource/TexturePool/TextureCubeShadowPool.cpp`

역할:

- point light shadow 전용 `Texture2D` cube array 리소스를 관리한다.
- texture 생성 시 `ArraySize = CubeCapacity * 6`을 사용한다.
- `D3D11_RESOURCE_MISC_TEXTURECUBE` 플래그를 사용한다.
- SRV는 `D3D11_SRV_DIMENSION_TEXTURECUBEARRAY`로 만든다.
- DSV는 face slice마다 `D3D11_DSV_DIMENSION_TEXTURE2DARRAY`로 만든다.

slice 계산:

```cpp
SliceIndex = CubeIndex * 6 + FaceIndex;
```

주요 API:

```cpp
FCubeShadowHandle Allocate();
void ReleaseHandle(FCubeShadowHandle Handle);
ID3D11DepthStencilView* GetFaceDSV(FCubeShadowHandle Handle, uint32 FaceIndex) const;
ID3D11ShaderResourceView* GetSRV() const;
```

## 2. Shadow Handle 구조

`FShadowMapKey`에 cube map handle을 추가했다.

관련 파일:

- `KraftonEngine/Source/Engine/Component/Light/LightComponent.h`
- `KraftonEngine/Source/Engine/Component/Light/PointLightComponent.h`
- `KraftonEngine/Source/Engine/Component/Light/PointLightComponent.cpp`

핵심 구조:

```cpp
struct FShadowMapKey
{
    FShadowHandleSet* Atlas = nullptr;
    FShadowCubeHandle CubeMap;
};
```

정리한 방향:

- point light는 cube shadow handle을 가진다.
- renderer는 point light 전용 public handle API가 아니라 `GetShadowMapKey().CubeMap`을 통해 접근한다.
- cube shadow handle은 point light component가 소유하고, component destruction 경로에서 release한다.

## 3. Shadow Render Task 확장

`FShadowRenderTask`가 atlas와 cube face target을 구분하도록 확장했다.

관련 파일:

- `KraftonEngine/Source/Engine/Render/Pipeline/Renderer.h`

추가된 개념:

```cpp
enum class EShadowRenderTargetType
{
    Atlas2D,
    CubeFace
};
```

cube face task는 다음 정보를 가진다.

- `TargetType = CubeFace`
- `CubeIndex`
- `CubeFaceIndex`
- face별 `LightVP`
- face별 `DSV`
- full cube face viewport

## 4. Point Light Shadow Task 생성

관련 파일:

- `KraftonEngine/Source/Engine/Render/Pipeline/Renderer.cpp`

`BuildShadowPassData()`에서 point light shadow task 생성을 추가했다.

처리 흐름:

1. shadow를 켠 point light를 수집한다.
2. camera frustum과 point light sphere bounds를 검사한다.
3. point light의 cube shadow handle을 확보한다.
4. point light 하나당 6개 face task를 만든다.
5. point light용 `FShadowInfo` 하나를 추가한다.
6. `PointLightShadowIndices[PointIndex]`가 해당 `FShadowInfo`를 가리키게 한다.

face 방향:

```cpp
// +X, -X, +Y, -Y, +Z, -Z
Forward = { +X, -X, +Y, -Y, +Z, -Z };
Up      = { +Y, +Y, -Z, +Z, +Y, +Y };
```

point shadow projection:

```cpp
FovY = 90 degrees
Aspect = 1.0
NearZ = clamp(AttenuationRadius * 0.01f, 0.05f, 5.0f)
FarZ = AttenuationRadius
```

## 5. FShadowInfo Packing

관련 파일:

- `KraftonEngine/Source/Engine/Render/Pipeline/ForwardLightData.h`
- `KraftonEngine/Shaders/Common/ForwardLightData.hlsli`

`FShadowInfo`에 shadow type과 point shadow near plane 정보를 담도록 정리했다.

```cpp
namespace EShadowInfoType
{
    constexpr uint32 Atlas2D = 0;
    constexpr uint32 CubeMap = 1;
}
```

point light shadow info:

```cpp
Info.Type = EShadowInfoType::CubeMap;
Info.ArrayIndex = CubeHandle.CubeIndex;
Info.LightIndex = PointIndex;
Info.NearZ = NearZ;
Info.LightVP = FMatrix::Identity;
Info.SampleData = FVector4(PointLightPosition, FarZ);
```

atlas shadow info:

```cpp
Info.Type = EShadowInfoType::Atlas2D;
```

## 6. Cube Shadow Resource Binding

관련 파일:

- `KraftonEngine/Source/Engine/Render/Resource/RenderResources.cpp`

기존에는 shadow cube SRV가 `nullptr`로 바인딩되고 있었다.

변경 후:

```cpp
ID3D11ShaderResourceView* ShadowCubeSRV = FTextureCubeShadowPool::Get().GetSRV();
Ctx->PSSetShaderResources(ESystemTexSlot::ShadowCubeArray, 1, &ShadowCubeSRV);
```

즉 shader의 `TextureCubeArray gShadowCubeArray : register(t23)`에 실제 cube shadow pool SRV가 전달된다.

## 7. Shadow Pass Rendering

관련 파일:

- `KraftonEngine/Source/Engine/Render/Pipeline/Renderer.cpp`

cube shadow는 face마다 별도 task로 렌더링한다.

중요한 점:

- 반드시 각 cube face slice용 DSV를 바인딩해서 렌더링한다.
- point light 하나는 6개 DSV slice에 shadow depth를 기록한다.
- 기존 atlas shadow와 같은 depth-only pass를 공유한다.
- shadow clear pass 후 pixel shader를 null로 설정해 RTV 미바인딩 warning을 없앴다.

```cpp
Ctx->OMSetRenderTargets(0, nullptr, Task.DSV);
Ctx->RSSetViewports(1, &Task.Viewport);
Ctx->PSSetShader(nullptr, nullptr, 0);
```

## 8. Face별 VP를 넘기지 않는 Cube Sampling 방식

현재 point shadow용 `FShadowInfo`는 face별 `LightVP[6]`을 shader로 넘기지 않는다.

대신 shader에서는 다음 값만 사용한다.

- point light position
- point light near/far
- cube array index
- receiver world position

이 방식의 핵심은 `worldPos - lightPos` 벡터에서 cube sampling 방향과 face 기준 depth를 동시에 재구성하는 것이다.

```hlsl
float3 toPixel = worldPos - lightPos;
float3 absToPixel = abs(toPixel);
float faceDepth = max(max(absToPixel.x, absToPixel.y), absToPixel.z);
float3 dir = toPixel / max(faceDepth, 0.0001f);
```

`dir`은 `TextureCubeArray`가 어떤 face를 샘플할지 결정하는 방향 벡터다. GPU cube sampler는 이 direction의 dominant axis를 기준으로 `+X`, `-X`, `+Y`, `-Y`, `+Z`, `-Z` 중 하나의 face를 선택하고 face 내부 UV를 계산한다.

`faceDepth`는 선택될 cube face의 view-space z에 해당하는 값이다. 예를 들어 `abs(x)`가 가장 크면 `+X` 또는 `-X` face를 보는 상황이고, 이때 face projection에서의 깊이 축 거리는 `abs(x)`가 된다.

따라서 face별 `LightVP`를 넘기지 않아도, cube direction에서 현재 receiver가 어느 face 기준으로 투영될지와 그 face 기준 깊이를 재구성할 수 있다.

이 방식의 장점:

- `FShadowInfo`가 작다.
- point light마다 `float4x4 * 6`을 GPU buffer에 넣지 않아도 된다.
- cube shadow의 일반적인 sampling 모델과 잘 맞는다.
- face별 VP 배열을 관리하지 않아도 된다.

주의할 점:

- shadow pass에서 사용한 face 방향 convention과 cube sampler의 direction convention이 맞아야 한다.
- face edge seam이나 PCF 품질 개선은 별도 처리가 필요할 수 있다.
- projection depth 공식을 shadow pass의 projection matrix와 같은 reversed-Z convention으로 맞춰야 한다.

## 9. HLSL Cube Shadow Sampling

관련 파일:

- `KraftonEngine/Shaders/Common/ForwardLighting.hlsli`

최종 구현은 `SampleCmpLevelZero()`를 사용한다.

처음 문제가 됐던 것은 `SampleCmpLevelZero()` 자체가 아니라, 비교값으로 넘기던 depth가 `dist / range`였다는 점이다.

shadow map에 저장된 값:

```text
face별 perspective projection depth
```

기존에 비교하던 값:

```text
linear distance ratio = length(worldPos - lightPos) / range
```

둘은 같은 의미의 depth가 아니므로 비교가 맞지 않았다.

최종 방식은 cube direction에서 face 기준 깊이를 구한 뒤, shadow pass에서 저장된 reversed-Z perspective depth와 같은 의미의 compare depth를 재구성한다.

```hlsl
float3 lightPos = info.SampleData.xyz;
float nearZ = max(info.NearZ, 0.0001f);
float farZ = max(info.SampleData.w, nearZ + 0.0001f);

float3 toPixel = worldPos - lightPos;
float3 absToPixel = abs(toPixel);
float faceDepth = max(max(absToPixel.x, absToPixel.y), absToPixel.z);
if (faceDepth < nearZ || faceDepth > farZ)
{
    return 1.0f;
}

float3 dir = toPixel / max(faceDepth, 0.0001f);
float depth = (nearZ * (farZ / faceDepth - 1.0f) / (farZ - nearZ)) + g_ShadowDepthBias;

return gShadowCubeArray.SampleCmpLevelZero(
    ShadowCmpSampler,
    float4(dir, info.ArrayIndex),
    depth);
```

`SampleCmpLevelZero()`는 cube shadow map을 샘플링하면서 세 번째 인자로 넘긴 receiver depth와 저장된 depth를 comparison sampler의 compare function으로 비교한다. 현재 shadow sampler는 reversed-Z에 맞춰 `D3D11_COMPARISON_GREATER_EQUAL`을 사용한다.

## 10. 검증 결과

디버그 과정에서 확인한 것:

- cube SRV가 non-null로 바인딩됐다.
- point light의 `ShadowIndex`가 light buffer에 들어갔다.
- point light 하나당 cube face task 6개가 생성됐다.
- 실제 scene에서 6개 face 중 5개 face에 geometry draw가 발생했다.
- `SampleCubeShadow()`를 강제로 0으로 만들었을 때 point light 영향이 사라졌다.
- `dist / range` 대신 face 기준 projection depth를 사용하자 point light shadow가 정상적으로 렌더링됐다.
- `SampleCmpLevelZero()` 경로에서도 정상 렌더링되는 것을 확인했다.

최종 상태에서는 디버그 로그와 강제 차폐 코드를 제거했다.

## 11. 남은 개선 후보

- cube shadow PCF 필터링 추가
- face edge seam 완화
- point light shadow resolution scale 반영
- point shadow caster culling 최적화
- cube shadow debug preview 추가
- 여러 point light shadow capacity 정책 정리

