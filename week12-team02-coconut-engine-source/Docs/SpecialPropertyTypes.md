# 특수 프로퍼티 타입 처리

[`PropertyReflectionSystem.md`](PropertyReflectionSystem.md)가 리플렉션 시스템의 큰 흐름을 설명한다면, 이 문서는 기본 수치 타입을 넘어서는 프로퍼티들이 실제로 어떤 메타데이터를 들고 있고 codegen이 무엇을 생성하는지 정리한다.

여기서 다루는 타입은 다음과 같다.

1. `FEnumProperty` - `UENUM()` / `enum class`
2. `FStructProperty` - `USTRUCT()` 구조체
3. `FArrayProperty` - `TArray<T>`
4. `FSceneComponentRefProperty` - 컴포넌트 경로 참조
5. `FSoftObjectProperty` - 에셋 경로 기반 soft 참조
6. `FObjectProperty` / `FClassProperty` - hard object/class 참조의 예정 설계

---

## 0. 공통 모델

모든 프로퍼티 디스크립터는 `FProperty` 를 상속한다. `FProperty` 는 값을 직접 소유하지 않고, 소유 객체나 구조체 안에서 값이 놓인 위치만 알고 있다.

```cpp
void* ContainerPtrToValuePtr(void* Container) const
{
    return static_cast<char*>(Container) + Offset_Internal;
}
```

직렬화와 역직렬화의 공통 인터페이스도 `FProperty` 에 있다.

```cpp
virtual json::JSON Serialize(const void* Instance) const = 0;
virtual void Deserialize(void* Instance, const json::JSON& Value) const = 0;
```

기본 타입은 offset과 size만으로 충분하지만, enum, struct, array, object reference는 추가 메타데이터가 필요하다. 예를 들어 enum은 가능한 이름과 값을 알아야 하고, struct는 자식 프로퍼티 목록을 알아야 하며, array는 `TArray<T>` 의 원소 개수와 원소 주소를 타입 소거된 방식으로 얻어야 한다.

---

## 1. `FEnumProperty` - `UENUM()` / `enum class`

### 저장 형태

C++ 쪽 값은 일반 enum 값 그대로 객체 메모리 안에 저장된다.

```cpp
UENUM()
enum class EInterpBehaviour
{
    OneShot,
    OneShotReverse,
    Loop,
    PingPong
};

UPROPERTY(Edit, Category="Movement")
EInterpBehaviour InterpBehaviour = EInterpBehaviour::OneShot;
```

`FEnumProperty` 는 enum 값을 해석하기 위해 `UEnum*` 을 들고 있다.

```cpp
class FEnumProperty final : public FProperty
{
public:
    UEnum* GetEnum() const { return EnumDesc; }

private:
    UEnum* EnumDesc = nullptr;
};
```

`UEnum` 은 이름과 실제 값을 함께 보관한다. 그래서 `enum class EFoo : uint8 { A = 5, B = 10 }` 처럼 값이 듬성듬성 있거나 underlying type이 작은 경우도 표현할 수 있다.

```cpp
class UEnum : public UField
{
public:
    void AddEnumerator(const char* InName, int64 InValue);
    FString GetNameByValue(int64 Value) const;
    int64 GetValueByName(FString Name) const;
    uint32 GetUnderlyingSize() const;
};
```

### Codegen

`UENUM()` 이 붙은 타입에 대해 `.gen.cpp` 는 정적 `UEnum` 객체를 만든다.

```cpp
UEnum* StaticEnum_EInterpBehaviour()
{
    static UEnum Enum("EInterpBehaviour",
                      sizeof(EInterpBehaviour),
                      ECppForm::EnumClass);
    static const bool bRegistered = []()
    {
        Enum.AddEnumerator("OneShot",
            static_cast<int64>(EInterpBehaviour::OneShot));
        Enum.AddEnumerator("Loop",
            static_cast<int64>(EInterpBehaviour::Loop));
        return true;
    }();
    (void)bRegistered;
    return &Enum;
}
```

