#pragma once

#ifndef WITH_LUA
#define WITH_LUA 0
#endif

#if WITH_LUA
#include <sol/sol.hpp>

void RegisterLuaBindings(sol::state& Lua);
#else
inline void RegisterLuaBindings()
{
}
#endif
