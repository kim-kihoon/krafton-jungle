# Lua Gameplay Managers

## Anomaly outline 디버그 입력

현재 활성 Anomaly 타겟을 인게임에서 외곽선으로 표시하는 디버그 입력은 다음 규칙을 사용한다.

- 키보드/마우스: `Q`
- 플스 패드: `L2` (`DebugAnomalyOutline` 액션, `Gamepad_LeftTrigger`)
- 기존 조준 입력: `Aim`은 `RightMouseButton`과 `Gamepad_LeftShoulder(L1)`를 유지한다.

`DebugAnomalyOutline`은 게임플레이 정답 판정이나 에디터 선택 상태를 바꾸지 않고, 입력을 누르고 있는 동안만 현재 활성 Anomaly 타겟의 렌더 outline을 표시한다.

## 목적

Hospital 계열 Lua 스크립트는 씬 액터에 붙은 진입점 스크립트와 전역 상태 모듈이 섞이면 책임 경계가 흐려지기 쉽다. 이 문서는 현재 Manager 분리 구조와 유지보수 규칙을 정리한다.

핵심 원칙은 다음과 같다.

1. 씬에 붙은 스크립트는 입력, 카메라, 월드 이벤트를 Manager에 연결하는 진입점 역할만 한다.
2. 게임 상태, 루프 상태, 문 상태, 사운드, UI, 장비 상태는 각각 전용 Manager가 소유한다.
3. 사용처는 물리 입력 장치나 구체적인 UI 문서 경로를 직접 판단하지 않고 Manager API를 통해 읽는다.
4. `require()`로 로드되는 Manager는 Lua 모듈 캐시에 남으므로 `BeginPlay`, `EndPlay`, 씬 전환 시 `Reset()` 호출 순서를 명확히 유지한다.

## 전체 구조

```txt
hospital_player.lua
  - Hospital.Scene의 플레이어 진입점
  - title mode, 카메라 전환, 워프 트리거, 입력 연결
  - Start 버튼에서 TitleMonkey 타이틀 연출을 호출
  - GameOverMonkey 모듈에 게임오버 연출 생명주기를 위임
  - title mode 해제 시 현재 loop stopped 상태를 기준값으로 동기화
  - DoorManager, SoundManager, UIManager, ToolManager, SettingManager 호출

TitleMonkey.lua
  - TitleMonkey 액터에 붙는 타이틀 전용 연출 스크립트
  - 타이틀 화면 진입점에서 `Ready`로 `CymbalMonkey_Joints_ArmOnlyCymbalEntry` 애니메이션을 0.1 속도로 재생
  - 타이틀 버튼 입력 시 `Strike`로 `CymbalMonkey_Joints_ArmOnlyCymbalStrike` 애니메이션 재생

GameOverMonkey.lua
  - 플레이어가 가진 `GameOverMonkey` 컴포넌트의 게임오버 전용 연출 모듈
  - `GameManager` 상태 변경을 구독해 GameOver 진입 시 표시/애니메이션/지연 UI 전환을 처리
  - 재시작, 리셋, 씬 종료 시 GameOverMonkey 표시와 GameOver UI를 정리

fps_character.lua
  - FPS 팔/무기/카메라 애니메이션 상태
  - 발사, 촬영, 장비 전환, 화면 흔들림, 발소리 처리
  - GameManager, SoundManager, ToolManager, SettingManager 상태를 소비

GameManager.lua
  - 게임 진행 상태의 단일 진실
  - Ready, Playing, Paused, GameOver, Clear
  - 점수, 시간, 압박 단계, 이상현상 배치, 리더보드, 이벤트 발행

LoopManager.lua
  - 루프 정지/재개와 CymbalMonkey cycle 상태
  - GameManager의 loop 관련 상태를 실제로 갱신

DoorManager.lua
  - Hospital 문 목록, 문 회전, 잠금, 자동 닫힘, 상호작용 대상
  - 문 열림 이벤트를 GameManager 루프 이벤트와 연결
  - 문 사운드는 SoundManager에 위임

SoundManager.lua
  - UI/게임플레이에서 쓰는 효과음 재생
  - 문 닫힘 지연 재생 큐
  - 타이틀 상태 사운드와 플레이 중 사운드 진입점 분리
  - 타이틀 시작 중 월드 AudioComponent 음소거와 게임 시작 시 복원

UIManager.lua
  - Hospital HUD, 문 프롬프트, 조작 프롬프트, 타이머, 타이틀 UI
  - InputSystem의 논리 액션 표시명을 사용해 현재 입력 장치에 맞는 문구 표시

ToolManager.lua
  - 플레이어 장비 선택 상태
  - Pistol, Camera 전환 상태를 fps_character.lua와 UIManager.lua가 공유

SettingManager.lua
  - 타이틀 설정 메뉴의 사용자 선택 상태
  - 감마, 마스터 볼륨, 마우스 감도, Y축 반전, 헤드밥, 조작 프롬프트 표시 여부를 소유
  - BeginPlay와 설정 변경 시 엔진, 오디오, 플레이어 입력에 현재 값을 적용
```