프로퍼티 등록 시에는 `FEnumProperty` 생성자에 `StaticEnum_*()` 결과를 넘긴다.

```cpp
Cls->AddProperty(new FEnumProperty(
    "Interpolation Behaviour", "Movement", CPF_Edit,
    offsetof(ThisClass, InterpBehaviour),
    sizeof(((ThisClass*)0)->InterpBehaviour),
    StaticEnum_EInterpBehaviour()));
```

### 직렬화

현재 JSON에는 enum의 정수 값이 저장된다. 메모리에서 읽고 쓸 때는 `UEnum::GetUnderlyingSize()` 를 사용해 실제 enum 크기만큼만 복사한다.

```cpp
int64 Value = 0;
std::memcpy(&Value, ValuePtr, EnumDesc->GetUnderlyingSize());
return json::JSON(static_cast<int32>(Value));
```

주의할 점은 최종 JSON 값이 아직 `int32` 로 내려간다는 것이다. 64비트 enum 값을 온전히 보존하려면 JSON 숫자 처리 쪽도 함께 넓혀야 한다.

---

## 2. `FStructProperty` - `USTRUCT()` 구조체

### 저장 형태

`USTRUCT()` 값도 일반 C++ 구조체로 객체 안에 inline 저장된다.

```cpp
USTRUCT()
struct FTransform
{
    GENERATED_BODY(FTransform)

    UPROPERTY(Edit, Category="Transform")
    FVector Location;

    FQuat Rotation;

    UPROPERTY(Edit, Category="Transform")
    FVector Scale;
};
```

`FStructProperty` 는 해당 타입의 메타 객체인 `UScriptStruct*` 를 들고 있다.

```cpp
class FStructProperty final : public FProperty
{
public:
    UScriptStruct* ScriptStruct = nullptr;

    const TArray<FProperty*>& GetStructProperties() const
    {
        if (ScriptStruct) return ScriptStruct->GetProperties();
        static const TArray<FProperty*> Empty;
        return Empty;
    }
};
```

이전처럼 "자식 프로퍼티 목록을 돌려주는 자유 함수 포인터" 를 들고 있는 대신, 이제 구조체도 `UObject -> UField -> UStruct -> UScriptStruct` 계층 안의 정식 메타 객체로 취급된다.

### `UScriptStruct` 와 `ICppStructOps`

`UScriptStruct` 는 두 종류의 정보를 묶는다.

- 리플렉션 정보: 이름, 크기, 정렬, 부모 구조체, 자식 `FProperty` 목록
- C++ 생명주기 정보: 생성, 파괴, 복사 방법

두 번째 역할을 담당하는 인터페이스가 `ICppStructOps` 이다.

```cpp
class ICppStructOps
{
public:
    virtual ~ICppStructOps() = default;

    virtual void Construct(void* Dest) const = 0;
    virtual void Destruct(void* Dest) const = 0;
    virtual void Copy(void* Dest, const void* Src) const = 0;
};

template <typename T>
class TCppStructOps final : public ICppStructOps
{
public:
    void Construct(void* Dest) const override { new (Dest) T(); }
    void Destruct(void* Dest) const override { static_cast<T*>(Dest)->~T(); }
    void Copy(void* Dest, const void* Src) const override
    {
        *static_cast<T*>(Dest) = *static_cast<const T*>(Src);
    }
};
```

codegen은 각 `USTRUCT` 타입마다 `TCppStructOps<T>` 정적 인스턴스를 만들고, 그 포인터를 `UScriptStruct` 에 넘긴다.

```cpp
static const TCppStructOps<FTransform> GFTransformCppStructOps;

UScriptStruct FTransform::StaticStructInstance(
    "FTransform", nullptr,
    sizeof(FTransform), alignof(FTransform),
    &GFTransformCppStructOps);
```

