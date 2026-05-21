#pragma once

#include "Core/CoreMinimal.h"

/*
Reflection규칙

	UCLASS()는 class 바로 위
    UPROPERTY(...) 는 property 바로 위
    property 선언은 첫 버전에서 한 줄
	#if 안의 property는 무시
    template property는 아직 안 함
*/

// These macros wrap metadata parsed by the Unreal Header Tool, and are otherwise
// ignored when code containing them is compiled by the C++ compiler
#define UCLASS()
#define UPROPERTY(...)
#define UFUNCTION(...)
#define USTRUCT(...)
#define UMETA(...)
#define UPARAM(...)
#define UENUM(...)
#define UDELEGATE(...)
#define RIGVM_METHOD(...)
#define VMODULE(...)

#define GENERATED_BODY(ClassName, SuperClass) \
public:                                       \
    using ThisClass = ClassName;              \
    using Super = SuperClass;                 \
    static const FTypeInfo s_TypeInfo;        \
    const FTypeInfo* GetTypeInfo() const override { return &s_TypeInfo; } \
    static UClass* StaticClass();             \
    UClass* GetClass() const override         \
    {                                         \
        return StaticClass();                 \
    }
