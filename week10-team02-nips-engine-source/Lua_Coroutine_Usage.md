# Lua Coroutine Usage Guide

## 목적

이 문서는 NipsEngine에서 Lua script를 작성할 때 coroutine을 어떻게 사용하는지 설명한다.

현재 엔진의 기준은 다음과 같다.

- `coroutine.create`, `coroutine.resume`, `coroutine.yield`, `coroutine.status`는 Lua 표준 API 그대로 둔다.
- 엔진 Tick 기반 시간 대기와 자동 재개가 필요하면 `StartCoroutine`, `CreateCoroutine`, `ResumeCoroutine`, `CancelCoroutine`, `wait`, `yield`를 사용한다.

즉 `coroutine.*`는 Lua 내부 제어용이고, 엔진 scheduler와 연결되는 coroutine은 엔진 API로 시작한다.

## 빠른 예시

게임 로직에서 일정 시간 기다렸다가 이어서 실행하려면 보통 이 방식을 사용한다.

```lua
function BeginPlay(owner)
    StartCoroutine(function()
        print("coroutine start")

        wait(1.0)
        print("after 1 sec")

        wait(2.0)
        print("after 3 sec total")
    end)
end

function Tick(owner, deltaTime)
end
```

실행 흐름:

```text
BeginPlay
  -> StartCoroutine으로 엔진 scheduler coroutine 시작
  -> wait(1.0)에서 yield
  -> 엔진 scheduler가 1초 대기
  -> 자동 resume
  -> "after 1 sec" 출력
  -> wait(2.0)에서 다시 yield
  -> 엔진 scheduler가 2초 대기
  -> 자동 resume
  -> "after 3 sec total" 출력
  -> coroutine 종료
```

## Lua 표준 coroutine과 엔진 coroutine 차이

### Lua 표준 coroutine

Lua 표준 API는 그대로 사용할 수 있다.

```lua
function BeginPlay(owner)
    local co = coroutine.create(function()
        print("A")
        coroutine.yield("paused")
        print("B")
    end)

    local ok, value = coroutine.resume(co)
    print(ok, value)

    coroutine.resume(co)
end
```

이 방식은 Lua 표준 semantics 그대로 동작한다.

중요한 점:

- `coroutine.yield(1.0)`은 1초 대기가 아니다.
- `1.0`은 단순히 `coroutine.resume()`의 반환값으로 전달되는 값이다.
- 엔진 scheduler가 자동으로 재개하지 않는다.
- script 작성자가 직접 `coroutine.resume(co)`를 다시 호출해야 한다.

### 엔진 coroutine

엔진 coroutine은 C++ `LuaCoroutineScheduler`가 관리한다.

```lua
function BeginPlay(owner)
    StartCoroutine(function()
        print("A")
        wait(1.0)
        print("B after 1 sec")
    end)
end
```

이 방식은 `wait(seconds)`를 엔진 Tick 기반 대기 시간으로 해석한다.

```text
wait(1.0)
  -> Lua coroutine yield
  -> C++ scheduler가 1초 대기
  -> scheduler가 자동 resume
```

게임 로직에서 시간 지연이 필요하면 이 방식을 사용한다.

## StartCoroutine

```lua
local handle = StartCoroutine(function()
    print("start")

    wait(1.0)
    print("after 1 sec")
end)
```

`StartCoroutine(function)`은 엔진 scheduler에 coroutine을 등록하고 즉시 한 번 실행한다.

반환값은 C++ scheduler가 관리하는 handle id다.

```lua
function BeginPlay(owner)
    local handle = StartCoroutine(function()
        wait(5.0)
        print("done")
    end)

    -- handle은 CancelCoroutine(handle) 등에 사용할 수 있다.
end
```

주의:

- `StartCoroutine`으로 시작한 coroutine은 이미 첫 resume까지 수행된다.
- 보통 반환된 handle에 곧바로 `ResumeCoroutine(handle)`을 호출하지 않는다.
- `wait()` 이후의 재개는 scheduler가 자동으로 처리한다.

## CreateCoroutine

```lua
local handle = CreateCoroutine(function()
    print("created")
end)
```

`CreateCoroutine(function)`은 엔진 scheduler에 coroutine을 paused 상태로 등록한다.

생성만 하고 바로 실행하지 않으므로, 시작하려면 `ResumeCoroutine(handle)`을 호출해야 한다.