## 진입점 스크립트 책임

### hospital_player.lua

`hospital_player.lua`는 Hospital.Scene에 붙는 런타임 연결 스크립트다. 직접 소유하는 상태는 씬 흐름에 필요한 값으로 제한한다.

- 타이틀 모드 여부
- 플레이어 워프 가능 여부
- 마지막 루프 정지 상태
- 타이틀 카메라/플레이어 카메라 전환
- Start 버튼 입력 시 TitleMonkey 연출 호출
- GameOverMonkey 모듈 초기화, Tick, 정리 호출
- Title 태그 액터 비활성화
- `Input.GetAxis`, `Input.GetActionDown` 결과를 플레이어 이동과 문 상호작용에 연결

문 목록, 문 열림 상태, 문 소리, HUD 문구, 장비 상태는 이 파일이 직접 소유하지 않는다.

### fps_character.lua

`fps_character.lua`는 애니메이션 그래프와 1인칭 표현을 담당한다. Hospital 씬의 문, 워프, 타이틀 UI를 직접 다루지 않는다.

- Pistol/Camera 애니메이션 상태
- 장비 전환 연출
- 조준, 발사, 사진 촬영 입력 처리
- projectile spawn, muzzle/socket 기반 발사 위치 계산
- head bob, camera bob, footstep timing
- 이상현상 총격 판정은 `GameManager:ReportAnomalyShot()`으로 위임
- 권총 발사 탄환 소모는 정답 이상현상, `Fake` 태그 대상, 일반 투사체 발사 모두 `GameManager:ConsumePlayerBullet()`을 먼저 통과한다.
- 사진 촬영은 `Anim.is_photo_capture_available()`이 true일 때만 `Anim.request_photo_capture()`를 호출한다.

`ToolManager`는 장비 상태 공유를 위한 단일 진실이다. `fps_character.lua` 내부의 애니메이션 전환 상태와 `ToolManager.CurrentTool`이 어긋나지 않도록 장비 표시가 확정되는 지점에서 동기화한다.

## Manager 책임

### GameManager

게임 진행 상태의 루트 Manager다. 점수, 시간, 압박 단계, 이상현상 배치, 리더보드, 게임 종료/클리어 상태를 관리한다.

`GameManager`가 직접 처리해야 하는 일:

- `StartGame`, `PauseGame`, `ResumeGame`, `GameOver`, `ClearGame`, `RestartGame`
- 점수와 타이머 갱신
- 스테이지별 플레이어 권총 탄환 수 초기화와 소모
- 압박 단계 계산
- 이상현상 배치와 활성 이상현상 선택
- 상태 변경 이벤트 발행

`GameManager`가 직접 처리하지 않는 일:

- 문 회전/문 충돌/문 소리
- 구체적인 UI 문서 생성/삭제
- 장비 애니메이션
- 물리 입력 키 판단

