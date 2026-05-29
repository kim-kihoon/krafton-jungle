# Property Reflection System

KraftonEngine의 프로퍼티 리플렉션 시스템은 C++ 멤버 변수에 대한 메타데이터를 빌드 타임에 생성하고, 런타임에서 에디터 UI, JSON 저장/로드, 타입 검사, 클래스 목록, 구조체/enum 메타 객체가 같은 정보 위에서 동작하게 만드는 기반이다.

핵심 아이디어는 단순하다.

> C++ 멤버 변수는 값일 뿐이고, 그 값을 어떻게 찾고 편집하고 저장할지는 `FProperty` 디스크립터가 안다.

이 문서는 현재 코드 기준의 구조를 설명한다. `UClass`, `UStruct`, `UScriptStruct`, `UEnum`, `FProperty`, codegen은 실제로 동작하는 축이고, `FField`, CDO, hard object property, 자동 `FArchive` property serialization은 아직 다음 단계로 남아 있다.

---

## 1. 전체 흐름

개발자는 헤더에 빈 매크로 마커를 쓴다.

```cpp
#include "ProjectileMovementComponent.generated.h"

UCLASS(Component)
class UProjectileMovementComponent : public UMovementComponent
{
    GENERATED_BODY(UProjectileMovementComponent)

    UPROPERTY(Edit, Category="Projectile")
    FVector Velocity;

    UPROPERTY(Edit, Category="Projectile", Min=0, Speed=1.0f)
    float InitialSpeed = 1000.0f;
};
```

`Scripts/GenerateCode.py` 는 `UCLASS`, `USTRUCT`, `UENUM`, `UPROPERTY`, `UFUNCTION`, `UPROPERTY_HIDE` 를 찾고 다음 두 종류의 파일을 만든다.

- `Intermediate/Generated/Inc/<Header>.generated.h`
- `Intermediate/Generated/Source/<Header>.gen.cpp`

생성된 코드는 정적 초기화 시점에 클래스, enum, struct, property 메타데이터를 등록한다. 이후 런타임 코드는 `UObject*` 하나만 있어도 그 객체가 가진 편집 가능 프로퍼티 목록을 얻을 수 있다.

```cpp
TArray<const FProperty*> Props;
SomeObject->GetEditableProperties(Props);

for (const FProperty* Prop : Props)
{
    json::JSON Value = Prop->Serialize(SomeObject);
}
```

이 흐름에서 실제 값은 `SomeObject` 안에 있고, `FProperty` 는 이름, category, flags, offset, type-specific metadata만 가진다.

---

## 2. 왜 필요한가

리플렉션이 없으면 새 멤버 하나를 추가할 때마다 여러 시스템을 수동으로 고쳐야 한다.

- 에디터 property panel에 새 widget 추가
- scene save/load에 새 field 추가
- duplicate/archive 경로에 새 field 추가
- enum/struct/array처럼 중첩 타입이면 별도 순회 코드 추가
- 클래스 이름으로 object를 만들거나 필터링하기 위한 registry 코드 추가

이 시스템은 그 반복을 codegen과 공통 메타데이터로 옮긴다.

현재 가장 직접적으로 이득을 보는 곳은 다음이다.

### 에디터 UI

`EditorPropertyWidget` 은 선택된 actor/component의 `GetEditableProperties()` 결과를 순회한다. 각 `FProperty` 의 `EPropertyType` 에 따라 체크박스, slider, vector editor, combo, asset picker, array editor, struct tree를 그린다.

즉, `UPROPERTY(Edit)` 가 붙으면 별도 UI 코드를 쓰지 않아도 property panel에 나타난다.

### JSON scene 저장/로드

`SceneSaveManager` 는 `GetNonTransientProperties()` 로 저장 대상 property를 모으고 `FProperty::Serialize()` / `Deserialize()` 를 호출한다. `CPF_Transient` 로 표시된 property는 제외된다.

로드 후에는 `PostEditProperty(PropertyName)` 를 호출해 mesh 변경 후 material slot 재구성 같은 후처리를 수행한다. 일부 객체는 property 목록이 후처리 과정에서 바뀔 수 있어 2-pass 로드 흐름을 가진다.

### 클래스 카탈로그와 factory

`UClass::GetAllClasses()` 는 정적 초기화로 등록된 모든 class metadata를 가진다. 에디터의 component 추가 메뉴, class picker, scene load 시 class name으로 object 생성하는 경로가 이 registry를 사용한다.