`UScriptStruct` 는 이 포인터를 통해 타입을 모르는 코드에서도 구조체 인스턴스를 다룰 수 있다.

```cpp
void InitializeStruct(void* Dest) const
{
    if (CppStructOps) CppStructOps->Construct(Dest);
}

void DestroyStruct(void* Dest) const
{
    if (CppStructOps) CppStructOps->Destruct(Dest);
}

void CopyScriptStruct(void* Dest, const void* Src) const
{
    if (CppStructOps) CppStructOps->Copy(Dest, Src);
}
```

### 왜 필요한가

리플렉션 시스템은 대부분의 순간에 C++ 템플릿 타입 `T` 를 모른다. 에디터, serializer, GC, duplication 같은 공통 코드는 `FProperty*` 나 `UScriptStruct*` 만 들고 "이 구조체 값을 하나 만들어라", "이 슬롯을 파괴해라", "이 값을 복사해라" 를 수행해야 한다.

`ICppStructOps` 가 없으면 다음 작업이 구조체 타입마다 별도의 템플릿 코드나 수작업 분기로 흩어진다.

- inline 구조체 값을 기본 생성하기
- 구조체 멤버가 가진 `FString`, `TArray`, smart pointer 같은 non-trivial 멤버를 안전하게 파괴하기
- 에디터 복제, prefab 복제, undo/redo, asset import 과정에서 구조체를 타입 소거된 방식으로 복사하기
- 향후 `FStructProperty` / `FArrayProperty` 를 따라 GC reference를 순회할 때 임시 구조체 storage를 안전하게 다루기

현재 `GetTArrayAccessor<T>()` 는 실제 `TArray<T>` 를 알고 생성되므로 `emplace_back()` 만으로 `T` 의 생성자를 호출할 수 있다. 그래도 `UScriptStruct` 가 `ICppStructOps` 를 갖는 것은 중요하다. 배열 accessor처럼 템플릿이 살아 있는 좁은 경로 밖에서도, 메타 객체만으로 구조체 생명주기를 다룰 수 있어야 하기 때문이다.

### Codegen

`USTRUCT()` 에 대해 codegen은 다음을 생성한다.

```cpp
static const TCppStructOps<FTransform> GFTransformCppStructOps;

UScriptStruct FTransform::StaticStructInstance(
    "FTransform", nullptr,
    sizeof(FTransform), alignof(FTransform),
    &GFTransformCppStructOps);

static void RegisterFTransformStructProperties(UScriptStruct* Struct)
{
    static bool bRegistered = false;
    if (bRegistered || !Struct) return;
    bRegistered = true;

    Struct->AddProperty(new FVec3Property(
        "Location", "Transform", CPF_Edit,
        offsetof(FTransform, Location),
        sizeof(((FTransform*)0)->Location)));
}
```

클래스 멤버로 구조체가 들어가면 `FStructProperty` 에 `FTransform::StaticStruct()` 가 등록된다.

```cpp
Cls->AddProperty(new FStructProperty(
    "Transform", "Actor", CPF_Edit,
    offsetof(ThisClass, ActorTransform),
    sizeof(((ThisClass*)0)->ActorTransform),
    FTransform::StaticStruct()));
```

### 직렬화

`FStructProperty` 는 `UScriptStruct` 의 자식 프로퍼티 목록을 순회해서 JSON object를 만든다.

```cpp
json::JSON Object = json::Object();
for (const FProperty* Child : ScriptStruct->GetProperties())
{
    Object[Child->Name] = Child->Serialize(StructInstance);
}
```

역직렬화는 JSON에 존재하는 키만 반영한다. 그래서 구조체에 필드를 추가해도 기존 저장 파일을 어느 정도 유지할 수 있다.

---

## 3. `FArrayProperty` - `TArray<T>`

### 저장 형태

엔진의 `TArray<T>` 는 현재 `std::vector<T>` 계열 컨테이너다. 배열 자체의 메모리와 원소 생명주기는 컨테이너가 관리한다.

