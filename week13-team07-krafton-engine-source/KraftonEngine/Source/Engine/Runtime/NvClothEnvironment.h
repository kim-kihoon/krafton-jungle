#pragma once

namespace nv { namespace cloth { class Factory; } }

class FNvClothEnvironment
{
public:
	static bool Initialize();
	static void Shutdown();
	static bool IsInitialized();
	static nv::cloth::Factory* GetFactory();
};