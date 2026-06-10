# Anomaly System

## 목적

- 게임 시작 또는 플레이어 루프 시점마다 `AnomalyCandidate` 태그가 붙은 액터 중 하나를 선택한다.
- 선택된 액터에는 랜덤 Anomaly 규칙 하나만 적용한다.
- 플레이어가 현재 활성 Anomaly 대상을 총으로 맞추면 클리어 상태를 기록하고 `GameManager:StopLoop()`를 호출한다.
- 총으로 맞춘 직후에는 Anomaly 효과를 바로 원복하지 않는다. 다음 Anomaly 로드, 리셋, 게임 종료 시 기존 효과를 정리한다.

## 구조

```txt
GameManager
  - 게임 시작, 루프 시작, 정답 처리, 게임 상태 관리
  - StopLoop / RestLoop / OnLoopStart / OnWarp 제공

AnomalyManager
  - 후보 수집
  - 랜덤 규칙 선택
  - 활성 Anomaly 상태 저장
  - Tick / Despawn / ReportShot 처리

Anomalies/*.lua
  - 각 Anomaly 규칙의 Spawn / Tick / Despawn / IsCleared 구현

DebugManager
  - 숫자키로 특정 규칙 강제 적용
  - Q 또는 패드 L2를 누르고 있는 동안 활성 대상 outline 표시
```

## 태그

```txt
AnomalyCandidate
ActiveAnomalyTarget
PhotoInvisible
PhotoBlackoutTarget
PhotoGhostReplacementTarget
PhotoGhostReplacementActor
PhotoBoneTwistTarget
CymbalsMonkey
CymbalsMonkeyInitPosition
CymbalsMonkeyPositionCandidate
```

- `AnomalyCandidate`: 랜덤 Anomaly 후보 액터에 붙인다.
- `ActiveAnomalyTarget`: 현재 활성 Anomaly 대상에 런타임으로 붙는다.
- `PhotoInvisible`: 촬영 결과에서 대상이 빠지는 규칙에 사용한다.
- `PhotoBlackoutTarget`: 촬영 조건을 만족하면 사진 내부 이미지를 검게 만드는 규칙에 사용한다.
- `PhotoGhostReplacementTarget`: 사진에서 원본 대신 Ghost actor가 보이게 할 원본 대상에 붙인다.
- `PhotoGhostReplacementActor`: 촬영 순간에만 표시할 별도 Ghost actor에 붙인다.
- `PhotoBoneTwistTarget`: 촬영 순간에만 skeletal mesh bone 회전을 무작위로 비트는 규칙에 사용한다.

## 랜덤 풀과 디버그 풀

`AnomalyManager.Rules`는 실제 루프 랜덤 선택에 사용하고, `AnomalyManager.AllRules`는 디버그 강제 적용 이름 검색에 사용한다.

- 랜덤 풀 포함: `PhotoInvisible`, `PhotoLookAtInvisible`, `PhotoLookAtBlackPhoto`, `BlackPhoto`, `PhotoGhostReplacement`, `PhotoBoneTwist`
- 랜덤 풀 제외: `NoShadow`, `OffscreenAnimation`, `OffscreenFacePlayer`, `NearSilentCymbalMonkey`
- 디버그 강제 적용: `AllRules`에 등록된 규칙은 `GameManager:DebugSpawnAnomalyRule(ruleName)`로 직접 호출할 수 있다.

기본 디버그 키:

```txt
1 -> PhotoInvisible
2 -> PhotoLookAtInvisible
3 -> PhotoLookAtBlackPhoto
4 -> BlackPhoto
5 -> PhotoGhostReplacement
6 -> PhotoBoneTwist
```

## 게임 흐름

### 게임 시작

`GameManager:StartGame()`은 게임 상태를 `Playing`으로 바꾸고 `RestLoop()`를 호출한 뒤 초기 Anomaly를 로드한다. 시작 시 플레이어 탄약도 3발로 초기화한다.

### 플레이어 워프

`hospital_player.lua`에서 플레이어 워프가 실제 수행된 직후 `GameManager:OnWarp("PlayerWarp")`를 호출한다.

`OnWarp()`는 내부에서 `AdvanceAnomalyLoop()` 흐름을 실행한다. 기존 활성 Anomaly가 있으면 먼저 `DespawnCurrent("SelectAndSpawn")`로 원복하고, 새 대상과 새 규칙을 선택한다.

### 루프 시작

`DoorEntry` 태그 문이 열리면 `GameManager:OnLoopStart("DoorEntryOpened")`를 호출한다.

이 함수는 `remainingTime`을 초기값으로 되돌리고, `StopLoop()`로 멈춘 시간과 원숭이 애니메이션을 다시 진행 가능한 상태로 만든다. Anomaly 재선택은 `StartGame()`과 `OnWarp()`가 담당한다.