### LoopManager

루프 정지 여부와 CymbalMonkey cycle 시작 여부만 소유한다. `GameManager`의 하위 상태 모듈이므로 외부 사용처는 가능하면 `GameManager:IsLoopStopped()`, `GameManager:OnLoopStart()` 같은 wrapper를 통해 접근한다.

이 구조를 유지하면 루프 상태 변경 이벤트는 계속 `GameManager:_FireEvent()`를 통해 발행된다.

### DoorManager

Hospital 문과 관련된 런타임 상태를 소유한다.

- 문 액터 수집
- 문 열림/닫힘 상태
- 회전 보간
- 플레이어와 문 충돌 보정
- 자동 닫힘 구역
- 출구 문 잠금/해제
- 루프 시작 문 이벤트
- CymbalMonkey cycle 시작 문 이벤트

문 사운드 재생은 `SoundManager`에 위임한다. `DoorManager:Reset()`은 문 상태를 초기화하면서 `SoundManager:Reset()`도 호출해 지연 문 닫힘 소리와 시작 음소거 상태를 정리한다.

### SoundManager

게임플레이 사운드와 월드 오디오 복원을 담당한다.

- 일반 효과음: `Play`, `PlayAt`, `PlayAtActor`
- 타이틀 상태: `EnterTitleState`, `ExitTitleState`, `PlayTitle`, `PlayTitleAt`, `PlayTitleAtActor`
- 플레이 상태: `EnterPlayingState`, `PlayGameplay`, `PlayGameplayAt`, `PlayGameplayAtActor`
- 문 소리: `PlayDoorOpen`, `QueueDoorClose`, `TickGameplaySounds`
- 장비 효과음: `PlayPartyBlower`
- 타이틀 중 월드 오디오: `MuteTitleWorldAudio`, `RestoreTitleWorldAudio`

시작 시 크게 들리는 AudioComponent 문제는 `AudioComponent`의 `bMuteUntilStart`와 `MuteForStartup()`을 함께 사용한다. `AudioComponent::BeginPlay()`에서 자동재생 전에 볼륨을 0으로 낮추고, `hospital_player.lua`의 `StartGame()`에서 `SoundManager:EnterPlayingState()`가 원래 볼륨을 복원한다.

상태별 사운드 규칙:

- 타이틀 화면에서 필요한 사운드는 `PlayTitle*` 함수로 추가한다.
- 실제 게임플레이 중 발생하는 효과음은 `PlayGameplay*` 함수로 추가한다.
- 문 소리와 장비 소리는 플레이 상태 사운드로 취급한다.
- 상태 전환은 `hospital_player.lua`가 `EnterTitleState()`와 `EnterPlayingState()`로 명시한다.
- 기존 `Play*` 함수는 공통 저수준 재생 함수로 남기되, 새 호출처는 상태별 함수를 우선 사용한다.

Lua에서 월드 오디오를 스캔할 때는 `Actor:GetAudioComponent()`를 쓰지 않는다. 이 함수는 없는 컴포넌트를 새로 만들 수 있으므로, 기존 컴포넌트만 대상으로 삼아야 하는 초기 음소거에는 맞지 않다. 대신 `World.FindActorsByClass()`와 `Actor:GetComponents()`를 사용한다.

### UIManager

UI 문서의 생성, 표시, 텍스트 갱신, 제거를 담당한다.

- 문 프롬프트
- 조작 프롬프트
- 타이머
- 타이틀 메인 UI
- 타이틀 설정/크레딧 팝업

조작키 표시는 `Input.GetActionMappingDisplayName()`을 통해 현재 입력 장치 기준으로 가져온다. 예를 들어 문 상호작용 UI는 `Interact` 액션 표시명을 사용하므로 키보드에서는 `E`, 패드에서는 해당 face button 이름으로 표시된다.

### ToolManager

플레이어 장비 상태만 소유한다.

