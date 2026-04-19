#pragma once

#include "EngineTypes.h"
#include "ObjectFactory.h"
#include "NameTypes.h"

#define DECLARE_OBJECT(cls, parent_cls) \
	public: \
		static const void* GetClass() { static int id = 0; return &id; } \
		virtual bool IsA(const void* id) const override \
		{ \
			 if (GetClass() == id) return true; \
			 return parent_cls::IsA(id); \
		}

#define REGISTER_CLASS(cls) \
	namespace { \
		struct cls##Register \
		{ \
			cls##Register() \
			{ \
				FObjectFactory::RegisterObjectType(cls::GetClass(), []() -> UObject* { return new cls(); }); \
			} \
		}; \
		static cls##Register global_##cls##Register; \
	}

class UObject
{
public:
	UObject();
	virtual ~UObject();

public:
	static const void* GetClass() { static int id = 0; return &id; }
	virtual bool IsA(const void* id) const { return GetClass() == id; }

public:
	uint32 UUID;
	uint32 InternalIndex;

	FName Name;
};

template <typename To, typename From>
To* Cast(From* obj)
{
	if (obj && obj->IsA(To::GetClass()))
	{
		return reinterpret_cast<To*>(obj);
	}
	return nullptr;
}