### 정답 총격

총격 라인트레이스가 현재 활성 대상 또는 `ActiveAnomalyTarget` 태그 액터를 맞추면 `GameManager:ReportAnomalyShot(hit.Actor, hit)`가 `true`를 반환한다.

정답이면 `AnomalyManager:OnClear()`가 클리어 상태를 기록하고 `GameManager:StopLoop()`가 호출된다. 이때 게임 시간과 `CymbalMonkey` 애니메이션은 멈추지만 Anomaly 효과는 즉시 원복하지 않는다.

### 실패와 게임 오버

총 3번 실패하면 남은 시간이 3초 동안 빠르게 줄어든 뒤 게임 오버 흐름으로 진입한다. 감소 곡선은 처음에 빠르고 점점 느려지는 비선형 형태다.

게임 오버 조건이 만족되면 `GameOverMonkey` 모듈이 원숭이 연출 애니메이션만 재생하고, 호출한 쪽에서 코루틴 대기 후 GameOver 메뉴를 연다.

## 사진 캡처

`ULuaAnimInstance::request_photo_capture`는 촬영 요청 시 `PhotoInvisible` 태그를 전달한다.

렌더 파이프라인은 실제 캡처 직전 `FPhotoOverlay::PreparePendingCaptureWorldState(World)`를 호출한다. 이 단계에서 사진에만 적용되는 상태 변경을 수행하고, 캡처 직후 즉시 원복한다.

현재 사진 캡처 전용 처리는 다음과 같다.

- `PhotoInvisible`: `PhotoInvisible` 태그 액터를 캡처 중에만 숨긴다.
- `BlackPhoto`: 조건을 만족한 한 장의 사진 내부 이미지만 검게 만든다.
- `PhotoGhostReplacement`: 원본 대상 actor는 숨기고, 미리 생성해 둔 별도 Ghost actor를 캡처 중에만 표시한다.
- `PhotoBoneTwist`: `PhotoBoneTwistTarget` 태그 액터의 skeletal mesh bone local rotation을 캡처 중에만 무작위로 비틀고 직후 복구한다.
- 플레이어가 들고 있는 카메라 mesh는 캡처 중에만 숨긴다.

## 규칙별 구현 원리

### PhotoInvisible

- `Spawn`: 대상에 `PhotoInvisible` 태그를 붙인다.
- `Despawn`: 원래 없던 `PhotoInvisible` 태그만 제거한다.
- 캡처 처리: `FPhotoOverlay`가 태그 액터를 캡처 중에만 숨긴다.

### BlackPhoto

- `Spawn`: 대상에 `PhotoBlackoutTarget` 태그를 붙인다.
- 촬영 입력 시점에 활성 대상이 카메라 프러스텀 안에 있고 라인트레이스가 막히지 않으면 `request_photo_capture(true)`로 블랙아웃을 요청한다.
- 블랙아웃은 폴라로이드 프레임이 아니라 사진 내부 캡처 이미지 전체에만 적용된다.
- 블랙아웃 여부는 매 촬영마다 다시 판정한다.

### PhotoLookAtInvisible

- 촬영 요청 직전에 대상 yaw를 플레이어 방향으로 돌린다.
- 동시에 `PhotoInvisible` 태그를 사용해서 사진 결과에서 대상이 빠지게 한다.
- `Tick`에서 계속 회전하지 않고 촬영 시점에만 회전한다.

### PhotoLookAtBlackPhoto

- 촬영 요청 직전에 대상 yaw를 플레이어 방향으로 돌린다.
- 동시에 `PhotoBlackoutTarget` 태그를 사용한다.
- 대상이 프러스텀과 라인트레이스 조건을 만족하면 해당 사진만 검게 나온다.

### PhotoGhostReplacement

- `Spawn`: `World.SpawnStaticMeshActor("Content/Data/Ghost/Ghost.uasset", ...)`로 별도 Ghost actor를 대상 위치, 회전, 스케일에 생성한다.
- 평상시: Ghost actor에는 `PhotoGhostReplacementActor` 태그를 붙이고 `SetVisible(false)`로 숨겨 둔다.
- 원본 대상: `PhotoGhostReplacementTarget` 태그만 붙인다. 원본 대상에는 `StaticMeshComponent`를 추가하지 않는다.
- 캡처 직전: `FPhotoOverlay`가 `PhotoGhostReplacementTarget` actor를 숨기고, `PhotoGhostReplacementActor` actor를 표시한다.
- 캡처 직후: 두 actor의 visibility를 촬영 전 상태로 복구한다.
- `Despawn`: 원래 없던 target 태그만 제거하고, 생성해 둔 Ghost actor를 `Destroy()`로 제거한다.
- `Content/Data/Ghost/Ghost.uasset`가 없거나 static mesh 로드에 실패하면 룰 `Spawn`은 실패한다.

