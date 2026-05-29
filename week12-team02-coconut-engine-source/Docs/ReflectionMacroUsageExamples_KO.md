# 리플렉션 매크로 사용 예제

이 문서는 `UCLASS`, `USTRUCT`, `UENUM`, `UPROPERTY` 를 처음 쓰는 팀원이 헤더를 작성할 때 바로 참고할 수 있는 예제다.

이 매크로들은 컴파일 시점에는 비어 있는 표식이고, `Scripts/GenerateCode.py` 가 헤더를 읽어 리플렉션용 `.generated.h` 와 `.gen.cpp` 를 만든다. 따라서 매크로를 붙인 타입은 헤더 배치 규칙도 같이 지켜야 한다.

## 기본 규칙

1. 리플렉션 매크로가 들어간 헤더는 같은 파일 이름의 generated header를 include한다.
2. `UCLASS()` 와 `USTRUCT()` 타입 본문에는 `GENERATED_BODY(TypeName)` 를 넣는다.
3. `UENUM()` 은 enum 선언 앞에 붙이고, enum 본문에는 `GENERATED_BODY` 를 넣지 않는다.
4. `UPROPERTY(...)` 는 리플렉션에 등록할 멤버 변수 선언 바로 앞에 붙인다.

예를 들어 `PatrolMovementComponent.h` 라면 generated header 이름은 `PatrolMovementComponent.generated.h` 다.

## 한 파일 예제

```cpp
#pragma once

#include "Component/Movement/MovementComponent.h"
#include "Math/Vector.h"

#include "PatrolMovementComponent.generated.h"

UENUM()
enum class EPatrolMode
{
	Once,
	Loop,
	PingPong,
};

USTRUCT()
struct FPatrolSettings
{
	GENERATED_BODY(FPatrolSettings)

	UPROPERTY(Edit, Category="Patrol", DisplayName="Move Speed", Min=0.0f, Max=100.0f, Speed=0.1f)
	float MoveSpeed = 10.0f;

	UPROPERTY(Edit, Category="Patrol", DisplayName="Wait Time", Min=0.0f, Max=30.0f, Speed=0.1f)
	float WaitTime = 0.5f;

	UPROPERTY(Edit, Category="Patrol", DisplayName="Loop Enabled")
	bool bLoop = true;
};

UCLASS()
class UPatrolMovementComponent : public UMovementComponent
{
public:
	GENERATED_BODY(UPatrolMovementComponent)

	UPatrolMovementComponent() = default;

private:
	UPROPERTY(Edit, Category="Patrol", DisplayName="Patrol Mode")
	EPatrolMode PatrolMode = EPatrolMode::Loop;

	UPROPERTY(Edit, Category="Patrol", DisplayName="Settings")
	FPatrolSettings Settings;

	UPROPERTY(Edit, Category="Patrol", DisplayName="Control Points")
	TArray<FVector> ControlPoints;

	UPROPERTY(Transient)
	FVector LastRuntimePosition;
};
```

이 예제에서 각 매크로의 역할은 다음과 같다.

| 매크로 | 예제에서 하는 일 |
| --- | --- |
| `UENUM()` | `EPatrolMode` 의 enum 메타데이터를 생성한다. `UPROPERTY` 로 enum 멤버를 등록하면 enum property로 분류된다. |
| `USTRUCT()` | `FPatrolSettings` 의 struct 메타데이터와 내부 property 목록을 생성한다. |
| `UCLASS()` | `UPatrolMovementComponent` 의 class 메타데이터와 property 목록을 생성한다. |
| `UPROPERTY(...)` | 멤버 변수를 property로 등록한다. `Edit` 가 있으면 에디터 property panel에서 편집 대상으로 노출된다. |

## `UCLASS` 예제

`UCLASS` 는 class 선언 바로 앞에 둔다.

```cpp
UCLASS(Actor)
class APatrolRouteActor : public AActor
{
public:
	GENERATED_BODY(APatrolRouteActor)
};
```

현재 codegen에서 자주 쓰는 class 표기는 다음과 같다.

| 표기 | 용도 |
| --- | --- |
| `UCLASS()` | 기본 reflected class |
| `UCLASS(Actor)` | actor class 플래그를 붙일 때 |
| `UCLASS(HiddenInComponentList)` | 에디터의 component 추가 목록에서 숨길 때 |
| `UCLASS(NoFactory)` | factory 자동 등록을 건너뛸 때 |