리플렉션은 `T` 를 직접 모르는 상태로 배열을 다뤄야 하므로, `FArrayAccessor` 라는 함수 포인터 테이블을 사용한다.

```cpp
struct FArrayAccessor
{
    uint32 (*Num)(const void*);
    void*  (*GetAt)(void*, uint32);
    void   (*AddDefault)(void*);
    void   (*RemoveAt)(void*, uint32);
    void   (*Clear)(void*);
    void   (*Assign)(void*, const void*);
};

template <typename T>
inline FArrayAccessor* GetTArrayAccessor()
{
    static FArrayAccessor s = {
        +[](const void* A) -> uint32 {
            return (uint32)static_cast<const TArray<T>*>(A)->size();
        },
        +[](void* A, uint32 i) -> void* {
            return &(*static_cast<TArray<T>*>(A))[i];
        },
        +[](void* A) {
            static_cast<TArray<T>*>(A)->emplace_back();
        },
        +[](void* A, uint32 i) {
            auto& V = *static_cast<TArray<T>*>(A);
            V.erase(V.begin() + i);
        },
        +[](void* A) {
            static_cast<TArray<T>*>(A)->clear();
        },
        +[](void* D, const void* S) {
            *static_cast<TArray<T>*>(D) = *static_cast<const TArray<T>*>(S);
        },
    };
    return &s;
}
```

`FArrayProperty` 는 두 가지를 들고 있다.

```cpp
class FArrayProperty final : public FProperty
{
public:
    std::unique_ptr<FProperty> Inner;
    FArrayAccessor* Accessor = nullptr;
};
```

`Inner` 는 원소 하나를 설명하는 디스크립터다. `TArray<float>` 라면 `FFloatProperty`, `TArray<EFoo>` 라면 `FEnumProperty`, `TArray<FTransform>` 이라면 `FStructProperty` 가 된다. 배열 원소는 `Accessor->GetAt()` 이 이미 원소 주소를 돌려주므로 inner property의 offset은 `0` 이다.

### Codegen

클래스 멤버 `TArray<FMaterialSlot>` 은 다음 형태로 등록된다.

```cpp
Cls->AddProperty(new FArrayProperty(
    "Materials", "Materials", CPF_Edit | CPF_FixedSize,
    offsetof(ThisClass, MaterialSlots),
    sizeof(((ThisClass*)0)->MaterialSlots),
    std::unique_ptr<FProperty>(new FMaterialSlotProperty(
        "Element", "Materials", CPF_None,
        0, sizeof(FMaterialSlot))),
    GetTArrayAccessor<FMaterialSlot>()));
```

원소 타입이 `USTRUCT` 라면 inner는 `FStructProperty` 가 된다.

```cpp
Cls->AddProperty(new FArrayProperty(
    "Transforms", "Animation", CPF_Edit,
    offsetof(ThisClass, Transforms),
    sizeof(((ThisClass*)0)->Transforms),
    std::unique_ptr<FProperty>(new FStructProperty(
        "Element", "Animation", CPF_None,
        0, sizeof(FTransform),
        FTransform::StaticStruct())),
    GetTArrayAccessor<FTransform>()));
```

`Scripts/GenerateCode.py` 는 이제 클래스 프로퍼티뿐 아니라 `USTRUCT` 내부 필드에서도 같은 배열 등록 경로를 사용한다. 따라서 `USTRUCT` 안의 `UPROPERTY() TArray<FSomeStruct>` 도 codegen 단계에서 막히지 않는다.

### 직렬화

`FArrayProperty::Serialize` 는 배열 원소를 순회하고, 실제 원소 값의 직렬화는 `Inner` 에 맡긴다.

```cpp
json::JSON Array = json::Array();
const uint32 Count = Accessor->Num(ArrayInstance);
for (uint32 Index = 0; Index < Count; ++Index)
{
    void* Element = Accessor->GetAt(ArrayInstance, Index);
    Array.append(Inner->Serialize(Element));
}
```

