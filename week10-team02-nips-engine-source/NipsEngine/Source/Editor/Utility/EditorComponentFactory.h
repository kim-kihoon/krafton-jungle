#pragma once

#include "Core/CoreMinimal.h"
#include "Engine/GameFramework/AActor.h"
#include <functional>

class UActorComponent;

struct FComponentMenuEntry
{
    const char* DisplayName;
    const char* Category;
    std::function<UActorComponent*(AActor*)> Register;
};

// 컴포넌트 등록을 전담하는 헬퍼 구조체, EditorPropertyWidget에 저장된 배열을 던져주는 역할만 합니다.
struct FEditorComponentFactory
{
    template<typename ComponentType>
    static UActorComponent* RegisterComp(AActor* Actor);

    template<typename LightType>
    static UActorComponent* RegisterLightComp(AActor* Actor);

    static const TArray<FComponentMenuEntry>& GetMenuRegistry();
};