`REGISTER_FACTORY(Type)` 는 `FObjectFactory` 에 class name -> object 생성 lambda를 등록한다.

### enum/struct metadata

`UEnum` 과 `UScriptStruct` 가 정식 metadata object로 들어오면서, enum 이름/값 테이블과 struct child property 목록이 `UObject -> UField` 계층 안에 모인다.

---

## 3. 마커 매크로

`ObjectMacros.h` 의 매크로들은 C++ 컴파일러 입장에서는 대부분 비어 있다.

```cpp
#define UCLASS(...)
#define USTRUCT(...)
#define UENUM(...)
#define UPROPERTY(...)
#define UFUNCTION(...)
#define UPROPERTY_HIDE(...)
#define GENERATED_BODY(ClassName) KE_GENERATED_BODY_##ClassName()
```

이 매크로들은 Python header tool이 읽기 위한 표식이다. 실제 C++ 코드는 `.generated.h` 에 생성되는 `KE_GENERATED_BODY_<Name>()` 매크로가 보강한다.

클래스용 generated body는 대략 다음을 선언한다.

```cpp
#define KE_GENERATED_BODY_UFoo() \
    using Super = UBar; \
    static UClass StaticClassInstance; \
    static UClass* StaticClass() { return &StaticClassInstance; } \
    UClass* GetClass() const override { return StaticClass(); } \
    friend struct UFoo_PropertyRegistrar;
```

구조체용 generated body는 `StaticStruct()` 를 제공한다.

```cpp
#define KE_GENERATED_BODY_FTransform() \
    static class UScriptStruct StaticStructInstance; \
    static class UScriptStruct* StaticStruct() { return &StaticStructInstance; }
```

enum은 generated header에 `StaticEnum_*()` forward declaration을 낸다.

---

## 4. Codegen 파이프라인

`Scripts/GenerateCode.py` 는 네 단계로 움직인다.

### 4-1. 헤더 발견

`SOURCE_ROOTS` 아래의 `.h` 파일을 훑고, 다음 marker 중 하나가 있는 파일만 대상으로 삼는다.

- `UCLASS(`
- `USTRUCT(`
- `UENUM(`

### 4-2. 파싱과 registry 구축

정규식 기반 parser가 다음 모델을 만든다.

- `ClassInfo`: class name, parent, flags, properties, functions, hidden properties, factory 여부
- `StructInfo`: struct name, properties
- `EnumInfo`: enum name, entries, underlying type, enum class 여부
- `PropertyInfo`: member name, C++ type, `EPropertyType`, category, display name, flags, metadata
- `FunctionInfo`: function name, Lua binding 여부

enum과 struct는 먼저 registry를 만든 뒤 property type 분류에 사용한다. 그래서 `FTransform` 처럼 `USTRUCT()` 로 알려진 타입은 `EPropertyType::Struct` 로 분류된다.

### 4-3. 타입 분류

기본 타입은 `TYPE_MAP` 으로 분류된다.

```python
"bool"    -> EPropertyType::Bool
"int32"   -> EPropertyType::Int
"float"   -> EPropertyType::Float
"FVector" -> EPropertyType::Vec3
"FName"   -> EPropertyType::Name
```

특수 타입은 별도 규칙을 탄다.

- `TArray<T>` -> `EPropertyType::Array`, inner type은 `T`
- 알려진 `UENUM` -> `EPropertyType::Enum`
- 알려진 `USTRUCT` -> `EPropertyType::Struct`
- `USceneComponent*` -> 현재는 `EPropertyType::SceneComponentRef`
- `TSoftObjectPtr<T>` 계열은 `Class=` metadata와 함께 `FSoftObjectProperty` 로 처리

아직 일반 `UObject*`, `AActor*`, `UClass*` hard reference는 정식 구현 전이다.

### 4-4. generated header emit

`.generated.h` 는 C++ class/struct 안에서 확장될 macro만 제공한다.

이 방식은 Unreal처럼 `GENERATED_BODY()` 를 소스 안에 남겨두되, 실제 내용은 빌드 전 codegen 결과로 채우는 구조다.

### 4-5. gen.cpp emit

`.gen.cpp` 는 실제 metadata object와 registrar를 만든다.

클래스는 정적 `UClass` 인스턴스를 가진다.

