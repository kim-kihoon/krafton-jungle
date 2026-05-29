#pragma once
#include "UStruct.h"

class UClass;

class ICppStructOps
{
public:
	virtual ~ICppStructOps() = default;

	virtual void Construct(void* Dest) const			 = 0;
	virtual void Destruct(void* Dest) const				 = 0;
	virtual void Copy(void* Dest, const void* Src) const = 0;
};

template <typename T>
class TCppStructOps final : public ICppStructOps
{
public:
	void Construct(void* Dest) const override
	{
		new (Dest) T();
	}

	void Destruct(void* Dest) const override
	{
		static_cast<T*>(Dest)->~T();
	}

	void Copy(void* Dest, const void* Src) const override
	{
		*static_cast<T*>(Dest) = *static_cast<const T*>(Src);
	}
};

class UScriptStruct : public UStruct
{
public:
	UScriptStruct(
		const char* InName,
		UScriptStruct* InSuperStruct,
		size_t InSize,
		size_t InAlignment,
		const ICppStructOps* InCppStructOps);

	virtual ~UScriptStruct();

	size_t GetAlignment() const { return Alignment; }
	const ICppStructOps* GetCppStructOps() const { return CppStructOps; }
	
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

	static UClass* StaticClass() { return &StaticClassInstance; }
	UClass* GetClass() const override { return StaticClass(); }

public:
	static UClass StaticClassInstance;


private:
	size_t Alignment = 0;
	const ICppStructOps* CppStructOps = nullptr;
};