프로젝트 코드에는 `UCLASS(Component)` 와 `UCLASS(Camera)` 도 지원 플래그로 준비되어 있다. 새 타입이 기존 상속 계층과 어떤 플래그를 쓰는지 먼저 주변 헤더를 확인하고 맞추는 편이 좋다.

## `USTRUCT` 예제

`USTRUCT` 는 editor에서 하위 필드를 펼쳐 편집하거나 직렬화 대상으로 다룰 struct에 붙인다.

```cpp
USTRUCT()
struct FSpawnRange
{
	GENERATED_BODY(FSpawnRange)

	UPROPERTY(Edit, Category="Spawn", DisplayName="Min Distance", Min=0.0f, Speed=1.0f)
	float MinDistance = 100.0f;

	UPROPERTY(Edit, Category="Spawn", DisplayName="Max Distance", Min=0.0f, Speed=1.0f)
	float MaxDistance = 500.0f;
};
```

`USTRUCT` 를 붙였더라도 내부 멤버가 자동으로 모두 property가 되지는 않는다. 리플렉션에 포함할 필드마다 `UPROPERTY` 를 붙인다.

## `UENUM` 예제

`UENUM` 으로 알려진 enum은 `UPROPERTY` 멤버 타입으로 사용할 수 있다.

```cpp
UENUM()
enum class ESpawnPolicy
{
	Disabled,
	OnBeginPlay,
	Repeated,
};

UPROPERTY(Edit, Category="Spawn", DisplayName="Spawn Policy")
ESpawnPolicy SpawnPolicy = ESpawnPolicy::OnBeginPlay;
```

enum 자체에는 `GENERATED_BODY` 가 필요 없다.

## `UPROPERTY` 예제

아무 옵션 없이 등록할 수도 있다.

```cpp
UPROPERTY()
int32 SavedCount = 0;
```

에디터에서 편집할 값에는 `Edit` 와 표시용 메타데이터를 함께 쓰는 패턴이 많다.

```cpp
UPROPERTY(Edit, Category="Movement", DisplayName="Max Speed", Min=0.0f, Max=50.0f, Speed=0.1f)
float MaxSpeed = 10.0f;
```

런타임 캐시처럼 저장 대상에서 제외할 값은 `Transient` 를 붙인다.

```cpp
UPROPERTY(Transient)
FVector CachedDirection;
```

현재 property flag로 파싱되는 표기는 다음과 같다.

| 표기 | 메모 |
| --- | --- |
| `Edit` | 에디터 편집 대상 |
| `Transient` | 일반 저장 대상에서 제외할 임시 값 |
| `DuplicateTransient` | duplicate 경로에서 별도 취급할 값 |
| `NonPIEDuplicateTransient` | PIE duplicate 경로에서 별도 취급할 값 |
| `Config` | config property 플래그 |
| `FixedSize` | 배열 크기 변경을 막는 property 플래그 |

자주 쓰는 metadata는 다음과 같다.

| metadata | 예 |
| --- | --- |
| `Category` | `Category="Movement"` |
| `DisplayName` | `DisplayName="Max Speed"` |
| `Min` | `Min=0.0f` |
| `Max` | `Max=50.0f` |
| `Speed` | `Speed=0.1f` |

## 작성 체크리스트

1. 헤더가 `Source` 아래에 있고, generated header include 이름이 실제 헤더 stem과 같은지 확인한다.
2. `UCLASS` 와 `USTRUCT` 본문에 타입 이름을 맞춘 `GENERATED_BODY(...)` 가 있는지 확인한다.
3. `UPROPERTY` 뒤 선언 타입이 codegen이 아는 타입인지 확인한다. 기본 타입, `UENUM` 타입, `USTRUCT` 타입, `TArray<T>` 는 주변 예제를 참고한다.
4. 새 source/header 파일을 프로젝트 파일에 반영해야 하면 `python Scripts/GenerateProjectFiles.py` 를 실행한다.
5. 빌드 전 codegen은 MSBuild의 `GenerateCode` target에서 실행된다. 필요하면 `python Scripts/GenerateCode.py --verbose` 로 생성 결과를 직접 확인한다.

더 자세한 동작 원리는 [`PropertyReflectionSystem.md`](PropertyReflectionSystem.md) 와 [`SpecialPropertyTypes.md`](SpecialPropertyTypes.md) 를 참고한다.
