# Camera Manager and Camera Modifier

## 1. 구현 목적

이번 작업의 핵심 목적은 카메라 관련 기능을 `ViewportClient`나 개별 렌더 패스에 흩어두지 않고, Unreal Engine의 `PlayerCameraManager` 방식처럼 한 곳에서 최종 카메라 상태를 만들도록 정리하는 것이었다.

기존에는 카메라 컴포넌트나 free camera가 직접 `FSceneView`를 만들고 렌더러에 넘기는 구조에 가까웠다. 이 방식은 단순한 카메라 렌더링에는 충분하지만, 카메라 흔들림, 페이드, 레터박스, 포스트 프로세스, Lua 기반 카메라 효과처럼 여러 효과가 동시에 들어가기 시작하면 어느 코드가 최종 카메라 상태를 책임지는지 불분명해진다.

그래서 현재 구조에서는 `APlayerCameraManager`가 다음 역할을 담당한다.

- 현재 카메라 기준 뷰를 만든다.
- 카메라 modifier들을 순서대로 적용한다.
- 최종 카메라, 포스트 프로세스, 오버레이 값을 캐싱한다.
- 이 최종 값을 `FSceneView`에 담아 렌더러에 전달한다.

관련 파일:

- `NipsEngine/Source/Engine/Camera/PlayerCameraManager.h`
- `NipsEngine/Source/Engine/Camera/PlayerCameraManager.cpp`
- `NipsEngine/Source/Engine/Runtime/SceneView.h`

## 2. 전체 데이터 흐름

현재 카메라 효과의 전체 흐름은 다음과 같다.

```text
CameraComponent / FViewportCamera
        ↓
APlayerCameraManager::BuildBaseCameraView()
        ↓
FCameraViewInfo
        ↓
CameraModifier 적용
        ↓
CachedCameraView
CachedPostProcessSettings
CachedCameraOverlaySettings
        ↓
APlayerCameraManager::FillSceneView()
        ↓
FSceneView
        ↓
RenderBus
        ↓
RenderPass
        ↓
Shader / 화면 출력
```

여기서 중요한 점은 renderer가 직접 modifier를 알지 않는다는 것이다. renderer는 `FSceneView`만 보고 화면을 그린다. 즉 modifier는 렌더러 앞단에서 최종 view data를 만드는 역할이고, render pass는 이미 결정된 `FSceneView` 값을 사용해 화면 효과를 적용한다.

## 3. APlayerCameraManager의 역할

`APlayerCameraManager::UpdateCamera()`는 매 프레임 최종 카메라 상태를 갱신한다.

```cpp
BuildBaseCameraView(NewView);
UpdateCameraTransition(DeltaTime, NewView);
UpdateCameraFade(DeltaTime);
SyncLuaCameraModifierComponents();

ApplyCameraModifiers(DeltaTime, NewView);
ApplyPostProcessComponent(NewPostProcess);
ApplyPostProcessModifiers(DeltaTime, NewPostProcess);
ApplyOverlayModifiers(DeltaTime, NewOverlay);
ApplyCameraFade(NewOverlay);

CachedCameraView = NewView;
CachedPostProcessSettings = NewPostProcess;
CachedCameraOverlaySettings = NewOverlay;
```

의미는 다음과 같다.

- `BuildBaseCameraView`: 현재 활성 카메라 컴포넌트 또는 fallback camera로부터 기본 카메라 상태를 만든다.
- `ApplyCameraModifiers`: 흔들림, FOV 변화, 위치 보정 같은 view 자체 변경을 적용한다.
- `ApplyPostProcessModifiers`: 감마, 비네팅 같은 후처리 값을 적용한다.
- `ApplyOverlayModifiers`: 레터박스 같은 최종 화면 overlay 값을 적용한다.
- `ApplyCameraFade`: camera fade 상태를 overlay 값으로 변환한다.

이후 `BuildSceneView()`에서 캐싱된 값을 `FSceneView`로 복사한다.

## 4. FSceneView의 의미

`FSceneView`는 renderer가 한 프레임을 그릴 때 필요한 최종 view snapshot이다.

현재 `FSceneView`에는 다음 값들이 들어간다.

- View matrix
- Projection matrix
- Camera position
- Camera forward/right/up
- Near/Far plane
- View mode
- `FPostProcessSettings`
- `FCameraOverlaySettings`

관련 구조체:

```cpp
struct FSceneView
{
    FMatrix View;
    FMatrix Proj;

    FVector CameraPosition;
    FVector CameraForward;
    FVector CameraRight;
    FVector CameraUp;

    FPostProcessSettings PostProcessSettings;
    FCameraOverlaySettings CameraOverlaySettings;
};
```

즉 `FSceneView`는 단순히 view/projection matrix만 담는 구조체가 아니라, 렌더러가 사용할 카메라 기반 후처리 설정까지 함께 담는 프레임 단위 데이터이다.

## 5. Modifier 종류별 처리 위치

현재 modifier는 크게 세 종류로 나뉜다.

