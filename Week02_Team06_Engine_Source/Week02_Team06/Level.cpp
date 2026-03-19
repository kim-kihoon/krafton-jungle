#include "pch.h"
#include "Level.h"

UWorld* ULevel::GetWorld()
{
	return OwningWorld;
}

void ULevel::LoadLevel(UResourceManager& ResourceManager)
{

}

void ULevel::Release()
{
	// Actor들의 수명은 GUObjectArray(ObjectFactory)가 관리
	// Level은 참조 목록만 비운다
	Actors.Clear();
	OwningWorld = nullptr;
}
