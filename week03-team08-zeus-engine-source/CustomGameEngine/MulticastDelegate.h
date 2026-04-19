#pragma once
#include "EngineTypes.h"
#include <functional>

#define DECLARE_MULTICAST_DELEGATE(DelegateName, ...) \
	class DelegateName : public TMulticastDelegate<__VA_ARGS__> {};

template <typename... Args>
class TMulticastDelegate
{
public:
	using Functor = std::function<void(Args...)>;

	template <typename T>
	void AddRaw(T* obj, void(T::*func)(Args...))
	{
		Handlers.push_back({ (void*)obj, [obj, func](Args... params) { (obj->*func)(params...); } });
	}

	void RemoveRaw(void* obj)
	{
		if (obj == nullptr) return;

		Handlers.erase(
			std::remove_if(Handlers.begin(), Handlers.end(),
				[obj](const FHandlerNode& node) { return node.ObjectPtr == obj; }),
			Handlers.end());
	}

	void Broadcast(Args... params)
	{
		for (auto& node : Handlers)
		{
			if (node.Func) node.Func(params...);
		}
	}

private:
	struct FHandlerNode
	{
		void* ObjectPtr;
		Functor Func;
	};
	TArray<FHandlerNode> Handlers;
};