```cpp
UClass UProjectileMovementComponent::StaticClassInstance(
    "UProjectileMovementComponent",
    &UMovementComponent::StaticClassInstance,
    sizeof(UProjectileMovementComponent),
    CF_Component);
```

기본적으로 factory에도 등록된다.

```cpp
REGISTER_FACTORY(UProjectileMovementComponent)
```

`UPROPERTY` 들은 registrar struct의 정적 인스턴스를 통해 등록된다.

```cpp
struct UProjectileMovementComponent_PropertyRegistrar
{
    UProjectileMovementComponent_PropertyRegistrar()
    {
        using ThisClass = UProjectileMovementComponent;
        UClass* Cls = UProjectileMovementComponent::StaticClass();

        Cls->AddProperty(new FVec3Property(
            "Velocity", "Projectile", CPF_Edit,
            static_cast<uint32>(offsetof(ThisClass, Velocity)),
            static_cast<uint32>(sizeof(((ThisClass*)0)->Velocity))));
    }
};

static UProjectileMovementComponent_PropertyRegistrar
    s_UProjectileMovementComponent_PropertyReg;
```

구조체는 `TCppStructOps<T>` 와 `UScriptStruct` 를 생성하고, struct child property도 같은 방식으로 등록한다.

```cpp
static const TCppStructOps<FTransform> GFTransformCppStructOps;

UScriptStruct FTransform::StaticStructInstance(
    "FTransform", nullptr,
    sizeof(FTransform), alignof(FTransform),
    &GFTransformCppStructOps);
```

enum은 `UEnum` 정적 인스턴스와 entry table을 생성한다.

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
        return true;
    }();
    (void)bRegistered;
    return &Enum;
}
```

---

## 5. 런타임 메타 객체

### `UObject`

모든 reflected runtime object의 base다. `UObject` 는 UUID, object name, outer, internal index를 갖고 `GetClass()` 를 통해 `UClass` metadata에 접근한다.

```cpp
virtual UClass* GetClass() const { return StaticClass(); }
virtual void GetEditableProperties(TArray<const FProperty*>& OutProps);
virtual void GetNonTransientProperties(TArray<const FProperty*>& OutProps);
```

`Cast<T>` / `IsA<T>` 는 `UClass::IsChildOf()` 를 사용한다.

### `UField`

`UField` 는 이름 있는 metadata object의 base다. 현재 `UClass`, `UStruct`, `UScriptStruct`, `UEnum` 이 이 계층에 있다.

정적 metadata object들은 C++ static initialization 시점에 생성되므로, `GUObjectArray` 가 준비되기 전에 생길 수 있다. 그래서 `UField` 계열은 생성 시 즉시 등록하지 않고 `FUObjectArray::DeferStaticObject()` 로 미뤘다가 engine loop에서 flush한다.

### `UStruct`

`UStruct` 는 property container다. `UClass` 와 `UScriptStruct` 가 모두 상속한다.

```cpp
class UStruct : public UField
{
public:
    const TArray<FProperty*>& GetProperties() const;
    void AddProperty(FProperty* InProperty);