역직렬화에서는 고정 크기 배열인지 여부가 중요하다.

- `CPF_FixedSize` 가 없으면 기존 배열을 `Clear()` 하고 JSON 원소마다 `AddDefault()` 한 뒤 채운다.
- `CPF_FixedSize` 가 있으면 기존 원소 수를 유지하고, JSON 원소 수가 더 많으면 남는 값은 무시한다.

### 현재 한계

- `TArray<UObject*>` 는 inner가 `FObjectProperty` 여야 하지만, hard object property가 아직 실구현 전이라 사용할 수 없다.
- 중첩 배열 `TArray<TArray<T>>` 는 parser와 inner property 생성이 아직 단순해서 지원 대상으로 보지 않는 편이 안전하다.
- `TArray<USTRUCT>` 는 codegen상 지원된다. 다만 원소 구조체의 필드 중 아직 지원하지 않는 타입이 있으면 그 inner property 생성에서 실패한다.

---

## 4. `FSceneComponentRefProperty` - 컴포넌트 경로 참조

`UMovementComponent::UpdatedComponent` 같은 값은 같은 actor 안의 scene component를 가리켜야 한다. raw pointer를 저장하면 직렬화와 재연결이 까다롭기 때문에, 현재는 `FString` 경로를 저장하는 특수 케이스로 처리한다.

```cpp
UPROPERTY(Edit, Category="Movement")
FString UpdatedComponentPath;
```

codegen은 `USceneComponent*` 를 `EPropertyType::SceneComponentRef` 로 분류하고, 실제 프로퍼티는 `FString` 을 serialize/deserialize한다. 에디터 UI는 owner actor의 component tree를 보고 선택지를 만든다.

장기적으로는 `FObjectProperty` 가 도입된 뒤에도 이 타입은 별도 정책이 필요하다. scene component는 에셋이 아니라 actor 내부 object이므로, asset path 기반 `FSoftObjectProperty` 와도 다르다.

---

## 5. `FSoftObjectProperty` - 에셋 경로 기반 soft 참조

soft object pointer는 "지금 메모리에 없어도 되는 에셋" 을 가리킨다. 저장되는 값은 object pointer가 아니라 경로다.

```cpp
UPROPERTY(Edit, Category="Mesh", Class=UStaticMesh)
TSoftObjectPtr<UStaticMesh> StaticMesh;
```

`FSoftObjectProperty` 는 `FObjectPropertyBase` 를 상속하고, 허용되는 클래스 타입을 `PropertyClass` 로 보관한다.

```cpp
class FObjectPropertyBase : public FProperty
{
public:
    UClass* PropertyClass = nullptr;
};
```

codegen은 `Class=UStaticMesh` 메타데이터를 읽어 `UStaticMesh::StaticClass()` 를 넘긴다.

```cpp
new FSoftObjectProperty(
    "Static Mesh", "Mesh", CPF_Edit,
    offsetof(ThisClass, StaticMesh),
    sizeof(((ThisClass*)0)->StaticMesh),
    UStaticMesh::StaticClass());
```

직렬화는 경로 문자열만 저장한다. 역직렬화도 우선 경로만 복원하고, 실제 object resolve는 `Get()` 시점에 `FSoftObjectPath::ResolveObject()` 를 통해 시도한다.

현재 `ResolveObject()` 는 `GUObjectArray` 를 선형 탐색하므로 에셋 수가 늘어나면 path -> object 인덱스가 필요해질 수 있다.

---

## 6. `FObjectProperty` / `FClassProperty` - 예정 설계

현재 두 헤더는 빈 스텁에 가깝다.

```cpp
// FObjectProperty.h
#pragma once

// FClassProperty.h
#pragma once
```

도입 시 예상되는 역할은 다음과 같다.

