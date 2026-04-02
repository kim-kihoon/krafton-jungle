# CO-PASS Engine

![CO-PASS Logo](Editor/Resources/Tool/copass.png)

CO-PASS Engine은 **Win32 + Direct3D 11 + ImGui** 기반으로 개발된 **자체 3D 게임 엔진 및 레벨 에디터**입니다.  
이 프로젝트는 언리얼 엔진(Unreal Engine)의 핵심 아키텍처(`UObject`, `Component`, `RTTI`, `Asset Pipeline`)를 심도 있게 분석하고 모방하여, 확장 가능하고 견고한 **에디터 중심 워크플로우**와 **렌더링 파이프라인**을 구축하는 것을 목표로 합니다.

---

## 1. 프로젝트 개요 및 철학

단순한 그래픽스 렌더러 구현을 넘어, **"실제 상용 게임 엔진은 어떻게 설계되는가?"**에 대한 해답을 찾기 위해 다음과 같은 코어 시스템을 직접 설계하고 구현했습니다.

- **Unreal-Style 객체 시스템**: 엔진 내 모든 객체의 최상위 부모인 `UObject`를 정의하고, 안전한 타입 캐스팅을 위한 자체 RTTI(`Cast<T>`) 시스템을 구현했습니다.
- **전역 객체 순회 (TObjectIterator)**: 메모리에 로드된 특정 타입의 객체(예: `UStaticMesh`)를 동적으로 탐색하여, 에디터 UI의 드롭다운 리스트 등에 실시간으로 연동시키는 구조를 갖추었습니다.
- **다중 빌드 구성 (Multi-Configuration)**: 전처리기 매크로(`IS_OBJ_VIEWER`)를 통해 무거운 에디터 기능이 포함된 빌드와 순수 모델 뷰어용 경량화 빌드를 컴파일 타임에 물리적으로 분리했습니다.

---

## 2. 주요 특징 및 구현 사항

### 🖥️ 에디터 및 UI/UX
- **ImGui 도킹 및 커스텀 레이아웃**: 첫 실행 시 언리얼 스타일(우측 상단 Outliner, 우측 하단 Details)로 자동 도킹 레이아웃을 구성하며, `Editor.ini`를 통해 사용자 레이아웃을 저장합니다.
- **다중 뷰포트 (Multi-Viewport)**: 화면을 4분할하는 가로/세로 스플릿터(`SSplitter`) 시스템을 구축하고, 직교(Orthogonal) 뷰와 원근(Perspective) 뷰를 동시에 렌더링합니다.
- **계층적 아웃라이너 (Outliner)**: 씬 내의 Actor와 Component 계층 구조를 트리 형태로 표시하며, 동적 생성/삭제 및 단축키(`F2`)를 통한 이름 변경을 지원합니다.
- **디테일 패널 (Details)**: 컴포넌트별 수동 프로퍼티 시스템(`DescribeProperties`)을 통해 속성을 직렬화하고 UI에 노출하여 실시간 편집을 지원합니다.
- **에셋 관리 자동화**: `TObjectIterator`로 수집된 에셋을 콤보박스로 선택하거나, Content Browser에서 드래그 앤 드롭으로 할당할 수 있습니다.

### 🎨 렌더링 파이프라인
- **정적 메시 (Static Mesh) & 다중 재질 (Multiple Materials)**: 하나의 메시에 여러 개의 서브 머티리얼 슬롯을 지원하며, 인덱스 버퍼를 섹션 단위로 나누어 렌더링합니다.
- **Lit 모드 조명 모델**:
  - **Half-Lambert**: 실내외 구분 없이 부드러운 볼륨감을 제공하는 셰이딩 기법 적용.
  - **Hemisphere Ambient**: 하늘과 지면의 색상을 보간하여 그림자 음영부의 텍스처 디테일을 확보.
- **동적 UV 스크롤 (Panner)**: 엔진 루프의 `TotalTime`과 각 인스턴스(컴포넌트)의 개별 `UVScrollSpeed` 오버라이드 값을 상수 버퍼(`FMeshLitConstants`)로 전달하여, 정점 쉐이더 단위의 고성능 텍스처 패닝 애니메이션을 구현했습니다.
- **디버그 오버레이 (Stat Overlay)**: `stat fps`, `stat memory` 콘솔 명령어를 통해 뷰포트 상단에 실시간 렌더링 성능 및 메모리 지표를 오버레이로 표시합니다.

### ⚙️ 데이터 및 에셋 파이프라인
- **OBJ 파싱 및 쿠킹 (Baking)**: 
  - `Raw Data (.obj)` 파싱 후 GPU 친화적인 `Cooked Data (.mesh)`로 변환합니다.
  - `FArchive`와 `BinWriter/Reader`를 활용한 **바이너리 직렬화(Baking)** 시스템을 구축하여, 방대한 모델의 재로딩 속도를 비약적으로 단축시켰습니다.
- **씬 직렬화 (Scene Serialization)**: JSON 포맷을 기반으로 Actor/Component의 계층 상태, 에셋 가상 경로(`/Game/...`), 그리고 Perspective 카메라의 정보(FOV, Clip Plane)를 저장 및 복원합니다.