- `Pistol`
- `Camera`
- 현재 장비
- 다음 장비 계산
- UI 표시 모드

장비의 애니메이션, mesh 표시, 발사/촬영 동작은 `fps_character.lua`가 처리한다. UI 문구는 `UIManager`가 `ToolManager`를 읽어 만든다.

### SettingManager

타이틀 설정 메뉴의 사용자 선택 상태를 소유한다.

- 감마 프리셋
- 마스터 볼륨 프리셋
- 마우스 감도 프리셋
- Y축 반전
- 헤드밥 사용 여부
- 조작 프롬프트 표시 여부

설정 값은 `Saves/UserSettings.ini`에 저장하고, `SettingManager:Load()`로 불러온 뒤 `SettingManager:ApplyAll(player)`로 엔진 렌더 옵션, 오디오 볼륨, 플레이어 입력 값에 반영한다. `UIManager`와 `TitleManager`는 설정 팝업 표시와 텍스트 갱신만 담당하고, `hospital_player.lua`는 Hospital 플레이어 적용을 연결한다.

## 시작과 종료 순서

Hospital.Scene 시작 흐름은 다음 순서를 유지한다.

```txt
hospital_player.lua BeginPlay
  1. local 상태 초기화
  2. DoorManager:Reset()
     - 문 상태 초기화
     - SoundManager:Reset()
  3. SoundManager:EnterTitleState()
     - bMuteUntilStart AudioComponent 추적
     - 이미 살아 있는 기존 AudioComponent fallback 음소거
  4. ToolManager:Reset()
  5. SettingManager:ApplyAll(obj)
  6. UIManager:ResetHospital()
  7. title_mode 설정
  8. EnterTitleScreen()
     - UIManager:ShowTitle()
     - 타이틀 카메라 점유
     - TitleMonkey:Ready()
```

실제 게임 시작 흐름은 다음 순서를 유지한다.

```txt
hospital_player.lua StartGame
  1. title 전환 중 상태 설정
  2. TitleMonkey:Strike()
  3. CameraManager.FadeOut() 시작
  4. hospital_player.lua Tick에서 전환 코루틴을 직접 갱신하며 fade-out 시간 대기
  5. 검은 화면 상태에서 현재 `GameManager:IsLoopStopped()` 값을 `bLastLoopStopped`에 동기화
  6. SoundManager:EnterPlayingState()
  7. HospitalPlayer.title_mode 해제
  8. UIManager:DisposeTitle()
  9. 플레이어 카메라 점유
 10. Title 태그 액터 비활성화
 11. CameraManager.FadeIn() 시작
 12. hospital_player.lua Tick에서 fade-in 시간 대기 후 전환 상태 해제
```

`EndPlay()`에서는 `DoorManager:Reset()`, `ToolManager:Reset()`, `UIManager:ResetHospital()`을 호출한다. `DoorManager:Reset()`이 `SoundManager:Reset()`을 호출하므로 지연 사운드와 시작 음소거 복원이 함께 정리된다.

`GameManager:StartGame()`은 게임을 루프 정지 상태로 시작한다. 타이틀 모드 중에는 `hospital_player.lua`의 gameplay Tick이 실행되지 않으므로, Start 버튼 전환 코루틴에서 실제 title mode를 해제하는 순간 현재 loop stopped 값을 기준값으로 저장해야 한다. 이 동기화를 하지 않으면 첫 gameplay Tick에서 시작 상태를 새 `LoopStopped` edge로 오인해 출구 문이 바로 열릴 수 있다.

`CoroutineManager.lua`의 `StartCoroutine()`은 `UpdateCoroutines(dt)`를 호출하는 액터가 씬에 있을 때만 진행된다. Hospital.Scene에는 별도 `GlobalManager.lua` 액터가 없으므로, 타이틀 시작 전환처럼 씬 진입점에서 반드시 끝나야 하는 코루틴은 `hospital_player.lua` 자신의 `Tick()`에서 직접 갱신한다.

## 입력 규칙

