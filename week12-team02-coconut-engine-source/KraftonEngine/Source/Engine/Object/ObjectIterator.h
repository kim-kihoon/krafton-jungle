#pragma once

#include "Object/Object.h"
#include "Object/UClass.h"

// 특정 UObject 타입 순회
template<typename TObject>
class TObjectIterator
{
public:
	TObjectIterator()
		: CurrentIndex(0)
	{
		AdvanceToNextValidObject();
	}

	// 다음 조건에 맞는 객체가 나올 때까지 계속 스킵
	TObjectIterator& operator++()
	{
		++CurrentIndex;
		AdvanceToNextValidObject();
		return *this;
	}

	explicit operator bool() const
	{
		return CurrentIndex < GUObjectArray.Num();
	}

	TObject* operator*() const
	{
		return static_cast<TObject*>(GUObjectArray.IndexToObject(CurrentIndex));
	}

private:
	void AdvanceToNextValidObject()
	{
		while (CurrentIndex < GUObjectArray.Num())
		{
			UObject* Obj = GUObjectArray.IndexToObject(CurrentIndex);
			if (Obj && Obj->IsA<TObject>())
			{
				return;
			}
			++CurrentIndex;
		}
	}

	int32 CurrentIndex;
};

// 타입이 런타임에 결정되는 경우
class FObjectIterator
{
public:
	FObjectIterator(UClass* InType = nullptr)
		: CurrentIndex(0), FilterType(InType)
	{
		AdvanceToNextValidObject();
	}

	FObjectIterator& operator++()
	{
		++CurrentIndex;
		AdvanceToNextValidObject();
		return *this;
	}

	explicit operator bool() const
	{
		return CurrentIndex < GUObjectArray.Num();
	}

	UObject* operator*() const
	{
		return GUObjectArray.IndexToObject(CurrentIndex);
	}

private:
	void AdvanceToNextValidObject()
	{
		while (CurrentIndex < GUObjectArray.Num())
		{
			UObject* Obj = GUObjectArray.IndexToObject(CurrentIndex);
			if (Obj != nullptr)
			{
				if (FilterType == nullptr || Obj->GetClass()->IsChildOf(FilterType))
				{
					return;
				}
			}
			++CurrentIndex;
		}
	}

	int32 CurrentIndex;
	UClass* FilterType;
};
