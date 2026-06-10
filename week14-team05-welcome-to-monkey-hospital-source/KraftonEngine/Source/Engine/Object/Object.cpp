#include "Object.h"
#include "UUIDGenerator.h"
#include "Serialization/Archive.h"
#include "Serialization/DuplicateArchive.h"
#include "Core/Property/NameProperty.h"
#include "Core/Property/NumericProperty.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Object/GarbageCollection.h"
#include "Object/Reflection/UStruct.h"

TArray<UObject*> GUObjectArray;
TSet<UObject*> GUObjectSet;

static uint32 GNextObjectSerialNumber = 1;

UObject::UObject()
{
	UUID = UUIDGenerator::GenUUID();
	SerialNumber = GNextObjectSerialNumber++;
	InternalIndex = static_cast<uint32>(GUObjectArray.size());
	ObjectFlags = RF_None;
	GUObjectArray.push_back(this);
	GUObjectSet.insert(this);
}

UObject::~UObject()
{
	GUObjectSet.erase(this);

	if (GUObjectArray.empty())
	{
		return;
	}

	uint32 LastIndex = static_cast<uint32>(GUObjectArray.size() - 1);

	if (InternalIndex != LastIndex)
	{
		UObject* LastObject = GUObjectArray[LastIndex];
		GUObjectArray[InternalIndex] = LastObject;
		if (LastObject)
		{
			LastObject->InternalIndex = InternalIndex;
		}
	}

	GUObjectArray.pop_back();
}

UObject* UObject::Duplicate(UObject* NewOuter) const
{
	FDuplicateArchiveContext DuplicateContext;
	UObject* Dup = DuplicateWithArchiveContext(NewOuter, DuplicateContext);
	DuplicateContext.ResolveObjectReferenceFixups();
	if (Dup)
	{
		Dup->PostDuplicate();
	}
	return Dup;
}

UObject* UObject::DuplicateWithArchiveContext(UObject* NewOuter, FDuplicateArchiveContext& DuplicateContext) const
{
	FScopedGarbageCollectionBlocker GCBlocker;
	// FObjectFactory 기반 같은 타입 인스턴스 생성 → Serialize 왕복.
	// UUID/Name은 생성자에서 새로 발급되며, Serialize에서 덮어쓰지 않는 것이 규칙이다.
	// NewOuter가 nullptr이면 원본의 Outer를 그대로 승계.
	UObject* EffectiveOuter = NewOuter ? NewOuter : Outer;
	UObject* Dup = FObjectFactory::Get().Create(GetClass()->GetName(), EffectiveOuter);
	if (!Dup)
	{
		return nullptr;
	}
	DuplicateContext.AddObjectMapping(GetUUID(), Dup);

	FDuplicateDataWriter Writer;
	const_cast<UObject*>(this)->Serialize(Writer);

	FDuplicateDataReader Reader(Writer.GetBuffer(), DuplicateContext);
	Dup->Serialize(Reader);
	return Dup;
}

void UObject::Serialize(FArchive& Ar)
{
	// Template Method: 직렬화 단계를 고정 순서로 호출한다.
	// 이 경로(obj->Serialize)는 에셋(.uasset)/복제(Duplicate)에서만 사용된다.
	// 씬 저장은 SceneSaveManager 가 SerializeProperties 를 직접 호출하므로 이 경로를 타지 않는다.
	SerializeIdentity(Ar);

	if (Ar.IsSaving())
	{
		OnPreSave(Ar);
	}

	if (ShouldReflectProperties())
	{
		SerializeProperties(Ar, PF_Save);
	}

	SerializeExtra(Ar);

	if (Ar.IsLoading())
	{
		OnPostLoad(Ar);
	}
}

void UObject::SerializeIdentity(FArchive& Ar)
{
	// 옛 에셋 포맷 호환을 위해 저장 시 ObjectName을 기록하되, 로드/복제
	// 경로에서는 현재 세션에서 새로 부여된 런타임 identity를 덮어쓰지 않는다.
	FName SerializedName = Ar.IsSaving() ? ObjectName : FName();
	Ar << SerializedName;
}

