# Light Component Camera Override Implementation

## 요약

현재 구현에서 `light component`의 `camera override`는 라이트 컴포넌트 고유 property가 아니다.  
실제 소유자는 각 에디터 뷰포트의 `FViewportRenderOptions::bOverrideCameraWithSelectedLight`이고, 선택된 라이트를 기준으로 **활성 뷰포트 카메라를 preview 모드로 동기화**하는 editor-side 기능이다.

중요한 점은 예전처럼 매 프레임 카메라를 강제로 덮어쓰는 구조가 아니라는 점이다.  
지금은 다음 시점에만 카메라를 다시 맞춘다.

- preview 시작 시
- preview 대상 light가 바뀔 때
- preview 중인 light의 transform/property가 바뀔 때

즉 구조적으로는 `selected light driven viewport preview`에 가깝다.

---

## 1. UI 진입점

체크박스는 라이트 컴포넌트 상세 패널에서 노출된다.

- 파일: `KraftonEngine/Source/Editor/UI/EditorPropertyWidget.cpp`
- 함수:
  - `RenderComponentProperties()`
  - `RenderLightShadowSettings(ULightComponent* LightComponent)`

핵심 코드는 아래 흐름이다.

1. 선택된 컴포넌트가 `ULightComponent`이면 `RenderLightShadowSettings()`를 호출
2. `Override camera with light's perspective` 체크박스를 그림
3. 체크 결과를 활성 뷰포트의 `RenderOptions.bOverrideCameraWithSelectedLight`에 반영

관련 코드 위치:

- `RenderLightShadowSettings(...)`: `EditorPropertyWidget.cpp:555`
- 체크박스 값 읽기: `EditorPropertyWidget.cpp:604`
- 체크박스 반영: `EditorPropertyWidget.cpp:605-607`

즉 UI는 라이트 패널에 있지만, 실제 데이터는 라이트 컴포넌트가 아니라 뷰포트 옵션에 저장된다.

---

## 2. 실제 상태 소유자

플래그 자체는 `FViewportRenderOptions`에 있다.

- 파일: `KraftonEngine/Source/Engine/Render/Types/ViewTypes.h`
- 필드:
  - `bool bOverrideCameraWithSelectedLight = false;`

이 구조체는 뷰포트별 렌더 옵션을 담는다. 따라서 이 기능은:

- 라이트 asset/property가 아니라
- viewport-local editor state 이고
- 뷰포트마다 독립적으로 켜고 끌 수 있다

관련 위치:

- `ViewTypes.h:90`

---

## 3. 저장/복원 범위

이 옵션은 editor settings를 통해 저장된다.

- 파일: `KraftonEngine/Source/Editor/Settings/EditorSettings.cpp`
- 저장:
  - `EditorSettings.cpp:127`
- 로드:
  - `EditorSettings.cpp:284-285`

즉 이 값은:

- scene/light serialization 대상이 아니고
- editor viewport 설정으로 저장되며
- 프로젝트를 다시 열어도 뷰포트 옵션으로 복원된다

---

## 4. 핵심 클래스와 역할

### 4-1. `FEditorViewportClient`

실제 preview 상태를 관리하는 중심 클래스다.

- 파일: `KraftonEngine/Source/Editor/Viewport/EditorViewportClient.h`

중요 멤버:

- `bool bLightCameraOverrideActive`
- `ULightComponent* PreviewLightComponent`
- `FCameraOverrideSnapshot CameraOverrideSnapshot`

중요 함수:

- `TickLightCameraOverride()`
- `GetSelectedLight()`
- `EnterLightCameraOverride(ULightComponent*)`
- `ExitLightCameraOverride()`
- `SyncLightCameraOverride()`
- `OnLightComponentChanged(ULightComponentBase*)`

### 4-2. `ULightComponentBase`

preview 대상 light가 수정되었을 때 editor 쪽에 변경 이벤트를 올리는 역할을 한다.

- 파일: `KraftonEngine/Source/Engine/Component/Light/LightComponentBase.h`
- 파일: `KraftonEngine/Source/Engine/Component/Light/LightComponentBase.cpp`