### 5.1 Camera modifier

Camera modifier는 렌더 패스에서 처리되지 않는다. 렌더링 전에 `FCameraViewInfo`를 수정한다.

예시:

- Camera shake
- FOV offset
- Location offset

`UCameraShakeModifier`는 다음 값을 바꾼다.

```cpp
InOutView.Rotation.X += ...
InOutView.Location.X += ...
InOutView.FOV += ...
```

이후 `FillSceneView()`에서 이 값으로 view/projection matrix를 만든다.

```cpp
OutView.View = FMatrix::MakeViewLookAtLH(...);
OutView.Proj = FMatrix::MakePerspectiveFovLH(CameraView.FOV, ...);
```

따라서 CameraShake는 특정 post process pass에 있는 효과가 아니라, 모든 렌더 패스가 공유하는 카메라 view 자체를 변경하는 효과이다.

관련 파일:

- `NipsEngine/Source/Engine/Camera/Modifier/CameraShakeModifier.cpp`
- `NipsEngine/Source/Engine/Camera/PlayerCameraManager.cpp`

### 5.2 PostProcess modifier

PostProcess modifier는 `FPostProcessSettings`를 수정하고, 실제 화면 처리는 `FPostProcessRenderPass`에서 한다.

현재 여기에 들어가는 효과:

- Gamma
- Vignette

`APlayerCameraManager`는 modifier와 post process component 값을 모아 `CachedPostProcessSettings`에 저장한다.

```cpp
CachedPostProcessSettings = NewPostProcess;
```

그 값은 `FSceneView`에 들어가고:

```cpp
OutView.PostProcessSettings = CachedPostProcessSettings;
```

`FPostProcessRenderPass`가 `RenderBus`를 통해 읽는다.

```cpp
const FPostProcessSettings& PostProcess = Context->RenderBus->GetPostProcessSettings();
ShaderBinding->SetFloat("Gamma", PostProcess.Gamma);
ShaderBinding->SetFloat("VignetteIntensity", PostProcess.VignetteIntensity);
ShaderBinding->SetFloat("VignetteRadius", PostProcess.VignetteRadius);
ShaderBinding->SetFloat("VignetteSoftness", PostProcess.VignetteSoftness);
```

shader에서는 `PostProcessPass.hlsl`이 실제 계산을 수행한다.

```hlsl
color.rgb *= lerp(1.0f, 1.0f - vignette, vignetteIntensity);
color.rgb = pow(saturate(color.rgb), 1.0f / safeGamma);
```

관련 파일:

- `NipsEngine/Source/Engine/Render/Renderer/RenderFlow/PostProcessRenderPass.cpp`
- `NipsEngine/Shaders/Multipass/PostProcessPass.hlsl`

### 5.3 Overlay modifier

Overlay modifier는 최종 화면 위에 덮는 효과이다. 현재 `FPostProcessOutlineRenderPass`의 마지막 fullscreen overlay 단계에서 처리한다.

현재 여기에 들어가는 효과:

- LetterBox
- Fade

`ULetterBoxCameraModifier`는 직접 render pass를 호출하지 않고 `FCameraOverlaySettings`에 값만 넣는다.

```cpp
InOutOverlay.LetterBoxRatio = MathUtil::Clamp(CurrentRatio, 0.0f, 0.5f);
```

`APlayerCameraManager`는 이 값을 캐싱하고 `FSceneView`에 복사한다.

```cpp
CachedCameraOverlaySettings = NewOverlay;
OutView.CameraOverlaySettings = CachedCameraOverlaySettings;
```

`FPostProcessOutlineRenderPass`는 `RenderBus`에서 overlay 값을 읽어 `FinalOverlayPass.hlsl`에 전달한다.

```cpp
const FCameraOverlaySettings& Overlay = Context->RenderBus->GetCameraOverlaySettings();

FinalOverlayShaderBinding->SetVector4("FadeColor", FVector4(Overlay.FadeColor));
FinalOverlayShaderBinding->SetFloat("LetterBoxRatio", Overlay.LetterBoxRatio);
```

shader에서는 레터박스 영역을 검정으로 덮고, 그 후 fade를 적용한다.

```hlsl
const float ratio = saturate(LetterBoxRatio);
if (uv.y < ratio || uv.y > 1.0f - ratio)
{
    color.rgb = float3(0.0f, 0.0f, 0.0f);
}

const float fadeAlpha = saturate(FadeColor.a);
color.rgb = lerp(color.rgb, FadeColor.rgb, fadeAlpha);
```

관련 파일:

- `NipsEngine/Source/Engine/Camera/Modifier/LetterBoxCameraModifier.cpp`
- `NipsEngine/Source/Engine/Render/Renderer/RenderFlow/PostProcessOutlineRenderPass.cpp`
- `NipsEngine/Shaders/Multipass/FinalOverlayPass.hlsl`

## 6. Render Pass 순서

현재 관련 pass 순서는 다음과 같다.