void UObject::SerializeProperties(FArchive& Ar, uint32 RequiredFlags)
{
	// Legacy binary assets still rely on the historical order-based payload path.
	// Versioned package loads opt into tagged property serialization via the
	// archive seam, which makes HasProperty(...) reflect real payload presence.
	Ar.BeginObject();

	TArray<const FProperty*> Properties;
	GetClass()->GetPropertyRefs(Properties);

	for (const FProperty* Property : Properties)
	{
		if (!Property || (Property->Flags & RequiredFlags) != RequiredFlags)
		{
			continue;
		}

		if (!Property->GetValuePtrFor(this))
		{
			continue;
		}

		if (!Ar.HasProperty(Property->Name))
		{
			continue;
		}

		FPropertySerializeContext Context;
		Context.Owner = this;
		Context.RequiredFlags = RequiredFlags;
		Context.bIsVersionedTaggedLoad = Ar.IsVersionedTaggedLoad();

		Ar.BeginProperty(Property->Name);
		Property->SerializeValue(Property->GetValuePtrFor(this), Ar, Context);
		Ar.EndProperty();
	}

	Ar.EndObject();
}

void UObject::GetEditableProperties(TArray<FPropertyValue>& OutProps)
{
	PreGetEditableProperties();

	TArray<const FProperty*> Properties;
	GetClass()->GetPropertyRefs(Properties);

	for (const FProperty* Property : Properties)
	{
		if (!Property || (Property->Flags & PF_Edit) == 0)
		{
			continue;
		}
		if (!ShouldExposeProperty(*Property))
		{
			continue;
		}

		if(Property->GetValuePtrFor(this))
		{
			OutProps.push_back(Property->ToValue(this, this));
		}
	}
}

bool UObject::ShouldExposeProperty(const FProperty& /*Property*/) const
{
	return true;
}

void UObject::PostEditProperty(const char* /*PropertyName*/)
{
	// 기본 UObject는 편집 후 추가 작업 없음.
}

void UObject::PostEditChangeProperty(const FPropertyChangedEvent& Event)
{
	PostEditProperty(Event.PropertyName);
}

void UObject::RegisterProperties(UStruct* Class)
{
	if (!Class)
	{
		return;
	}

	Class->AddProperty(new FNameProperty(
		"ObjectName",
		"Header",
		PF_Edit | PF_Save,
		offsetof(UObject, ObjectName),
		sizeof(static_cast<UObject*>(nullptr)->ObjectName),
		"Name",
		{{"category", "Header"}, {"displayname", "Name"}, {"edit", "true"}, {"member", "ObjectName"}, {"save", "true"}},
		"UObject"
	));
	Class->AddProperty(new FIntProperty(
		"UUID",
		"Header",
		PF_Edit | PF_ReadOnly,
		offsetof(UObject, UUID),
		sizeof(static_cast<UObject*>(nullptr)->UUID),
		0.0f,
		0.0f,
		0.1f,
		"UUID",
		{{"category", "Header"}, {"displayname", "UUID"}, {"member", "UUID"}, {"readonly", "true"}, {"visibleanywhere", "true"}},
		"UObject"
	));
}

void UObject::AddReferencedObjects(FReferenceCollector& Collector)
{
	const TArray<FGCReferenceToken>& Tokens = GetClass()->GetReferenceTokenStream();
	for (const FGCReferenceToken& Token : Tokens)
	{
		const FProperty* Property = Token.Property;
		if (!Property)
		{
			continue;
		}

		FScopedReferenceName Scope(Collector, Property->Name);
		Property->AddReferencedObjects(Property->GetValuePtrFor(this), Collector);
	}
}

void UObject::BeginDestroy()
{
	if (HasAnyFlags(RF_BeginDestroy))
	{
		return;
	}

	SetFlags(RF_BeginDestroy | RF_PendingKill);
}

void UObject::FinishDestroy()
{
	SetFlags(RF_FinishDestroy);
}

UClass UObject::StaticClassInstance("UObject", nullptr, sizeof(UObject), CF_None);

namespace
{
	FClassRegistrar GUObjectClassRegistrar(&UObject::StaticClassInstance);

	struct FUObjectRootReflectionRegistrar
	{
		FUObjectRootReflectionRegistrar()
		{
			UObject::RegisterProperties(UObject::StaticClass());
			FObjectFactory::Get().Register(
				"UObject",
				[](UObject* InOuter)->UObject* { return UObjectManager::Get().CreateObject<UObject>(InOuter); }
			);
		}
	};

	FUObjectRootReflectionRegistrar GUObjectRootReflectionRegistrar;
}

bool UObject::ProcessEvent(const FFunction* Function, void* ParametersStorage, void* ReturnValueStorage)
{
    if (!Function)
    {
        return false;
    }

    if (!Function->IsStatic() && !IsValid(this))
    {
        return false;
    }

    return Function->Invoke(this, ParametersStorage, ReturnValueStorage);
}