사용 지점:

- `OnTransformDirty()`
- `PostEditProperty(const char* PropertyName)`

이 두 경로에서 `PushToScene()` 뒤에 `UEditorEngine::NotifyLightComponentChanged(this)`를 호출한다.

### 4-3. `UEditorEngine`

light 변경 알림을 모든 level viewport로 fan-out 한다.

- 파일: `KraftonEngine/Source/Editor/EditorEngine.h`
- 파일: `KraftonEngine/Source/Editor/EditorEngine.cpp`

핵심 함수:

- `NotifyLightComponentChanged(ULightComponentBase* LightComponent)`

이 함수는 모든 `FLevelEditorViewportClient`에 `OnLightComponentChanged()`를 전달한다.

---

## 5. 현재 동작 흐름

### 5-1. 프레임마다 하는 일

`FEditorViewportClient::Tick()`는 매 프레임 호출되지만, 이제 카메라를 매 프레임 재설정하지는 않는다.

흐름:

1. `TickEditorShortcuts()`
2. `TickLightCameraOverride()`
3. 일반 입력 처리 또는 directional preview 입력 처리
4. `TickInteraction()`

관련 위치:

- `EditorViewportClient.cpp:150` 부근

### 5-2. preview 진입/유지/해제

`TickLightCameraOverride()`는 다음 역할만 한다.

- 현재 선택된 light를 찾는다
- override 조건이 맞으면 preview 모드로 진입한다
- 선택된 light가 바뀌면 preview target만 교체하고 즉시 sync한다
- 조건이 깨지면 snapshot을 복원하고 종료한다

관련 코드:

- `GetSelectedLight()`: `EditorViewportClient.cpp:211`
- `TickLightCameraOverride()`: `EditorViewportClient.cpp:376`

즉 지금 `Tick`의 역할은:

- 상태 전이 감지
- 선택 대상 변경 감지

까지만 담당한다.

실제 카메라 동기화는 `SyncLightCameraOverride()`가 담당한다.

---

## 6. 선택된 라이트를 찾는 방식

현재는 primary selection actor에서 첫 번째 `ULightComponent`를 찾는다.

흐름:

1. `SelectionManager->GetPrimarySelection()`으로 actor 획득
2. actor의 `GetComponents()` 순회
3. 첫 번째 `ULightComponent`를 반환

관련 위치:

- `EditorViewportClient.cpp:211-230`

의미:

- light actor 하나를 기준으로 동작
- light component가 여러 개면 첫 번째 것만 사용
- multi-light preview 구조는 아님

---

## 7. preview 시작 시 저장하는 상태

preview 진입 시 원래 카메라 상태를 `FCameraOverrideSnapshot`에 저장한다.

- 파일: `KraftonEngine/Source/Editor/Viewport/EditorViewportClient.h`
- 구조체: `FCameraOverrideSnapshot`

저장 필드:

- `Location`
- `FocusPoint`
- `Rotation`
- `FOV`
- `NearZ`
- `FarZ`
- `OrthoWidth`
- `bIsOrthographic`
- `bWasGizmoEnabled`
- `ViewportType`

저장 함수:

- `SaveCameraOverrideSnapshot()`
- 위치: `EditorViewportClient.cpp:173`

`FocusPoint`는 현재 카메라 위치에서 `Forward * 100.0f`로 계산한다.  
이 값은 point/directional preview에서 기준점으로 재사용된다.

---

## 8. preview 종료 시 복원하는 상태

preview 종료 시에는 snapshot을 그대로 되돌린다.

복원 함수:

- `RestoreCameraOverrideSnapshot()`
- 위치: `EditorViewportClient.cpp:192`

복원 내용:

- 카메라 위치/회전
- `FOV`
- `NearZ/FarZ`
- `OrthoWidth`
- orthographic 여부
- viewport type
- gizmo 활성 상태

그 뒤 `CameraOverrideSnapshot = {};` 로 초기화한다.

즉 이 기능은 본질적으로:

- 진입 시 editor camera snapshot 저장
- preview camera로 전환
- 해제 시 원래 editor camera 복원

