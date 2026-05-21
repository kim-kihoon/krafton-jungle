# Skeletal Asset Restructure Digest

## Before / After

| Before | After |
|---|---|
| FBX 원본을 직접 로드하는 흐름이 중심이었다. | FBX를 명시적으로 import해 생성된 `.bin` asset을 사용하는 흐름이 중심이 된다. |
| FBX 하나에서 skeletal mesh, static mesh, animation 중 하나만 가져오고 나머지는 사실상 버렸다. | FBX import 시 skeleton, merged skeletal mesh, animation을 함께 추출한다. |
| FBX 안에 skeletal mesh가 여러 개 있으면 제대로 커버하기 어려웠다. | 단일 FBX 안의 여러 skeletal mesh는 같은 캐릭터의 파츠로 보고 material slot 기준으로 병합한다. |
| skeleton이 skeletal mesh 내부 데이터에 묶여 있었다. | skeleton은 독립 asset이 된다. |
| socket은 skeletal mesh 쪽 데이터로 저장됐다. | socket은 skeleton 공용 asset에 저장한다. |
| animation이 skeleton을 공유하는 여러 skeletal mesh에 재사용되기 어려웠다. | skeletal mesh와 animation이 target skeleton을 asset-relative path로 저장한다. |
| `LoadSkeletalMesh("file.fbx")`가 FBX를 lazy load/import하는 흐름에 가까웠다. | `LoadSkeletalMesh("file.fbx")`는 이미 import된 결과 중 deterministic 기준의 임의 1개 skeletal mesh를 반환한다. 자동 import는 하지 않는다. |
| 특정 skeleton 기준 skeletal mesh 로드 overload가 없었다. | `LoadSkeletalMesh("file.fbx", "skeletonName")`을 제공한다. `skeletonName`은 skeleton root FbxNode 이름이다. |
| skeletal mesh 선택 목록에 FBX 원본이 직접 노출될 수 있었다. | import로 생성된 `.bin` asset만 `SkeletalMeshComponent` 등에서 선택 가능하다. |
| import되지 않은 FBX 내부 항목도 로드 경로를 통해 접근될 수 있었다. | import되지 않은 FBX 내부 항목은 선택/사용할 수 없다. |
| Contents Browser에서 FBX 더블클릭 시 viewer를 열었다. | 미임포트 FBX 더블클릭 시 import 후 결과 skeletal mesh를 viewer로 연다. |
| 이미 import된 FBX라는 개념이 명확하지 않았다. | 이미 import된 FBX 더블클릭 시 재임포트하지 않고 기존 imported skeletal mesh를 viewer로 연다. |
| Contents Browser에서 FBX viewport drag/drop 시 skeletal mesh actor를 배치했다. | 기존 UX를 유지하되, FBX drag/drop 시 import 후 generated skeletal mesh actor를 배치한다. |
| source FBX 변경 감지/재임포트 정책이 명확하지 않았다. | 자동 재임포트는 하지 않는다. 우클릭 컨텍스트 메뉴의 명시적 Reimport만 제공한다. |
| cache 파일은 기존 전용 cache 폴더에 생성됐다. | import 결과 `.bin`은 원본 FBX와 같은 폴더에 생성한다. |
| 생성 cache 파일 이동 정책이 명확하지 않았다. | generated `.bin` 이동/rename은 지원하지 않는다. 이동 시 정상 동작을 보장하지 않는다. |
| import 결과 추적용 manifest를 쓰지 않았다. | 여전히 manifest는 쓰지 않는다. 대신 `.bin` 자체에 필요한 type/version/target skeleton 등 최소 metadata를 serialize한다. |
| 고아 cache 파일 정리 정책이 명확하지 않았다. | 재임포트 시 같은 이름의 `.bin`은 덮어쓰되, 이름 변경 등으로 생긴 고아 `.bin`은 추적/삭제하지 않는다. |
| 첫 번째 mesh/skeleton/animation의 의미가 암묵적이었다. | “첫 번째”는 “deterministic 기준으로 고른 임의의 1개”와 동의어로 취급한다. |
| bone index 제한은 현재 구현에 종속돼 있었다. | bone index 제한은 현행 유지한다. |

## Generated File Names

| Asset | Naming |
|---|---|
| Skeleton | `{FBX파일명}_skeleton_{스켈레톤루트FbxNode이름}.bin` |
| Animation | `Animation/{FBX파일명}_anim_{애니메이션클립명}.anim` 또는 기존 sibling `.anim` |
| Skeletal Mesh | skeleton root 또는 mesh 이름을 포함한 deterministic 이름 |

## Accepted Constraints

| Constraint | Policy |
|---|---|
| 기존 cache 호환성 | 고려하지 않음. cache 삭제 후 재임포트 전제 |
| generated `.bin` 이동/rename | Skeleton/SkeletalMesh에 한정. Animation은 authored `.anim` 사용 |
| 자동 재임포트 | 하지 않음 |
| 고아 `.bin` | 추적/삭제하지 않음 |
| bone index 제한 | 현행 유지 |
| animation stack이 여러 skeleton에 적용 가능한 케이스 | 고려하지 않음 |
| skeleton 재임포트 시 socket 보존 | 보장하지 않음 |
| import manifest | 사용하지 않음 |
