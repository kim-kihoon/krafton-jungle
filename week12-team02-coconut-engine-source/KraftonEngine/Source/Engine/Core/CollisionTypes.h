#pragma once
#include "Math/Vector.h"
#include "Core/CoreTypes.h"
#include "Core/Property/FEnumProperty.h"
#include "Core/Property/PropertyTypes.h"
#include "Object/UEnum.h"
#include "Object/ScriptStruct.h"

class AActor;
class UPrimitiveComponent;

enum class ECollisionChannel : uint8
{
	WorldStatic = 0,
	WorldDynamic = 1,
	Pawn = 2,
	Projectile = 3,
	Trigger = 4,
	FootIK = 5,
	// 필요 시 확장
};

inline constexpr int32 NumActiveCollisionChannels = 6;
inline constexpr int32 MaxCollisionChannels = 16;

inline UEnum* StaticEnum_ECollisionChannel()
{
	static UEnum Enum("ECollisionChannel", sizeof(ECollisionChannel));
	static const bool bRegistered = []()
	{
		Enum.AddEnumerator("WorldStatic", static_cast<int64>(ECollisionChannel::WorldStatic));
		Enum.AddEnumerator("WorldDynamic", static_cast<int64>(ECollisionChannel::WorldDynamic));
		Enum.AddEnumerator("Pawn", static_cast<int64>(ECollisionChannel::Pawn));
		Enum.AddEnumerator("Projectile", static_cast<int64>(ECollisionChannel::Projectile));
		Enum.AddEnumerator("Trigger", static_cast<int64>(ECollisionChannel::Trigger));
		Enum.AddEnumerator("FootIK", static_cast<int64>(ECollisionChannel::FootIK));
		return true;
	}();
	(void)bRegistered;
	return &Enum;
}

enum class ECollisionResponse : uint8
{
	Ignore = 0,
	Overlap = 1,
	Block = 2,

	COUNT
};

inline UEnum* StaticEnum_ECollisionResponse()
{
	static UEnum Enum("ECollisionResponse", sizeof(ECollisionResponse));
	static const bool bRegistered = []()
	{
		Enum.AddEnumerator("Ignore", static_cast<int64>(ECollisionResponse::Ignore));
		Enum.AddEnumerator("Overlap", static_cast<int64>(ECollisionResponse::Overlap));
		Enum.AddEnumerator("Block", static_cast<int64>(ECollisionResponse::Block));
		return true;
	}();
	(void)bRegistered;
	return &Enum;
}

enum class ECollisionEnabled : uint8
{
	NoCollision = 0,
	QueryOnly = 1,		// Overlap/Hit 이벤트만
	PhysicsOnly = 2,	// 향후 물리 엔진용
	QueryAndPhysics = 3,

	COUNT
};

inline UEnum* StaticEnum_ECollisionEnabled()
{
	static UEnum Enum("ECollisionEnabled", sizeof(ECollisionEnabled));
	static const bool bRegistered = []()
	{
		Enum.AddEnumerator("NoCollision", static_cast<int64>(ECollisionEnabled::NoCollision));
		Enum.AddEnumerator("QueryOnly", static_cast<int64>(ECollisionEnabled::QueryOnly));
		Enum.AddEnumerator("PhysicsOnly", static_cast<int64>(ECollisionEnabled::PhysicsOnly));
		Enum.AddEnumerator("QueryAndPhysics", static_cast<int64>(ECollisionEnabled::QueryAndPhysics));
		return true;
	}();
	(void)bRegistered;
	return &Enum;
}

// ============================================================
// FCollisionResponseContainer — 채널별 응답 테이블
// Manual UScriptStruct metadata because the active collision-channel fields
// are generated from ECollisionChannel metadata rather than ordinary members.
// ============================================================
struct FCollisionResponseContainer
{
	ECollisionResponse Responses[MaxCollisionChannels];

	FCollisionResponseContainer()
	{
		SetAllChannels(ECollisionResponse::Block);
	}

	explicit FCollisionResponseContainer(ECollisionResponse DefaultResponse)
	{
		SetAllChannels(DefaultResponse);
	}

	void SetAllChannels(ECollisionResponse InResponse)
	{
		for (int32 i = 0; i < MaxCollisionChannels; ++i)
		{
			Responses[i] = InResponse;
		}
	}

	void SetResponse(ECollisionChannel Channel, ECollisionResponse InResponse)
	{
		Responses[static_cast<int32>(Channel)] = InResponse;
	}

	ECollisionResponse GetResponse(ECollisionChannel Channel) const
	{
		return Responses[static_cast<int32>(Channel)];
	}

	static UScriptStruct* StaticStruct()
	{
		static const TCppStructOps<FCollisionResponseContainer> CppStructOps;
		static UScriptStruct Struct(
			"FCollisionResponseContainer",
			nullptr,
			sizeof(FCollisionResponseContainer),
			alignof(FCollisionResponseContainer),
			&CppStructOps);
		static const bool bPropertiesRegistered = []()
		{
			UEnum* ChannelEnum = StaticEnum_ECollisionChannel();
			UEnum* ResponseEnum = StaticEnum_ECollisionResponse();
			for (int32 i = 0; i < NumActiveCollisionChannels; ++i)
			{
				Struct.AddProperty(new FEnumProperty(
					ChannelEnum->GetNameByIndex(static_cast<uint32>(i)),
					"",
					CPF_Edit,
					static_cast<uint32>(offsetof(FCollisionResponseContainer, Responses) + sizeof(ECollisionResponse) * i),
					sizeof(ECollisionResponse),
					ResponseEnum));
			}

			return true;
		}();
		(void)bPropertiesRegistered;

		return &Struct;
	}
};

// ============================================================
// FHitResult — 충돌/레이캐스트 결과
// ============================================================
struct FHitResult
{
	UPrimitiveComponent* HitComponent = nullptr;
	AActor* HitActor = nullptr;

	float Distance = 3.402823466e+38F; // FLT_MAX
	float PenetrationDepth = 0.0f;
	FVector WorldHitLocation = { 0, 0, 0 };
	FVector WorldNormal = { 0, 0, 0 };
	FVector ImpactNormal = { 0, 0, 0 };
	int FaceIndex = -1;

	bool bHit = false;
};

// ============================================================
// FOverlapResult — 오버랩 결과
// ============================================================
struct FOverlapResult
{
	AActor* OverlapActor = nullptr;
	UPrimitiveComponent* OverlapComponent = nullptr;
};

// ============================================================
// FOverlapPair — 프레임 간 오버랩 쌍 추적용
// ============================================================
struct FOverlapPair
{
	UPrimitiveComponent* A = nullptr;
	UPrimitiveComponent* B = nullptr;

	bool operator==(const FOverlapPair& Other) const
	{
		return (A == Other.A && B == Other.B)
			|| (A == Other.B && B == Other.A);
	}
};

// std::unordered_set 호환 해시
namespace std
{
	template<>
	struct hash<FOverlapPair>
	{
		size_t operator()(const FOverlapPair& Pair) const
		{
			// 순서 무관 해시: A와 B를 정렬 후 조합
			auto PtrA = reinterpret_cast<uintptr_t>(Pair.A);
			auto PtrB = reinterpret_cast<uintptr_t>(Pair.B);
			if (PtrA > PtrB) std::swap(PtrA, PtrB);
			size_t H = hash<uintptr_t>()(PtrA);
			H ^= hash<uintptr_t>()(PtrB) + 0x9e3779b9 + (H << 6) + (H >> 2);
			return H;
		}
	};
}