```lua
function BeginPlay(owner)
    local handle = CreateCoroutine(function()
        print("manual start")

        wait(1.0)
        print("after 1 sec")
    end)

    ResumeCoroutine(handle)
end
```

`StartCoroutine(function)`은 사실상 `CreateCoroutine(function)` 후 `ResumeCoroutine(handle)`을 바로 호출하는 편의 함수로 볼 수 있다.

## ResumeCoroutine

```lua
ResumeCoroutine(handle)
```

`ResumeCoroutine(handle)`은 `CreateCoroutine`으로 만든 엔진 coroutine을 실행하거나 재개한다.

일반적으로는 처음 시작할 때 한 번만 직접 호출한다.

```lua
local handle = nil

function BeginPlay(owner)
    handle = CreateCoroutine(function()
        print("start")
        wait(1.0)
        print("after wait")
    end)

    ResumeCoroutine(handle)
end
```

나쁜 예:

```lua
function Tick(owner, deltaTime)
    ResumeCoroutine(handle) -- 매 frame 호출하지 말 것
end
```

`wait(seconds)` 이후의 재개는 scheduler가 자동으로 처리한다. 같은 handle을 매 frame resume하면 이미 실행 중이거나 끝난 coroutine을 다시 resume하려 해서 오류가 날 수 있다.

## CancelCoroutine

```lua
CancelCoroutine(handle)
```

`CancelCoroutine(handle)`은 scheduler에 등록된 엔진 coroutine을 제거한다.

예시:

```lua
local handle = nil

function BeginPlay(owner)
    handle = StartCoroutine(function()
        print("start long wait")

        wait(10.0)
        print("this will not run if canceled")
    end)
end

function EndPlay(owner)
    if handle ~= nil then
        CancelCoroutine(handle)
        handle = nil
    end
end
```

사용할 만한 상황:

- Actor가 사라지기 전에 지연 로직을 취소하고 싶을 때
- 특정 이벤트가 발생하면 대기 중인 coroutine을 중단하고 싶을 때
- 동일한 행동을 중복 시작하지 않도록 기존 coroutine을 끊고 새로 시작할 때

## wait

```lua
wait(seconds)
```

`wait(seconds)`는 엔진 coroutine 내부에서 현재 coroutine을 멈추고, scheduler에게 대기 시간을 넘긴다.

```lua
StartCoroutine(function()
    print("A")
    wait(1.0)
    print("B")
end)
```

기대 동작:

```text
A 출력
약 1초 뒤
B 출력
```

주의:

- `wait(seconds)`는 엔진 coroutine 내부에서만 사용한다.
- 일반 callback 함수 최상위에서 바로 호출하지 않는다.

잘못된 예:

```lua
function BeginPlay(owner)
    wait(1.0)
    print("after 1 sec")
end
```

올바른 예:

```lua
function BeginPlay(owner)
    StartCoroutine(function()
        wait(1.0)
        print("after 1 sec")
    end)
end
```

## yield

```lua
yield(seconds)
```

`yield(seconds)`는 `wait(seconds)`와 같은 의미로 남겨둔 전역 alias다.

```lua
StartCoroutine(function()
    print("A")
    yield(1.0)
    print("B")
end)
```

새 script에서는 의도가 더 명확한 `wait(seconds)`를 권장한다.

Lua 표준 coroutine의 `coroutine.yield(...)`와 엔진 alias `yield(...)`는 다르다.

```lua
coroutine.yield(1.0) -- Lua 표준 yield. 엔진 scheduler 대기가 아님.
yield(1.0)           -- 엔진 coroutine용 alias. wait(1.0)과 같음.
```

## 여러 엔진 coroutine 실행

```lua
function BeginPlay(owner)
    StartCoroutine(function()
        print("first start")
        wait(1.0)
        print("first done")
    end)

    StartCoroutine(function()
        print("second start")
        wait(2.0)
        print("second done")
    end)
end
```

기대 로그:

```text
first start
second start
약 1초 뒤
first done
약 2초 뒤
second done
```

## owner와 함께 사용하기

Lua lifecycle 함수는 C++에서 owner actor를 인자로 받는다.

```lua
function BeginPlay(owner)
    print("BeginPlay", owner:GetName())
end

function Tick(owner, deltaTime)
end
```

엔진 coroutine 내부에서도 `owner`를 캡처해서 사용할 수 있다.