### `FObjectProperty`

`UObject*` hard reference를 표현한다. 메모리에는 raw pointer가 들어 있고, 프로퍼티는 허용 타입을 `PropertyClass` 로 제한한다.

```cpp
class FObjectProperty : public FObjectPropertyBase
{
public:
    UObject* GetObjectPropertyValue(void* Addr) const override
    {
        return *static_cast<UObject**>(Addr);
    }

    void SetObjectPropertyValue(void* Addr, UObject* Value) const override
    {
        if (Value && PropertyClass && !Value->IsA(PropertyClass))
        {
            return;
        }
        *static_cast<UObject**>(Addr) = Value;
    }
};
```

가장 큰 결정은 직렬화 ID다. 같은 scene 안의 object reference라면 UUID가 자연스럽지만, 로딩 순서 문제를 피하려면 object 생성과 reference fix-up을 분리하는 2-pass load가 필요하다.

### `FClassProperty`

`UClass*` 를 표현한다. 값 자체는 object pointer와 비슷하지만, 항상 `UClass` 인스턴스를 가리키며 `MetaClass` 로 "어떤 클래스의 자식만 허용할지" 를 제한할 수 있다.

```cpp
UPROPERTY(Edit, Category="Spawn", MetaClass=AActor)
UClass* SpawnableClass = nullptr;
```

직렬화는 UUID보다 class name이 더 자연스럽다. 정적 class object는 이름으로 찾을 수 있고, 일반 runtime object보다 생명주기가 안정적이기 때문이다.

### GC 관점

`FObjectProperty` 는 hard reference이므로 GC mark 단계에서 참조 그래프를 이어야 한다. 반대로 `FSoftObjectProperty` 는 경로 기반 soft reference이므로, resolve되지 않은 에셋을 강제로 살려두면 안 된다.

배열과 구조체 안에 object property가 들어갈 수 있으므로, GC는 다음처럼 재귀 순회해야 한다.

1. object의 `UStruct::GetAllProperties()` 를 순회한다.
2. `FObjectPropertyBase` 를 만나면 hard/soft 정책에 맞게 처리한다.
3. `FStructProperty` 를 만나면 `UScriptStruct` 의 자식 프로퍼티로 들어간다.
4. `FArrayProperty` 를 만나면 `Accessor` 로 원소를 돌고 `Inner` 를 재귀 처리한다.

---

## 7. 요약

| 타입 | 추가 메타데이터 | JSON 표현 | 핵심 포인트 |
|---|---|---|---|
| `FEnumProperty` | `UEnum*` | 정수 값 | 이름/값/underlying size를 `UEnum` 이 보관 |
| `FStructProperty` | `UScriptStruct*` | object | 자식 프로퍼티 목록과 `ICppStructOps` 를 함께 사용 |
| `FArrayProperty` | `Inner`, `FArrayAccessor*` | array | 원소 접근은 accessor, 원소 처리는 inner property 담당 |
| `FSceneComponentRefProperty` | 없음 | 경로 문자열 | actor 내부 component 참조용 특수 케이스 |
| `FSoftObjectProperty` | `PropertyClass` | 에셋 경로 문자열 | 로드되지 않은 에셋을 강하게 잡지 않음 |
| `FObjectProperty` | `PropertyClass` | 미정 | hard reference와 GC mark가 핵심 |
| `FClassProperty` | `PropertyClass`, `MetaClass` | class name 예정 | `UClass*` 전용 hard reference |

핵심은 메타데이터를 흩어진 함수 포인터나 raw 배열로 두지 않고 `UEnum`, `UScriptStruct`, `FProperty` 계층으로 모으는 것이다. 특히 `UScriptStruct + ICppStructOps` 조합은 "구조체의 필드 목록을 아는 것" 을 넘어, 타입을 모르는 런타임 코드가 구조체 값을 안전하게 만들고 파괴하고 복사할 수 있게 해 준다.
