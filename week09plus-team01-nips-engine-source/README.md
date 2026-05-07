# NipsEngine

## Current Dependency Setup

After pulling the latest `main`, run `SetupDependencies.bat` from the repository root.
The batch file installs project-local vcpkg under `./vcpkg`, uses `vcpkg.json` in manifest mode, and installs engine dependencies under `./vcpkg_installed`.

Current vcpkg dependencies:

- `luajit`: LuaJIT runtime for Lua scripting. This is Lua 5.1 compatible, so avoid Lua 5.4-only syntax/API.
- `miniaudio`: audio playback and spatial audio backend.
- `joltphysics`: rigid body physics backend used by `RigidBodyComponent` and `PhysicsHandleComponent`.

Required build environment:

- Visual Studio 2022
- MSVC v143 x64/x86 build tools
- MSVC toolset 14.44 or newer
- Windows 10/11 SDK

Do not install dependencies one by one with commands like `vcpkg install lua`.
Use the project manifest instead:

```bat
SetupDependencies.bat
```

The Visual Studio project expects:

- `vcpkg_installed/x64-windows/include/Jolt/Jolt.h`
- `vcpkg_installed/x64-windows/debug/lib/Jolt.lib`
- `vcpkg_installed/x64-windows/lib/Jolt.lib`

Build and test with `x64` configurations. `Debug | x64`, `Release | x64`, and `Game | x64` are the supported paths for the current dependency setup.

## Play Input

- `Mouse Left Click`: pick up or drop a physics object during PIE.
- `W / A / S / D`: move the PIE camera.
- `Mouse Move`: look around while the PIE cursor is captured.
- `F4`: toggle PIE cursor capture.

## Player Start

Place a `Player Start` actor in the scene to control the initial player camera position and direction.
When PIE or Game mode starts, the engine uses the first active `Player Start` actor it finds.
If no `Player Start` exists, PIE falls back to the current editor camera.

## Physics Pickup Setup

To make an object pickable during PIE, the actor should have:

- A blocking shape component, such as `UBoxComponent`, `USphereComponent`, or `UCapsuleComponent`.
- `URigidBodyComponent` with `Can Be Picked Up` enabled.
- `Simulate Physics` enabled when the object should fall, collide, and be released back into physics.

The engine pickup handle preserves the object's current rotation when grabbed.
If a game object should snap to a specific viewing pose, handle that in game logic or Lua instead of relying on the engine default.

Use these physics settings as the baseline:

- Fixed room geometry, walls, floors, and furniture that should never move: blocking shape enabled, `Simulate Physics` disabled.
- Heavy props that can move but should resist being pushed: blocking shape enabled, `Simulate Physics` enabled, high `Mass`, `Can Be Picked Up` disabled.
- Small props the player can hold: blocking shape enabled, `Simulate Physics` enabled, lower `Mass`, `Can Be Picked Up` enabled.

`Mass` is applied to Jolt dynamic bodies when `Simulate Physics` is enabled.
Turning `Use Gravity` off only disables gravity; it does not make the object fixed.

## 실행 준비

이 프로젝트는 Windows, Visual Studio 2022, DirectX 11, ImGui 기반의 C++ 3D 엔진입니다. 처음 실행하는 팀원은 프로젝트 루트에서 아래 순서대로 준비하면 됩니다.

### 1. Visual Studio 프로젝트 파일 생성

`Scripts/GenerateProjectFiles.py`를 실행해 Visual Studio 프로젝트 파일을 현재 소스 구성에 맞게 갱신합니다. 새 `.cpp`/`.h` 파일이 추가된 뒤에도 이 배치 파일을 다시 실행하면 됩니다.

### 2. vcpkg 의존성 설치

프로젝트 루트에서 아래 배치 파일을 실행합니다.

```bat
SetupDependencies.bat
```

고정 경로를 사용합니다.

- `./vcpkg`: 없으면 자동으로 clone/bootstrap합니다.
- `./vcpkg_installed`: vcpkg manifest mode가 생성하는 프로젝트 로컬 의존성 설치 폴더입니다.
- `./vcpkg.json`: 의존성 목록의 단일 기준입니다.

전역 vcpkg를 사용하지 말고, `vcpkg install lua`처럼 개별 패키지를 따로 설치하지 않습니다.
LuaJIT, miniaudio, Jolt Physics 등 엔진 의존성은 `vcpkg.json` 기준으로 함께 설치됩니다.
LuaJIT는 Lua 5.1 계열 호환 런타임이므로 Lua 5.4 전용 문법/API 사용은 피해야 합니다.