```lua
function BeginPlay(owner)
    StartCoroutine(function()
        print("owner", owner:GetName())

        wait(1.0)
        print("after wait", owner:GetName())
    end)
end
```

## Event callback에서 사용하기

Overlap이나 Hit 이벤트에서도 엔진 coroutine을 시작할 수 있다.

```lua
function OnOverlap(owner, otherActor)
    StartCoroutine(function()
        print("overlap start", owner:GetName())

        wait(1.0)

        if otherActor ~= nil then
            print("after overlap wait", otherActor:GetName())
        end
    end)
end
```

주의할 점은 `otherActor`가 대기 중 삭제될 수 있다는 것이다. 현재 엔진 Lua binding에서 객체 lifetime 안전성이 완전히 보장되지 않는다면, 장기 대기 후에는 nil 체크나 유효성 체크 API가 필요할 수 있다.

## print 출력

엔진은 Lua `print(...)`를 editor console로 연결해 둔다.

```lua
print("hello")
print("value", 1.0, true)
```

출력 예:

```text
[Lua] hello
[Lua] value    1.000000    true
```

coroutine 테스트는 `print()` 로그로 확인하는 것이 가장 쉽다.

## 표준 방식과 엔진 방식 비교

### Lua 표준 coroutine

```lua
function BeginPlay(owner)
    local co = coroutine.create(function()
        print("start")
        coroutine.yield("paused")
        print("after manual resume")
    end)

    local ok, value = coroutine.resume(co)
    print(ok, value)

    coroutine.resume(co)
end
```

특징:

- Lua 원래 semantics 그대로 동작한다.
- 엔진이 시간을 재서 자동 resume하지 않는다.
- 직접 `coroutine.resume(co)`를 호출해야 한다.

### 엔진 coroutine

```lua
function BeginPlay(owner)
    StartCoroutine(function()
        print("start")
        wait(1.0)
        print("after 1 sec")
    end)
end
```

특징:

- 엔진 scheduler가 관리한다.
- `wait(seconds)`로 시간 대기가 가능하다.
- 대기 후 자동 resume된다.

## 자주 나는 오류

### cannot resume non-suspended coroutine

이 오류는 보통 실행 중이거나 이미 끝난 coroutine을 다시 resume하려고 할 때 발생한다.

예를 들어 같은 엔진 coroutine handle을 매 frame resume하면 문제가 생길 수 있다.

```lua
local handle

function BeginPlay(owner)
    handle = StartCoroutine(function()
        wait(1.0)
        print("done")
    end)
end

function Tick(owner, deltaTime)
    ResumeCoroutine(handle) -- 잘못된 사용
end
```

올바른 방식은 처음 시작할 때만 직접 resume하고, 이후 재개는 엔진 scheduler에 맡기는 것이다.

```lua
function BeginPlay(owner)
    StartCoroutine(function()
        wait(1.0)
        print("done")
    end)
end
```

### wait이 동작하지 않음

`wait()`는 일반 Lua 표준 coroutine이 아니라 엔진 coroutine 내부에서 사용해야 한다.

잘못된 예:

```lua
local co = coroutine.create(function()
    wait(1.0)
    print("after 1 sec")
end)

coroutine.resume(co)
```

위 코드는 엔진 scheduler가 관리하는 coroutine이 아니므로 의도한 방식으로 동작하지 않을 수 있다.

올바른 예:

```lua
StartCoroutine(function()
    wait(1.0)
    print("after 1 sec")
end)
```

### 로그가 안 찍힘

다음을 확인한다.

- Actor에 `LuaScriptComponent`가 붙어 있는지
- script file이 `ScriptPath`에 정상 연결되어 있는지
- `Reload Script`가 성공했는지
- Lua 문법 오류가 없는지
- PIE가 실행되어 `BeginPlay()`가 호출되는지

## 권장 템플릿

새 script는 다음 형태로 시작하면 된다.

```lua
function BeginPlay(owner)
    print("BeginPlay", owner:GetName())

    StartCoroutine(function()
        print("coroutine start")

        wait(1.0)
        print("after 1 sec")
    end)
end

function Tick(owner, deltaTime)
end

function EndPlay(owner)
end

function OnOverlap(owner, otherActor)
end

function OnEndOverlap(owner, otherActor)
end

function OnHit(owner, hit)
end
```