    void GetAllProperties(TArray<const FProperty*>& OutProps) const;
    void GetEditableProperties(TArray<const FProperty*>& OutProps) const;
    void GetNonTransientProperties(TArray<const FProperty*>& OutProps) const;
    const FProperty* FindPropertyByName(const char* InName) const;
};
```

`GetAllProperties()` 는 base-to-derived 순서로 부모의 property를 먼저 모은 뒤 자기 property를 붙인다. `FindPropertyByName()` 은 반대로 자기 class에서 먼저 찾고 없으면 parent로 올라간다.

`UPROPERTY_HIDE("Name")` 은 inherited property를 editor와 serialization 대상에서 숨기기 위해 `HiddenProperties` 에 이름을 기록한다.

### `UClass`

`UClass` 는 `UStruct` 에 class flags와 cast flags를 더한다.

```cpp
enum EClassFlags
{
    CF_None,
    CF_Actor,
    CF_Component,
    CF_Camera,
    CF_HiddenInComponentList,
};
```

`UClass::GetAllClasses()` 는 모든 class metadata registry다. `FindByName()` 은 scene load나 settings load에서 class string을 다시 `UClass*` 로 바꿀 때 사용한다.

`EClassCastFlags` 는 빠른 `IsA` 경로를 위한 bitmask다. root class에 고유 bit를 부여하고, `UClass::Bind()` 가 parent bits를 OR 해서 descendant에도 inherited cast bits가 적용되게 한다.

### `UEnum`

`UEnum` 은 enum entry 이름과 실제 값을 보관한다.

```cpp
void AddEnumerator(const char* InName, int64 InValue);
FString GetNameByValue(int64 Value) const;
int64 GetValueByName(FString Name) const;
uint32 GetUnderlyingSize() const;
```

이 구조 덕분에 sparse enum value와 custom underlying type을 다룰 수 있다.

### `UScriptStruct`

`UScriptStruct` 는 `UStruct` 에 C++ struct lifecycle 정보를 더한다.

```cpp
class ICppStructOps
{
public:
    virtual void Construct(void* Dest) const = 0;
    virtual void Destruct(void* Dest) const = 0;
    virtual void Copy(void* Dest, const void* Src) const = 0;
};
```

codegen은 `TCppStructOps<T>` 정적 인스턴스를 만들고 `UScriptStruct` 에 넘긴다. 따라서 타입을 모르는 코드도 `UScriptStruct` 만 가지고 구조체 값을 생성/파괴/복사할 수 있다.

자세한 내용은 [`SpecialPropertyTypes.md`](SpecialPropertyTypes.md)의 `FStructProperty` 섹션에 있다.

---

## 6. `FProperty` 계층

`FProperty` 는 순수 schema다. 인스턴스 값을 들고 있지 않고, container pointer와 offset을 합쳐 값 주소를 계산한다.

```cpp
class FProperty
{
public:
    FString Name;
    FString Category;
    uint32 PropertyFlag = CPF_None;
    uint32 ElementSize = 0;
    uint32 Offset_Internal = 0;

    virtual EPropertyType GetType() const = 0;
    virtual json::JSON Serialize(const void* Instance) const = 0;
    virtual void Deserialize(void* Instance, const json::JSON& Value) const = 0;