게임플레이 입력은 논리 액션/축을 우선 사용한다.

```lua
Input.GetAxis("MoveForward")
Input.GetAxis("MoveRight")
Input.GetActionDown("Interact")
Input.GetActionDown("Fire")
Input.GetAction("Aim")
```

키보드 raw key fallback은 임시 호환이 필요한 곳에만 둔다. 문 상호작용처럼 키보드와 패드 edge 상태가 충돌할 수 있는 입력은 `Input.GetActionDown("Interact")` 하나만 사용한다.

디버그 키, 에디터 전용 단축키, 아직 논리 액션으로 승격하지 않은 임시 조작은 raw key를 유지할 수 있다.

## 유지보수 규칙

1. 새 Hospital 씬 로직을 추가할 때 먼저 소유자를 정한다.
   - 게임 상태면 `GameManager`
   - 루프 상태면 `LoopManager`
   - 문이면 `DoorManager`
   - 사운드면 `SoundManager`
   - UI면 `UIManager`
   - 장비 선택이면 `ToolManager`
   - 사용자 설정이면 `SettingManager`
   - FPS 애니메이션 표현이면 `fps_character.lua`

2. Manager가 다른 Manager를 호출할 때 순환 의존을 늘리지 않는다.
   - 현재 `DoorManager -> SoundManager`, `DoorManager -> GameManager`는 문 이벤트 연결 때문에 허용된 구조다.
   - `UIManager -> ToolManager`는 표시 문구 생성을 위한 읽기 의존이다.
   - `UIManager -> SettingManager`는 설정 팝업 텍스트와 조작 프롬프트 표시 여부를 읽기 위한 의존이다.
   - 가능하면 `hospital_player.lua`가 여러 Manager를 조합하고, Manager끼리는 최소한으로 연결한다.

3. `require()` 모듈 상태는 월드 전환 뒤에도 남을 수 있다.
   - 씬 시작과 종료에서 `Reset()` 호출을 빠뜨리지 않는다.
   - `Reset()`은 런타임 객체 참조, 지연 큐, UI widget 참조를 정리해야 한다.

4. 월드 액터나 컴포넌트를 스캔할 때는 생성 side effect가 있는 getter를 피한다.
   - 기존 컴포넌트만 필요하면 `Actor:GetComponents()`를 사용한다.
   - 대표 컴포넌트를 만들거나 보장해야 하는 기능에서만 `Actor:GetAudioComponent()` 같은 생성 가능 API를 쓴다.

5. UI는 물리 키 이름을 직접 쓰지 않는다.
   - `UIManager:FormatActionPrompt()`와 `Input.GetActionMappingDisplayName()`을 사용한다.
   - fallback 문자열은 바인딩이 없을 때만 표시되는 기본값으로 둔다.

6. 새 Manager API를 추가하면 이 문서의 책임 표와 시작/종료 순서를 함께 갱신한다.

## 검증 체크리스트

Manager 구조나 Hospital 흐름을 바꾼 뒤에는 다음을 확인한다.

```powershell
Scripts\python\python.exe Scripts\GenerateHeaders.py --root KraftonEngine
git diff --check
```

C++ 바인딩, `UFUNCTION`, `UPROPERTY`, scene serialization, Lua binding을 함께 건드렸다면 `KraftonEngine.sln`의 `Debug|x64` 빌드까지 확인한다.

수동 확인 항목:

- 타이틀 화면에서 월드 ambient audio가 크게 튀지 않는다.
- StartGame 후 ambient audio가 원래 볼륨으로 복원된다.
- 문 프롬프트가 현재 입력 장치에 맞는 `Interact` 표시명을 보여준다.
- `E`와 패드 `Interact`가 같은 문 상호작용을 수행한다.
- 워프 후 문 상태, 출구 문 잠금, 장난감 projectile 정리가 유지된다.
- Pistol/Camera 전환 후 `ToolManager`, UI 문구, FPS 애니메이션 표시가 서로 어긋나지 않는다.
