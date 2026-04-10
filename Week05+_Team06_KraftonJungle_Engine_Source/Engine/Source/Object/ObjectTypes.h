#pragma once
#include "Types/CoreTypes.h"
#include "CoreMinimal.h"

enum class EObjectFlags : uint32
{
    None = 0,
    Public = 1 << 0,
    Transient = 1 << 1,
    Standalone = 1 << 2,
    RootSet = 1 << 3,
    PendingKill = 1 << 4,
};

inline EObjectFlags operator|(EObjectFlags A, EObjectFlags B)
{
    return static_cast<EObjectFlags>(
        static_cast<uint32>(A) | static_cast<uint32>(B));
}

inline EObjectFlags operator&(EObjectFlags A, EObjectFlags B)
{
    return static_cast<EObjectFlags>(
        static_cast<uint32>(A) & static_cast<uint32>(B));
}

inline EObjectFlags& operator|=(EObjectFlags& A, EObjectFlags B)
{
    A = A | B;
    return A;
}

inline EObjectFlags& operator&=(EObjectFlags& A, EObjectFlags B)
{
    A = A & B;
    return A;
}

inline EObjectFlags operator~(EObjectFlags A)
{
    return static_cast<EObjectFlags>(~static_cast<uint32>(A));
}

class UObject;

// 복제 작업 상태와 원본, 복사본 매핑 정보를 들고 다니는 컨텍스트
struct ENGINE_API FDuplicateionContext
{
	TMap<UObject*, UObject*> DuplicatedObjects;

	UObject* GetMappedObject(UObject* Source) const
	{
		if (!Source) return nullptr;
		auto It = DuplicatedObjects.find(Source);
		return (It != DuplicatedObjects.end()) ? It->second : Source;
	}
};