---

## 3. 기술 스택

- **Language**: C++ (C++20)
- **Platform**: Windows 10/11 (Win32 API)
- **Graphics API**: Direct3D 11
- **Editor UI**: Dear ImGui (Docking Branch)
- **Data Serialization**: JSON (nlohmann/json)
- **Version Control**: Git / Visual Studio 2022

---

## 4. 빌드 및 실행

### 빌드 방법
1. `Kraftonjungle_Week4_Team8.sln` 솔루션 파일을 Visual Studio 2022에서 엽니다.
2. 상단 구성(Configuration)을 목적에 맞게 선택합니다:
   - **Debug / Release**: 풀 에디터 환경 (Outliner, Details, Content Browser 등 모든 도구 포함)
   - **ObjViewerDebug / ObjViewerRelease**: 순수 OBJ 뷰어 모드 (에디터 UI가 제거된 독립된 경량 엔진 루프 실행)
3. 솔루션을 빌드(`F5` 또는 `Ctrl+Shift+B`)하고 실행합니다.

---

## 5. 핵심 단축키 (Shortcuts)

에디터 뷰포트 및 패널 내에서 상용 엔진과 유사한 단축키 워크플로우를 제공합니다:

- **카메라 조작**: `우클릭 드래그` (회전), `W/A/S/D/Q/E` (이동), `휠 드래그` (패닝)
- **기즈모 전환**: `W` (이동), `E` (회전), `R` (스케일) *(우클릭 중에는 카메라 이동으로 전환)*
- **뷰 모드 전환**: `1` (Lit), `2` (Unlit), `3` (Wireframe)
- **포커스 및 선택**: `F` (선택된 객체로 카메라 초점 맞춤), `클릭` (선택), `Ctrl+클릭` (다중 선택)
- **객체 제어**: `F2` (이름 변경), `Delete` (삭제)
- **저장/열기**: `Ctrl+S` (저장), `Ctrl+Shift+S` (다른 이름으로 저장), `Ctrl+O` (열기)

---

## 6. 시스템 아키텍처 상세 (Technical Details)

### A. UObject & Reflection의 대안
C++ 자체의 한계를 극복하기 위해 매크로(`REGISTER_CLASS`)와 객체 팩토리를 구축했습니다. 이를 통해 문자열 이름만으로 런타임에 동적으로 클래스를 인스턴스화하여 `.Scene` 파일 복원을 처리하며, 싱글톤 에셋 매니저(`FObjManager`)를 통해 중복 로딩을 방지합니다.

### B. UV Scroll의 인스턴스 격리 아키텍처 (Instance Overrides)
같은 머티리얼 에셋(`UMaterial`)을 공유하는 여러 메시가 서로 다른 속도로 텍스처를 스크롤할 수 있도록 설계했습니다. 원본 에셋을 변형하지 않고, `UMeshComponent` 내부에 `TMap<uint32, FVector2> UVScrollOverrides`를 관리합니다. 렌더러는 매 프레임 상수 버퍼(`FMeshLitConstants`)를 갱신할 때 에셋의 기본값보다 컴포넌트의 오버라이드 값을 우선 적용하여 **Material Instance Dynamic(MID)**과 유사한 기능을 구현했습니다.

### C. 전용 OBJ Viewer 모드 (`IS_OBJ_VIEWER`)
에디터와 뷰어의 역할을 물리적으로 분리하여 배포 최적화를 달성했습니다. 뷰어 빌드로 실행 시 `FEditorEngineLoop`가 아닌 `FObjViewerEngineLoop`라는 완전히 독립된 루프가 작동하며, 드래그 앤 드롭 모델 로딩과 퀵 카메라 얼라인(정면, 측면 등) 기능을 제공하여 모델 검토에 최적화된 UX를 제공합니다.

---

## 7. 콘솔 명령어 (Console)

`Console` 패널에서 다음 명령어를 통해 엔진의 런타임 상태를 제어할 수 있습니다.
- `stat fps` / `stat memory` / `stat none` : 뷰포트 성능 오버레이 제어
- `viewmode lit` / `viewmode unlit` / `viewmode wireframe` : 렌더링 모드 전환
- `actor.spawn cube [개수]` / `actor.delete_selected` : 액터 제어
- `scene.save` / `scene.new` / `scene.open` : 씬 제어

---

## 8. 크레딧 및 기여자 (Contributors)

### First Contributors
- **강명호**: Editor UI, panels, tools, and asset integration
- **김연하**: Renderer, sprite/text rendering, and show flags
- **김형도**: Gizmo interaction, center highlight, and multi-rotation
- **양현석**: Engine loop, viewport input, camera, and snapping

### Second Contributors (Week 4)
- **김기훈**: Editor UI & Tools, Material editing and UV scroll
- **김형도**: OBJ parser, Binary mesh serialization and Material Editing
- **김형준**: Multi-viewport architecture, Camera systems and Viewer core
- **장민준**: Stat overlay (FPS/Memory), Console engine and Timer management

---
**License**: MIT License  
Copyright (c) 2026 CO-PASS Engine Contributors