구조다.

---

## 9. event-driven sync 구조

### 9-1. 왜 바꿨는가

예전 방식은 preview 활성 중 `Tick`에서 매 프레임 카메라를 light 시점으로 다시 맞췄다.  
지금은 그 대신, **light가 실제로 바뀐 경우에만 sync**한다.

장점:

- 불필요한 per-frame camera overwrite 제거
- editor camera 시스템과 preview 로직 결합도 감소
- light 변경 시점이 코드상 더 명확해짐

### 9-2. light 변경 감지

`ULightComponentBase`에서 다음 두 경로가 editor에 이벤트를 보낸다.

- `OnTransformDirty()`
- `PostEditProperty(...)`

구현 위치:

- `LightComponentBase.cpp:23-35`

핵심 흐름:

1. light transform 또는 property 변경
2. `PushToScene()` 수행
3. `NotifyEditorLightPreviewChanged(this)`
4. 내부에서 `UEditorEngine::NotifyLightComponentChanged(this)` 호출
5. editor engine이 모든 level viewport client에 전달
6. 각 viewport는 자기 preview target과 같을 때만 `SyncLightCameraOverride()` 실행

전달 위치:

- `EditorEngine.cpp:113-126`
- `EditorViewportClient.cpp:159-171`

즉 지금 sync는 `light change event -> matching viewport sync` 방식이다.

---

## 10. preview 대상별 카메라 설정 방식

모든 preview 동기화는 `FEditorViewportClient::SyncLightCameraOverride()`에서 처리한다.

- 파일: `KraftonEngine/Source/Editor/Viewport/EditorViewportClient.cpp`
- 위치: `EditorViewportClient.cpp:264`

공통 처리:

- `NearZ = 0.1f`
- `FarZ = 1000.0f`
- `PreviewBackoff = max(NearZ * 2.0f, 0.25f)`

또한 orientation은 `LookAt()` 대신 실제 라이트 basis를 쓰도록 바뀌었다.

보조 함수:

- `SetCameraOrientationFromBasis(...)`
- `SetCameraLookDirection(...)`

위치:

- `EditorViewportClient.cpp` 상단 namespace 내부

### 10-1. SpotLight

조건:

- `PreviewLightComponent->IsA<USpotLightComponent>()`

동작:

- 카메라 위치 = `LightLocation + LightForward * PreviewBackoff`
- 카메라 회전 = light의 `Forward/Right/Up` basis 그대로 복사
- perspective 사용
- FOV = `OuterConeAngle * 2`를 radian으로 변환 후 clamp

관련 코드:

- `EditorViewportClient.cpp:281-291`

의미:

- 스포트라이트가 실제로 빛을 쏘는 방향과 카메라 방향이 일치
- 카메라를 광원 원점보다 약간 앞쪽에 둬서 전환 직후 near clip 겹침 문제를 줄임

### 10-2. PointLight

조건:

- `PreviewLightComponent->IsA<UPointLightComponent>()`

동작:

- 카메라 위치 = `LightLocation + LightForward * PreviewBackoff`
- 카메라 방향 = 저장된 `FocusPoint - LightLocation`
- perspective 사용
- FOV = 90도

관련 코드:

- `EditorViewportClient.cpp:292-300`

제한:

- 현재 point light의 진짜 6-face cubemap preview는 구현되지 않음
- TODO도 코드에 남아 있음

즉 지금 point light preview는 임시 성격이다.

### 10-3. DirectionalLight

spot/point가 아니면 directional 계열로 취급한다.

동작:

- 카메라 위치 = `FocusPoint - LightForward * 100.0f`
- 카메라 회전 = light의 `Forward/Right/Up` basis 그대로 복사
- orthographic 강제
- `OrthoWidth = 100.0f`
- `ViewportType = ELevelViewportType::FreeOrthographic`

관련 코드:

- `EditorViewportClient.cpp:301-311`

의미:

- directional light에는 위치 개념보다 방향이 중요하므로
- 현재 바라보던 focus 영역을 light 방향 기준 orthographic으로 보는 preview가 된다

