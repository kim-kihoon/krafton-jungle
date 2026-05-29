#pragma once
#include "UStruct.h"

class UObject;
class FArchive;

enum EClassFlags : uint32
{
	CF_None      = 0,
	CF_Actor     = 1 << 0,
	CF_Component = 1 << 1,
	CF_Camera    = 1 << 2,
	CF_HiddenInComponentList = 1 << 3,
};

enum EClassCastFlags : uint32
{
	CASTCLASS_None						= 0,
	CASTCLASS_UField					= 1 << 0,
	CASTCLASS_UEnum						= 1 << 1,	
	CASTCLASS_UStruct					= 1 << 2,
	CASTCLASS_UScriptStruct             = 1 << 3,
	CASTCLASS_UClass					= 1 << 4,
	CASTCLASS_UFunction					= 1 << 5,
	CASTCLASS_AActor					= 1 << 6,
	CASTCLASS_APawn						= 1 << 7,
	CASTCLASS_APlayerController			= 1 << 8,
	CASTCLASS_USceneComponent			= 1 << 9,
	CASTCLASS_UPrimitiveComponent		= 1 << 10,
	CASTCLASS_UStaticMeshComponent		= 1 << 11,
	CASTCLASS_USkinnedMeshComponent		= 1 << 12,
	CASTCLASS_USkeletalMeshComponent	= 1 << 13,
};

class UClass : public UStruct
{
public:
	UClass(const char* InName, UClass* InSuperClass, size_t InSize,
		uint32 InFlags = CF_None, uint32 InCastFlags = CASTCLASS_None);

	~UClass() = default;

	void	Bind();
	uint32	GetClassFlags() const { return ClassFlags; }
	void	AddClassFlags(uint32 Flags) { ClassFlags |= Flags; }
	uint32	GetClassCastFlags() const { return ClassCastFlags; }
	UClass* GetSuperClass() const { return (UClass*)GetSuperStruct(); }
	void	SerializeBin(FArchive& Ar, void* Data);

	// Not a virtual override, but rather a C++ function overload that utilizes Name Hiding.
	// This is a deliberate decision.
	bool IsChildOf(const UClass* Other);

	bool HasAnyClassFlags(uint32 Flags) const
	{
		return (ClassFlags & Flags) != 0;
	}

	// --- Global class registry ---
	static TArray<UClass*>& GetAllClasses()
	{
		static TArray<UClass*> Registry;
		return Registry;
	}

	// 이름으로 등록된 클래스 룩업. 못 찾으면 nullptr.
	// 직렬화/설정 파일에서 클래스를 string으로 지정할 때 사용.
	static UClass* FindByName(const char* InName)
	{
		if (!InName) return nullptr;
		for (UClass* C : GetAllClasses())
		{
			if (C && C->GetName() && std::strcmp(C->GetName(), InName) == 0)
				return C;
		}
		return nullptr;
	}

	static UClass* StaticClass() { return &StaticClassInstance; }
	UClass* GetClass() const override { return StaticClass(); }

public:
	static UClass StaticClassInstance;

private:
	bool   bBound		  = false;
	uint32 ClassFlags     = CF_None;
	uint32 OwnClassCastFlags = CASTCLASS_None;
	uint32 ClassCastFlags = CASTCLASS_None;
};
