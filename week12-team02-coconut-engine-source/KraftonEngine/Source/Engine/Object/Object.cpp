#include "Object.h"
#include "UUIDGenerator.h"
#include "Serialization/Archive.h"
#include "Serialization/MemoryArchive.h"
#include "Object/ObjectFactory.h"
#include "UClass.h"

UObject::UObject()
{
	UUID = UUIDGenerator::GenUUID();
}

UObject::~UObject()
{
	GUObjectArray.RemoveObject(this);
}

UObject* UObject::Duplicate(UObject* NewOuter) const
{
	// FObjectFactory 기반 같은 타입 인스턴스 생성 → Serialize 왕복 → PostDuplicate.
	// UUID/Name은 생성자에서 새로 발급되며, Serialize에서 덮어쓰지 않는 것이 규칙이다.
	// NewOuter가 nullptr이면 원본의 Outer를 그대로 승계.
	UObject* EffectiveOuter = NewOuter ? NewOuter : Outer;
	UObject* Dup = FObjectFactory::Get().Create(GetClass()->GetName(), EffectiveOuter);
	if (!Dup)
	{
		return nullptr;
	}

	FMemoryArchive Writer(/*bIsSaving=*/true);
	Writer.SetIsDuplicating(true);
	const_cast<UObject*>(this)->Serialize(Writer);

	FMemoryArchive Reader(Writer.GetBuffer(), /*bIsSaving=*/false);
	Reader.SetIsDuplicating(true);
	Dup->Serialize(Reader);

	Dup->PostDuplicate();
	return Dup;
}

bool UObject::IsA(const UClass* Other) const 
{ 
	return Other && GetClass()->IsChildOf(Other);
}

void UObject::Serialize(FArchive& Ar)
{
	// UUID/InternalIndex/Outer는 직렬화 금지 (복제 시 새로 발급/Outer는 호출자가 지정).
	// ObjectName은 복제 컨텍스트에서도 제외 — Factory가 새 인스턴스에 유니크 이름을 부여하므로
	// 원본 이름으로 덮어쓰면 UObjectManager 내 충돌이 발생한다.
	if (!Ar.IsDuplicating())
	{
		Ar << ObjectName;
	}

	if (UClass* Cls = GetClass())
	{
		Cls->SerializeBin(Ar, this);
	}
}

void UObject::GetAllProperties(TArray<const FProperty*>& OutProps)
{
	UClass* Cls = GetClass();
	if (!Cls) return;
	Cls->GetAllProperties(OutProps);
}

void UObject::GetEditableProperties(TArray<const FProperty*>& OutProps)
{
	UClass* Cls = GetClass();
	if (!Cls) return;
	Cls->GetEditableProperties(OutProps);
}

void UObject::PostEditProperty(const char* /*PropertyName*/)
{
	// 기본 UObject는 편집 후 추가 작업 없음.
}

void UObject::GetNonTransientProperties(TArray<const FProperty*>& OutProps)
{
	UClass* Cls = GetClass();
	if (!Cls) return;
	Cls->GetNonTransientProperties(OutProps);
}

UClass UObject::StaticClassInstance("UObject", nullptr, sizeof(UObject), CF_None);