    void* ContainerPtrToValuePtr(void* Container) const
    {
        return static_cast<char*>(Container) + Offset_Internal;
    }
};
```

`Instance` 는 property를 소유한 object일 수도 있고, struct property 내부를 순회할 때는 struct instance pointer일 수도 있으며, array inner property를 처리할 때는 element pointer일 수도 있다.

현재 주요 property type은 다음과 같다.

| 타입 | 클래스 | 역할 |
|---|---|---|
| bool | `FBoolProperty`, `FByteBoolProperty` | `bool` 과 byte bool 분리 |
| number | `FIntProperty`, `FFloatProperty` | `Min`, `Max`, `Speed` metadata |
| vector/color | `FVec3Property`, `FVec4Property`, `FRotatorProperty`, `FColor4Property` | float 배열 형태 JSON |
| string/name | `FStringProperty`, `FNameProperty`, `FScriptProperty` | 문자열 계열 |
| scene component ref | `FSceneComponentRefProperty` | 같은 actor 안 component path |
| material slot | `FMaterialSlotProperty` | material path object |
| enum | `FEnumProperty` | `UEnum*` 으로 이름/값/크기 해석 |
| struct | `FStructProperty` | `UScriptStruct*` child properties 재귀 |
| array | `FArrayProperty` | `Inner` property + `FArrayAccessor` |
| soft object | `FSoftObjectProperty` | asset path + `PropertyClass` |
| hard object/class | `FObjectProperty`, `FClassProperty` | 아직 빈 스텁에 가까움 |

복합 타입은 모두 재귀 구조다.

- `FStructProperty` 는 `UScriptStruct::GetProperties()` 를 순회한다.
- `FArrayProperty` 는 `Accessor->Num()` / `GetAt()` 로 element를 얻고 `Inner` 에 처리를 위임한다.
- `FEnumProperty` 는 raw value를 읽되 enum metadata는 `UEnum` 에서 얻는다.

---

## 7. Property flags

현재 flag는 다음처럼 정의되어 있다.

```cpp
enum EPropertyFlags : uint32
{
    CPF_None                     = 0,
    CPF_Edit                     = 1 << 1,
    CPF_FixedSize                = 1 << 2,
    CPF_Transient                = 1 << 3,
    CPF_DuplicateTransient       = 1 << 4,
    CPF_NonPIEDuplicateTransient = 1 << 5,
    CPF_Config                   = 1 << 6,
};
```

현재 확실히 적용되는 대표 경로는 다음이다.

- `CPF_Edit`: editor property panel 노출 대상
- `CPF_FixedSize`: array editor와 array deserialize에서 길이 변경 제한
- `CPF_Transient`: `GetNonTransientProperties()` 기반 JSON save/load 제외
- `CPF_DuplicateTransient`, `CPF_NonPIEDuplicateTransient`: flag vocabulary는 있지만 현재 manual `Serialize(FArchive&)` duplicate 경로에서는 아직 정책 적용 전
- `CPF_Config`: flag는 있지만 config serialization 정책은 별도 확장 지점

`DuplicateTransient` 류를 제대로 적용하려면 generated/reflected `FArchive` serialization 또는 property-aware duplicate archive가 필요하다.

---

## 8. Serialization 경로

이 엔진에는 현재 두 serialization 축이 공존한다.

### JSON property serialization

`FProperty::Serialize(const void*)` / `Deserialize(void*, const json::JSON&)` 경로다. Scene save/load와 editor-facing data에 쓰인다.

이 경로는 property flags, struct recursion, array recursion, enum metadata, soft object path를 비교적 잘 활용한다.

### `FArchive` binary serialization

`UObject::Serialize(FArchive& Ar)` virtual 함수와 `Ar << Value` operator 기반 경로다. `Duplicate()` 는 현재 `FMemoryArchive` 로 이 경로를 왕복한다.

```cpp
UObject* UObject::Duplicate(UObject* NewOuter) const
{
    UObject* Dup = FObjectFactory::Get().Create(GetClass()->GetName(), EffectiveOuter);

    FMemoryArchive Writer(true);
    const_cast<UObject*>(this)->Serialize(Writer);

    FMemoryArchive Reader(Writer.GetBuffer(), false);
    Dup->Serialize(Reader);

    Dup->PostDuplicate();
    return Dup;
}
```

중요한 점은 현재 대부분의 `Serialize(FArchive&)` 가 수동 구현이라는 것이다.

```cpp
void UProjectileMovementComponent::Serialize(FArchive& Ar)
{
    UMovementComponent::Serialize(Ar);
    Ar << Velocity;
    Ar << InitialSpeed;
    Ar << MaxSpeed;
}
```

따라서 `FArchive` duplicate는 아직 `FProperty` flags를 자동으로 보지 않는다. `CPF_DuplicateTransient` 같은 정책은 generated archive serialization을 도입한 뒤에야 자연스럽게 적용할 수 있다.

권장되는 다음 단계는 `SerializeGeneratedProperties(FArchive& Ar)` 를 codegen으로 만들고, 수동 `Serialize` 는 필요한 custom payload만 남기는 방식이다.

---

## 9. PostEditProperty

`PostEditProperty(const char* PropertyName)` 는 editor에서 값이 바뀐 직후, 그리고 scene load 후 호출된다.

현재 이벤트 객체가 아니라 property display/name 문자열만 전달한다. 단순하지만 대부분의 component 후처리에는 충분하다.

예를 들어 static mesh가 바뀌면 mesh section 개수에 맞춰 material slot 배열을 재구성하고, primitive collision property가 바뀌면 physics body를 다시 만든다.

향후 structured event가 필요해지면 다음 정보가 들어갈 수 있다.

- `const FProperty*`
- property path
- array index
- edit source: editor, load, duplicate, script
- interactive change 여부

다만 지금은 string 기반 API가 이미 넓게 쓰이고 있으므로, 큰 migration보다 compatibility wrapper가 안전하다.

---

## 10. 생성 결과 예시

사용자 코드:

```cpp
UCLASS(Component)
class UHeightFogComponent : public USceneComponent
{
    GENERATED_BODY(UHeightFogComponent)

    UPROPERTY(Edit, Category="Fog", Min=0, Speed=0.01f)
    float Density = 0.02f;
};
```

생성되는 class metadata:

```cpp
UClass UHeightFogComponent::StaticClassInstance(
    "UHeightFogComponent",
    &USceneComponent::StaticClassInstance,
    sizeof(UHeightFogComponent),
    CF_Component);