`SetupDependencies.bat`는 MSVC toolset 14.44 이상이 설치되어 있는지 먼저 확인합니다.
Jolt 링크 오류로 `_Thrd_sleep_for`, `__std_search_1` 같은 외부 기호 문제가 발생하면 MSVC v143 x64/x86 build tools를 업데이트하거나 설치한 뒤 `vcpkg_installed` 폴더를 삭제하고 `SetupDependencies.bat`를 다시 실행합니다.

### 3. 빌드 및 실행

`NipsEngine.sln`을 Visual Studio 2022로 열고 아래 구성 중 하나로 빌드합니다.

- `Debug | x64`: 에디터 개발용 기본 구성
- `Release | x64`: 릴리스 구성
- `Game | x64`: 게임 실행용 구성
- `ObjViewer | x64`: OBJ 모델 확인 전용 실행 모드

LuaJIT 사용 구성에서는 빌드 후 `lua51.dll`이 출력 폴더로 자동 복사됩니다.

## 엔진 개요

NipsEngine은 학습용으로 직접 구현한 소형 3D 에디터 엔진입니다. Win32 런타임 위에서 DirectX 11 렌더러를 구동하고, ImGui 기반 에디터 UI를 통해 씬 편집, 뷰포트 조작, 오브젝트 선택, 머티리얼 확인, 씬 저장/로드를 제공합니다.

엔진은 크게 런타임, 코어 오브젝트 시스템, 렌더링, 에디터, 에셋 파이프라인, 직렬화, 공간 인덱스/충돌, Lua 스크립팅, ObjViewer 모드로 구성됩니다.

## 주요 기능

- `UObject` 기반 객체 시스템, RTTI, 팩토리, 전역 객체 순회
- `World`, `Level`, `Actor`, `Component` 기반 씬 구조
- `SceneComponent`의 계층 Transform과 `PrimitiveComponent`의 Bounds/Spatial Index 연동
- DirectX 11 기반 렌더링 파이프라인
- Lit, Unlit, Wireframe, Cascade Shadow 등 뷰 모드
- Directional, Point, Spot, Ambient Light와 Shadow 처리
- StaticMesh, Decal, Billboard, Text, SubUV, Fog, Sky, Grid, Gizmo 렌더링
- BVH 기반 프러스텀 컬링, 구 범위 쿼리, 에디터 피킹/선택
- OBJ/MTL 로딩, StaticMesh 변환, Material/Texture 연결
- `Asset/Mesh/Bin` 기반 OBJ 바이너리 캐시
- JSON 기반 `.Scene` 저장/로드
- ImGui 기반 에디터 패널과 4분할/단일 뷰포트
- PIE 흐름과 에디터 월드/실행 월드 분리
- LuaJIT 및 sol2 기반 스크립팅 준비
- OBJ 파일을 빠르게 확인하는 `ObjViewer` 전용 구성

## 프로젝트 구조

```text
NipsEngine/
  main.cpp
  Source/
    Engine/
      Asset/          OBJ, StaticMesh, Binary Cache, Font/Particle Atlas
      Collision/      Ray Collision
      Component/      ActorComponent, SceneComponent, Render/Light/Movement Components
      Core/           Core Types, Console Helpers
      GameFramework/  World, Level, Actor, Primitive Actors
      Geometry/       AABB, OBB, Transform Geometry
      Input/          Input 처리
      Math/           Vector, Matrix, Quaternion 등 수학 유틸리티
      Object/         UObject, FName, Object Factory, Iterators
      Render/         Device, Renderer, Pipeline, Pass, Resource, Collector
      Runtime/        Launch, EngineLoop, Window, Timer, Viewport
      Scripting/      Lua Bindings, Lua Script System
      Serialization/  Archive, JSON Reader/Writer, Scene Save Manager
      Spatial/        WorldSpatialIndex, BVH, KDTree
    Editor/
      Selection/      선택 상태와 Gizmo 동기화
      Settings/       에디터 설정, Show Flags, Viewport 설정
      UI/             Scene, Property, Material, Console, Stat 패널
      Utility/        Component Factory, UI Utils
      Viewport/       EditorViewportClient, Camera, Layout
    Misc/
      ObjViewer/      OBJ 확인 전용 엔진/렌더 파이프라인/UI
  Asset/
    Mesh/
    Material/
    Texture/
    Font/
    Particle/
    Scene/
  Shaders/
  Settings/
```

