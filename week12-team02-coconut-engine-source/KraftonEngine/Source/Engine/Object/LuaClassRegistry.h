#pragma once

#include "Core/CoreTypes.h"

namespace sol { class state; }

struct FLuaClassRegistrar
{
	using FRegisterFn = void(*)(sol::state&);

	explicit FLuaClassRegistrar(FRegisterFn Fn)
	{
		GetAll().push_back(Fn);
	}

	static TArray<FRegisterFn>& GetAll()
	{
		static TArray<FRegisterFn> Registry;
		return Registry;
	}
};