```cpp
RenderPasses.push_back(FXAARenderPass);
RenderPasses.push_back(FontRenderPass);
RenderPasses.push_back(SubUVRenderPass);
RenderPasses.push_back(BillboardRenderPass);
RenderPasses.push_back(TranslucentRenderPass);
RenderPasses.push_back(SelectionMaskRenderPass);
RenderPasses.push_back(GridRenderPass);
RenderPasses.push_back(EditorRenderPass);
RenderPasses.push_back(DepthLessRenderPass);
RenderPasses.push_back(PostProcessRenderPass);
RenderPasses.push_back(PostProcessOutlineRenderPass);
```

이 순서의 의미는 다음과 같다.

- `PostProcessRenderPass`는 Grid, SubUV, Billboard, Font 등 화면에 보이는 대부분의 결과가 끝난 뒤 실행된다.
- 그래서 Gamma/Vignette는 최종 scene color 전체에 적용된다.
- `PostProcessOutlineRenderPass`는 마지막 pass다.
- 그래서 LetterBox/Fade는 최종 present 직전에 화면 전체에 덮인다.

관련 파일:

- `NipsEngine/Source/Engine/Render/Renderer/RenderFlow/RenderPipeline.cpp`

## 7. Lua Camera Modifier

Lua camera modifier는 스크립트에 함수를 작성하지 않고, 데이터만 넣어도 동작하도록 구성했다.

예시:

```lua
return {
    Modifiers = {
        {
            Type = "PostProcess",
            Gamma = 2.2,
            VignetteIntensity = 1.0,
            VignetteRadius = 0.35,
            VignetteSoftness = 0.45,
        },

        {
            Type = "LetterBox",
            TargetRatio = 0.08,
            Duration = 1.0,
        },

        {
            Type = "Fade",
            Color = { R = 0.0, G = 0.0, B = 0.0, A = 1.0 },
            FromAlpha = 0.0,
            ToAlpha = 0.25,
            Duration = 1.0,
            Hold = true,
        },
    },
}
```

Lua 파일의 `Type`에 따라 C++이 적절한 modifier 처리로 변환한다.

- `Type = "Camera"`: `FCameraViewInfo` 변경
- `Type = "PostProcess"`: `FPostProcessSettings` 변경
- `Type = "Overlay"`: `FCameraOverlaySettings` 변경
- `Type = "Fade"`: `StartCameraFade()` 호출
- `Type = "LetterBox"`: `StartLetterBox()` 호출
- `Type = "Shake"`: `StartCameraShake()` 호출

관련 파일:

- `NipsEngine/Source/Engine/Camera/Modifier/LuaCameraModifier.cpp`
- `NipsEngine/Source/Engine/Component/LuaCameraModifierComponent.cpp`
- `NipsEngine/Asset/Scripts/TestLuaCameraModifier.lua`

## 8. 왜 이렇게 나눴는가

효과를 pass 기준으로만 생각하면 모든 효과를 하나의 post process pass에 넣을 수도 있다. 하지만 현재 구조에서는 효과의 성격에 따라 처리 위치를 나눴다.

Camera modifier는 view/projection 자체를 바꾸므로 render pass 이전에 적용해야 한다.

PostProcess modifier는 이미 그려진 scene color의 색을 보정하므로 일반 post process pass에서 처리한다.

Overlay modifier는 letterbox나 fade처럼 최종 화면 전체를 덮는 효과이므로 가장 마지막 pass에서 처리한다.

이렇게 나누면 각 단계의 책임이 명확해진다.

```text
Camera modifier     = 카메라가 어디서 무엇을 보는가
PostProcess modifier = 그려진 화면의 색을 어떻게 보정하는가
Overlay modifier    = 최종 화면 위에 무엇을 덮는가
```

## 9. 요약

이번 구현은 카메라 효과를 단순히 렌더 패스에 추가한 것이 아니라, Unreal Engine의 `PlayerCameraManager`와 유사하게 최종 카메라 상태를 한 곳에서 구성하도록 만든 작업이다.

`APlayerCameraManager`는 기본 카메라 정보를 가져오고, 여러 modifier를 적용한 뒤, 최종 카메라 상태를 `FSceneView`에 담는다. 렌더러는 modifier를 직접 알 필요 없이 `FSceneView`와 `RenderBus`를 통해 전달된 값만 사용한다.

효과별로 보면 CameraShake는 view 자체를 바꾸기 때문에 별도 pass가 없고, Gamma와 Vignette는 `PostProcessRenderPass`에서 처리된다. LetterBox와 Fade는 최종 화면 overlay 성격이므로 마지막 pass인 `PostProcessOutlineRenderPass`에서 `FinalOverlayPass.hlsl`을 통해 처리된다.

Lua camera modifier는 데이터만 작성하면 C++이 해당 데이터를 읽어 modifier 동작으로 변환한다. 따라서 디자이너나 스크립트 작성자는 C++ 코드를 수정하지 않고도 카메라 흔들림, 페이드, 레터박스, 감마, 비네팅 같은 효과를 조합할 수 있다.