REGISTER_FACTORY(UHeightFogComponent)
```

생성되는 property registration:

```cpp
struct UHeightFogComponent_PropertyRegistrar
{
    UHeightFogComponent_PropertyRegistrar()
    {
        using ThisClass = UHeightFogComponent;
        UClass* Cls = UHeightFogComponent::StaticClass();

        Cls->AddProperty(new FFloatProperty(
            "Density", "Fog", CPF_Edit,
            static_cast<uint32>(offsetof(ThisClass, Density)),
            static_cast<uint32>(sizeof(((ThisClass*)0)->Density)),
            0.0f, 0.0f, 0.01f));
    }
};
```

런타임에서 editor는 `CPF_Edit` 를 보고 노출하고, scene save는 `CPF_Transient` 가 없으므로 저장하고, JSON deserialize는 offset으로 `Density` 주소를 찾아 값을 쓴다.

---

## 11. 현재 한계와 다음 단계

### 이미 있는 기반

- `UClass` / `UStruct` / `UField` metadata hierarchy
- `UEnum` metadata object
- `UScriptStruct` + `ICppStructOps`
- `FProperty` subclass hierarchy
- `TArray<T>` type-erased accessor
- JSON save/load property recursion
- editor property panel
- class registry와 factory
- Lua용 `UFUNCTION(Lua)` binding generation

### 아직 없는 것

- `FField` / `FFieldVariant` 기반 lightweight field ownership
- `UClass::ClassDefaultObject` / CDO
- `FObjectProperty` / `FClassProperty` hard reference 구현
- GC mark/sweep 본체와 recursive reference collector
- generated `FArchive` property serialization
- property path 기반 structured `PostEditProperty`
- tagged binary property archive
- nested template parser의 완전한 C++ parsing

### 추천 작업 순서

1. generated `SerializeGeneratedProperties(FArchive& Ar)` helper 도입
2. `FObjectProperty` / `FClassProperty` 구현
3. recursive reference collection과 basic GC
4. CDO 또는 structured property event
5. `FField` linked list / `TFieldIterator` 스타일 migration

순서는 프로젝트 목표에 따라 바뀔 수 있지만, hard reference와 GC는 object lifetime correctness와 직접 연결되므로 가장 높은 우선순위다.

---

## 12. 주요 파일

| 파일 | 역할 |
|---|---|
| [`Scripts/GenerateCode.py`](../Scripts/GenerateCode.py) | header scan, parse, generated output |
| [`ObjectMacros.h`](../KraftonEngine/Source/Engine/Object/ObjectMacros.h) | 빈 marker macro와 `GENERATED_BODY` |
| [`Object.h`](../KraftonEngine/Source/Engine/Object/Object.h) | `UObject`, RTTI helpers, duplicate entry |
| [`FUObjectArray.h`](../KraftonEngine/Source/Engine/Object/FUObjectArray.h) | object creation, static metadata defer/flush |
| [`UField.h`](../KraftonEngine/Source/Engine/Object/UField.h) | named metadata object base |
| [`UStruct.h`](../KraftonEngine/Source/Engine/Object/UStruct.h) | property container, inherited property traversal |
| [`UClass.h`](../KraftonEngine/Source/Engine/Object/UClass.h) | class metadata, flags, cast flags, registry |
| [`UEnum.h`](../KraftonEngine/Source/Engine/Object/UEnum.h) | enum metadata |
| [`ScriptStruct.h`](../KraftonEngine/Source/Engine/Object/ScriptStruct.h) | `UScriptStruct`, `ICppStructOps` |
| [`PropertyTypes.h`](../KraftonEngine/Source/Engine/Core/Property/PropertyTypes.h) | `FProperty` base and simple property types |
| [`FArrayProperty.h`](../KraftonEngine/Source/Engine/Core/Property/FArrayProperty.h) | array descriptor |
| [`FStructProperty.h`](../KraftonEngine/Source/Engine/Core/Property/FStructProperty.h) | struct descriptor |
| [`FEnumProperty.h`](../KraftonEngine/Source/Engine/Core/Property/FEnumProperty.h) | enum descriptor |
| [`FObjectPropertyBase/`](../KraftonEngine/Source/Engine/Core/Property/FObjectPropertyBase/) | soft/hard object property family |
| [`EditorPropertyWidget.cpp`](../KraftonEngine/Source/Editor/UI/EditorPropertyWidget.cpp) | reflected editor property UI |
| [`SceneSaveManager.cpp`](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp) | reflected scene save/load |

---

## 13. 한 문장 요약

`UPROPERTY` 한 줄은 codegen을 거쳐 `UClass` / `UScriptStruct` 안의 `FProperty` 디스크립터가 되고, 그 디스크립터가 editor, JSON serialization, type metadata, class registry가 같은 방식으로 값을 찾고 해석하게 만든다.
