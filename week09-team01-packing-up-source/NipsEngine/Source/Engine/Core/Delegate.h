#pragma once

#include <functional>
#include <algorithm>
#include <type_traits>
#include <vector>

bool IsUObjectAlive(const void* Ptr);
class UObject;

#define DECLARE_DELEGATE(Name, ...) using Name = TDelegate<__VA_ARGS__>

template<typename... Args>
class TDelegate
{
public:
	using HandlerType = std::function<void(Args...)>;

	void Add(const HandlerType& handler)
	{
		FDelegateEntry NewEntry;
		NewEntry.Owner = nullptr;
		NewEntry.Callback = handler;
		NewEntry.IsAlive = []() { return true; };
		Handlers.push_back(NewEntry);
	}

	template <typename T>
	void AddDynamic(T* Instance, void (T::* Func)(Args...))
	{
        static_assert(std::is_base_of<UObject, T>::value, "T must be a UObject.");

        const void* ObjPtr = static_cast<const void*>(Instance);
        
		FDelegateEntry NewEntry;
		NewEntry.Owner = Instance;
        NewEntry.Callback = [Instance, Func](Args... args)
        { (Instance->*Func)(args...); };
        NewEntry.IsAlive = [ObjPtr]() { return IsUObjectAlive(ObjPtr); };

        Handlers.push_back(NewEntry);
	}

	template <typename T>
	void RemoveDynamic(T* Instance)
	{
		Handlers.erase(
			std::remove_if(
				Handlers.begin(),
				Handlers.end(),
				[Instance](const FDelegateEntry& Entry)
				{
					return Entry.Owner == Instance;
				}),
			Handlers.end());
	}

	void Broadcast(Args... args)
	{
        auto HandlersCopy = Handlers;
		
		for (auto& Entry : HandlersCopy)
		{
            if (Entry.IsAlive())
                Entry.Callback(args...);
		}

		Handlers.erase(
            std::remove_if(Handlers.begin(), Handlers.end(),
                           [](const FDelegateEntry& E)
                           { return !E.IsAlive(); }),
            Handlers.end()
		);
	}

	void Clear()
    {
        Handlers.clear();
    }

	private:
		struct FDelegateEntry
		{
			void* Owner = nullptr;
            HandlerType Callback;
            std::function<bool()> IsAlive;
		};

		std::vector<FDelegateEntry> Handlers;
};