## 런타임 흐름

프로그램은 `main.cpp`의 `wWinMain()`에서 시작해 `Launch()`로 진입합니다. `FEngineLoop`는 Win32 창, 메시지 루프, 타이머, 엔진 인스턴스를 초기화하고 매 프레임 `GEngine->Tick(DeltaTime)`을 호출합니다.

엔진 생성은 빌드 매크로에 따라 달라집니다.

- `IS_OBJ_VIEWER=1`: `UObjViewerEngine`
- `WITH_EDITOR=1`: `UEditorEngine`
- 그 외: 기본 `UEngine`

`UEngine`은 Window, Timer, Renderer, Render Pipeline, World Context를 소유하고 월드 Tick과 Render를 연결합니다.

## 에디터

에디터는 `UEditorEngine`을 중심으로 동작합니다. 에디터 월드와 PIE 월드를 관리하고, 선택 상태, 뷰포트 레이아웃, 렌더 파이프라인, ImGui 메인 패널을 초기화합니다.

주요 UI는 다음과 같습니다.

- Scene 패널: Actor 계층과 씬 작업
- Property 패널: 선택된 Actor/Component의 편집 가능한 속성 수정
- Material 패널: StaticMesh 섹션별 머티리얼 확인 및 교체
- Console 패널: 명령 입력
- Stat/Overlay 패널: 렌더링, 컬링, 그림자 통계 확인
- Viewport 설정: Show Flags, Grid, View Mode, Camera 옵션

선택과 Gizmo는 `SelectionManager`와 `UGizmoComponent`를 중심으로 동기화됩니다. 뷰포트 피킹과 박스 선택은 카메라 Ray, Collision, Spatial Index, Selection 상태가 함께 관여합니다.

## 렌더링

렌더링은 `FDefaultRenderPipeline`, `FRenderCollector`, `FRenderBus`, `Renderer`가 역할을 나누어 처리합니다.

1. 월드에서 렌더 가능한 Actor/Component를 수집합니다.
2. Spatial Index와 Frustum Query로 보이는 Primitive를 선별합니다.
3. Light, Shadow, Decal, Fog, Sky, Editor Overlay 등 패스별 Render Command를 `RenderBus`에 기록합니다.
4. Render Pipeline이 패스 순서에 따라 GPU 리소스와 셰이더를 바인딩하고 출력 SRV를 생성합니다.

주요 패스에는 Shadow, Opaque, Decal, Fog, FXAA, Font, SubUV, Billboard, SelectionMask, Grid, Editor, Outline, ToonOutline 등이 포함됩니다.

## 에셋과 씬 저장

OBJ/MTL 로더는 `v`, `vt`, `vn`, `mtllib`, `usemtl`, `f` 등 기본 OBJ 정보를 읽고 StaticMesh용 Vertex/Index/Section/Material Slot 데이터로 변환합니다. 변환된 결과는 `Asset/Mesh/Bin` 아래 `.bin` 캐시로 저장되어 다음 실행부터 빠르게 로드됩니다.

씬은 `NipsEngine/Asset/Scene/*.Scene` JSON 파일로 저장됩니다. 저장 대상에는 월드 데이터, Actor/Component 계층, 편집 가능한 속성, StaticMesh 경로, 머티리얼 오버라이드, 일부 카메라/에디터 상태가 포함됩니다.

## Lua 스크립팅

Lua 스크립팅은 `NipsEngine/Source/Engine/Scripting`과 `LuaScriptComponent`를 통해 연결됩니다. 프로젝트는 `WITH_LUA=1`, `SOL_LUAJIT=1`, `SOL_LUA_VERSION=501`, `SOL_ALL_SAFETIES_ON=1` 구성에서 LuaJIT와 sol2를 사용합니다.

## ObjViewer

`ObjViewer | x64` 구성은 전체 에디터가 아니라 OBJ 파일을 빠르게 열어 확인하기 위한 전용 모드입니다. 파일 다이얼로그로 `.obj`를 선택하고, StaticMeshComponent 미리보기, 카메라 리셋, 기본 메쉬 통계와 렌더 옵션을 확인할 수 있습니다.