### PhotoBoneTwist

- `Spawn`: 대상에 skeletal mesh가 있는지 확인하고 `PhotoBoneTwistTarget` 태그를 붙인다.
- 캡처 직전: 현재 local bone pose를 저장하고 각 bone의 local rotation에 무작위 delta rotation을 곱한다.
- root bone은 전체 위치감이 크게 흔들리지 않도록 작은 각도로 제한한다.
- 캡처 직후: 저장한 local pose를 즉시 복구한다.
- `Despawn`: 원래 없던 `PhotoBoneTwistTarget` 태그만 제거한다. bone pose는 촬영 직후 이미 복구되어 있어야 한다.

### OffscreenAnimation

- 대상 skeletal mesh의 skeleton과 호환되는 animation asset 목록을 조회한다.
- 현재 재생 중인 animation은 후보에서 제외한다.
- 후보가 없으면 하드코딩 fallback 없이 `Spawn` 실패로 처리한다.
- 대상이 프러스텀 밖이면 선택된 animation을 재생하고, 프러스텀 안이면 정지한다.
- 원래 animation이 없던 대상은 `StopAnimation()`으로 reference pose에 돌아가게 한다.

### OffscreenFacePlayer

- 플레이어가 대상을 한 번 관측하기 전까지는 동작하지 않는다.
- 최초 관측은 프러스텀 판정과 라인트레이스 판정을 함께 사용한다.
- 최초 관측 이후 대상이 프러스텀 밖에 있을 때만 actor yaw를 플레이어 방향으로 갱신한다.
- 이 단독 규칙은 랜덤 풀에는 포함하지 않는다.

### NoShadow

- 대상 primitive component의 `CastShadow` 값을 저장한 뒤 false로 바꾼다.
- `Despawn`에서 저장한 원래 값으로 복구한다.
- 이 규칙은 랜덤 풀에는 포함하지 않는다.

### NearSilentCymbalMonkey

- `World.FindActorsByTag("CymbalsMonkey")`로 원숭이 액터를 찾는다.
- 플레이어가 활성 Anomaly 대상 2.5m 이내에 들어가면 원숭이 `AudioComponent` 볼륨을 0으로 바꾼다.
- 범위 밖으로 나가거나 `Despawn`되면 원래 볼륨으로 복구한다.
- 이 규칙은 랜덤 풀에는 포함하지 않는다.

## Lua 바인딩

Anomaly 시스템에서 사용하는 주요 바인딩은 다음과 같다.

```txt
PrimitiveComponent:SetCastShadow(bool)
PrimitiveComponent:GetCastShadow()
PrimitiveComponent:SetVisibility(bool)
PrimitiveComponent:IsVisible()
Actor:GetAudioComponent()
Actor:SetGameplayOutline(bool)
Actor:Destroy()
AudioComponent:SetVolume(float)
AudioComponent:GetVolume()

SkeletalMeshComponent:GetAnimationPath()
SkeletalMeshComponent:GetCompatibleAnimationPaths()
SkeletalMeshComponent:GetPlayRate()
SkeletalMeshComponent:GetLooping()
SkeletalMeshComponent:IsPlaying()

World.SpawnStaticMeshActor(meshPath, location, rotation, scale)
World.IsActorInViewFrustum(actor)
World.IsComponentInViewFrustum(component)
World.GetGameTime()
World.GetRealTimeSeconds()
```

랜덤 seed는 Lua `os.time()`이 아니라 엔진 바인딩 `World.GetRealTimeSeconds()`를 사용한다.

## 테스트 체크리스트

- `1`을 누르면 `PhotoInvisible`이 적용되고 사진에서 대상만 빠지는지 확인한다.
- `2`를 누르면 촬영 시점에 대상이 플레이어를 바라본 뒤 사진에서 빠지는지 확인한다.
- `3`을 누르면 촬영 시점에 대상이 플레이어를 바라본 뒤 조건 만족 시 사진이 검게 나오는지 확인한다.
- `4`를 누르면 `BlackPhoto` 조건을 만족한 사진만 검게 나오는지 확인한다.
- `5`를 누르면 월드에서는 원본만 보이고, 촬영 결과에는 원본 대신 별도 Ghost actor가 보이는지 확인한다.
- `6`을 누르면 촬영 결과에서만 skeletal mesh bone rotation이 무작위로 비틀리고 월드 포즈는 즉시 복구되는지 확인한다.
- 정답 대상을 맞추면 시간과 `CymbalMonkey` 애니메이션이 멈추고, 다음 루프에서 기존 Anomaly가 원복되는지 확인한다.
- `Q` 또는 패드 `L2`를 누르고 있는 동안에만 활성 Anomaly 대상 outline이 보이는지 확인한다.