---

## 11. directional preview에서 입력을 일부 허용하는 이유

preview 중 일반적으로는 사용자가 카메라를 임의로 움직이지 못하게 막는다.  
하지만 directional light는 위치가 본질이 아니고, orthographic pan/zoom이 필요해서 예외를 둔다.

`Tick()`의 조건:

- preview active
- target light가 point/spot이 아님
- camera가 orthographic

이면 `TickInput()`을 허용한다.

관련 코드:

- `EditorViewportClient.cpp:155-168`

결과:

- spot/point preview에서는 입력 제한
- directional preview에서는 ortho pan/zoom 허용

그래서 directional light 시점에서는:

- 우클릭 드래그로 상하좌우 pan
- 스크롤로 ortho zoom

이 가능하다.

---

## 12. sync 이후 occlusion invalidation

preview 카메라를 큰 폭으로 바꾸는 경우 이전 프레임의 occlusion readback이 남을 수 있어서, sync 직후 occlusion 결과를 무효화한다.

관련 코드:

- `EditorViewportClient.cpp:315-319`
- `EditorEngine.h:46`

호출:

- `UEditorEngine::InvalidateOcclusionResults()`

이건 stale occlusion 방지용 보조 처리다.

---

## 13. 실제 렌더 파이프라인에는 어떻게 반영되는가

별도의 preview camera 객체를 렌더러가 따로 쓰는 구조가 아니다.  
그냥 editor viewport가 원래 가지고 있던 `UCameraComponent` 자체를 바꾼다.

흐름:

1. viewport client가 자기 `Camera`를 override 상태로 세팅
2. editor render pipeline이 viewport client의 `Camera`를 읽음
3. `FFrameContext`가 그 카메라 정보로 채워짐
4. 이후 렌더링이 그대로 그 카메라 기준으로 진행

관련 파일:

- `KraftonEngine/Source/Editor/EditorRenderPipeline.cpp`
- `KraftonEngine/Source/Engine/Render/Pipeline/FrameContext.cpp`

즉 이 기능은 shadow-map 전용 뷰가 아니라, **메인 editor viewport camera 자체를 라이트 시점으로 전환하는 방식**이다.

---

## 14. directional ortho grid 문제와 수정 방식

### 14-1. 원래 왜 문제가 생겼는가

기존 world grid는 사실상 axis-aligned 평면 기반이었다.  
`Top/Bottom/Left/Right/Front/Back`처럼 축 고정 orthographic view에서는 괜찮지만, directional light preview처럼 임의 방향의 `FreeOrthographic`에서는 빈 공간이 생길 수 있었다.

원래 문제는 두 단계였다.

1. `FreeOrthographic`가 `IsFixedOrtho()`에 포함되지 않아 ortho grid 확장 로직이 덜 적용됨
2. 더 근본적으로는 grid를 `XY/XZ/YZ` 같은 world 축 평면에 맞춰 만들고 있었기 때문에, 비스듬한 ortho 카메라에서는 화면 전체를 정확히 덮지 못함

### 14-2. 현재 수정된 방식

지금은 `FreeOrthographic`일 때 world 축 평면을 쓰지 않고, **카메라의 `Right/Up` 평면 위에 직접 grid를 생성**한다.

관련 파일:

- `KraftonEngine/Source/Engine/Render/Helper/LineGeometry.h`
- `KraftonEngine/Source/Engine/Render/Helper/LineGeometry.cpp`
- `KraftonEngine/Source/Engine/Render/Pipeline/DrawCommandBuilder.cpp`

핵심 변경:

- `FLineGeometry::AddWorldHelpers(...)`에 `bUseCameraPlaneGrid` 인자 추가
- `DrawCommandBuilder.cpp`에서 `ViewportType == ELevelViewportType::FreeOrthographic`일 때 이 값을 `true`로 전달

### 14-3. camera-plane grid 생성 로직

`bUseCameraPlaneGrid && bIsOrtho`일 때:

1. `OrthoWidth`와 `ViewAspect`로 화면 half width/height 계산
2. camera position을 `CameraRight`, `CameraUp` 축에 투영해 현재 grid center 계산
3. 화면 범위를 덮도록 min/max U/V 범위를 계산
4. spacing 기준으로 snap된 line 위치를 구함
5. `CameraRight` 방향 선, `CameraUp` 방향 선을 직접 생성

관련 코드:

- `LineGeometry.cpp:170` 부근

의미:

- directional light preview처럼 비스듬한 orthographic 시점에서도
- 줌아웃 시 grid가 카메라 화면을 자연스럽게 끝까지 덮게 됨

### 14-4. 고정 ortho와의 차이

여기서 말하는 고정 ortho는:

- `Top`
- `Bottom`
- `Left`
- `Right`
- `Front`
- `Back`

같은 축 정렬 orthographic view다.

반면 directional preview는:

- `FreeOrthographic`
- 투영은 ortho지만 각도는 자유로운 상태

라서 같은 grid 처리 경로를 쓰면 안 된다.

---

## 15. 현재 구조가 의미하는 것

현재 구현을 설계 관점에서 정리하면 다음과 같다.

### 15-1. 이것은 light component property가 아니다

아래 중 어디에도 저장되지 않는다.

- `ULightComponent`
- `ULightComponentBase`
- scene serialization

대신 아래에 저장된다.

- viewport render options
- editor settings

### 15-2. 이것은 PSM preview가 아니다

이 기능은 shadow map depth texture를 직접 보여주지 않는다.  
그냥 메인 editor viewport 카메라를 라이트 시점으로 바꾸는 기능이다.

따라서 정상 동작 시 보여야 하는 것은:

- 일반 액터
- 그리드
- gizmo를 제외한 일반 scene contents

이다.

### 15-3. spotlight가 가장 완성도가 높다

현재 타입별 완성도는 대략 이렇다.

- `SpotLight`: 실제 라이트 방향 + cone 기반 FOV로 가장 자연스러움
- `PointLight`: 6-face preview 미구현, 임시 방향 사용
- `DirectionalLight`: 방향 기준 orthographic preview, pan/zoom 가능

---

## 16. 코드 추적 시 먼저 보면 좋은 파일

1. `KraftonEngine/Source/Editor/UI/EditorPropertyWidget.cpp`
   - 체크박스 UI 진입점
2. `KraftonEngine/Source/Engine/Render/Types/ViewTypes.h`
   - `bOverrideCameraWithSelectedLight` 소유 위치
3. `KraftonEngine/Source/Editor/Viewport/EditorViewportClient.h`
   - preview 상태와 snapshot 구조
4. `KraftonEngine/Source/Editor/Viewport/EditorViewportClient.cpp`
   - preview 상태 전이, 타입별 카메라 동기화
5. `KraftonEngine/Source/Engine/Component/Light/LightComponentBase.cpp`
   - light 변경 알림 발생
6. `KraftonEngine/Source/Editor/EditorEngine.cpp`
   - light 변경 알림 fan-out
7. `KraftonEngine/Source/Engine/Render/Helper/LineGeometry.cpp`
   - directional preview용 camera-plane grid 생성
8. `KraftonEngine/Source/Engine/Render/Pipeline/DrawCommandBuilder.cpp`
   - 현재 프레임의 grid helper 호출 경로

---

## 17. 최종 정리

현재 코드 기준의 `camera override`는 다음으로 이해하는 것이 정확하다.

- light component에 붙은 영구 property가 아니다
- 활성 viewport의 preview 옵션이다
- 선택된 light를 기준으로 viewport camera를 preview 모드로 세팅한다
- 원래 camera 상태는 snapshot으로 저장했다가 해제 시 복원한다
- light가 바뀌면 event-driven으로 다시 sync한다
- directional light는 `FreeOrthographic` preview + pan/zoom 허용 구조다
- directional preview의 grid는 world 축 평면이 아니라 camera plane 기준으로 생성되도록 수정되어 있다

즉 이름은 `light component camera override`지만, 실제 구조는

`selected light -> active viewport preview camera sync`

라고 보는 편이 가장 정확하다.
