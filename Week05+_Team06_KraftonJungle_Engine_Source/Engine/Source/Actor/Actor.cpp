#include "Actor.h"
#include "Object/ObjectFactory.h"
#include "Component/UUIDBillboardComponent.h"
#include "Object/Class.h"
#include "Renderer/Material.h"
#include "Component/TextRenderComponent.h"
#include "Component/SceneComponent.h"
#include "Debug/EngineLog.h"
#include "Serializer/Archive.h"
#include "Scene/Level.h"
IMPLEMENT_RTTI(AActor, UObject)

namespace {
	FVector GZeroVector{};
}

ULevel* AActor::GetLevel() const { return Level; }
void AActor::SetLevel(ULevel* InLevel) { Level = InLevel; }
UWorld* AActor::GetWorld() const
{
	if (Level)
	{
		return Level->GetTypedOuter<UWorld>();
	}
	return nullptr;
}
USceneComponent* AActor::GetRootComponent() const { return RootComponent; }

void AActor::SetRootComponent(USceneComponent* InRootComponent)
{
	// 의문점
	// 기존에 RootComponent가 있을 시에는 RootComponent의 OwnerActor를 지워주나?
	// 이러면 두 개의 RootComponent가 하나의 Owner을 가지고 있는건데.
	if (RootComponent)
	{
		RemoveOwnedComponent(RootComponent);
	}

	RootComponent = InRootComponent;

	// 2. 새 루트가 들어왔다면, OwnedComponents 배열에 확실하게 등록!
	if (RootComponent)
	{
		AddOwnedComponent(RootComponent);
		// AddOwnedComponent 안에서 SetOwner(this)도 해주고 배열에도 넣어주니 일석이조!
	}
}

const TArray<UActorComponent*>& AActor::GetComponents() const { return OwnedComponents; }

void AActor::AddOwnedComponent(UActorComponent* InComponent)
{
	if (InComponent == nullptr)
	{
		return;
	}

	auto It = std::find(OwnedComponents.begin(), OwnedComponents.end(), InComponent);
	if (It != OwnedComponents.end())
	{
		return;
	}

	OwnedComponents.push_back(InComponent);
	InComponent->SetOwner(this);

	if (RootComponent == nullptr && InComponent->IsA(USceneComponent::StaticClass()))
	{
		RootComponent = static_cast<USceneComponent*>(InComponent);
	}
}

void AActor::RemoveOwnedComponent(UActorComponent* InComponent)
{
	if (InComponent == nullptr)
	{
		return;
	}

	std::erase(OwnedComponents, InComponent);

	if (RootComponent == InComponent)
	{
		RootComponent = nullptr;
	}

	InComponent->SetOwner(nullptr);
}

void AActor::PostSpawnInitialize()
{
	if (GetComponentByClass<UUUIDBillboardComponent>() == nullptr)
	{
		UUUIDBillboardComponent* UUIDComponent =
			FObjectFactory::ConstructObject<UUUIDBillboardComponent>(this, "UUIDBillboard");

		if (UUIDComponent)
		{
			AddOwnedComponent(UUIDComponent);

			if (RootComponent && RootComponent != UUIDComponent)
			{
				UUIDComponent->AttachTo(RootComponent);
			}
			UUIDComponent->SetWorldOffset(FVector(0.0f, 0.0f, 0.3f));
			UUIDComponent->SetWorldScale(0.3f);
			UUIDComponent->SetTextColor(FVector4(1.0f, 1.0f, 1.0f, 1.0f));
		}
	}

	for (UActorComponent* Component : OwnedComponents)
	{
		if (Component && !Component->IsRegistered())
		{
			Component->OnRegister();
		}
		if (UPrimitiveComponent* PrimComp = dynamic_cast<UPrimitiveComponent*>(Component))
		{
			PrimComp->UpdateBounds();
		}
	}
}

void AActor::BeginPlay()
{
	if (bActorBegunPlay)
	{
		return;
	}

	bActorBegunPlay = true;

	for (UActorComponent* Component : OwnedComponents)
	{
		if (Component && !Component->HasBegunPlay())
		{
			Component->BeginPlay();
		}
	}
}

void AActor::Tick(float DeltaTime)
{
	if (!CanTick() || bPendingDestroy)
	{
		return;
	}

	for (UActorComponent* Component : OwnedComponents)
	{
		if (Component && Component->CanTick())
		{
			Component->Tick(DeltaTime);
		}
	}
}

void AActor::EndPlay()
{
}

void AActor::Destroy()
{
	if (bPendingDestroy)
	{
		return;
	}

	bPendingDestroy = true;
	MarkPendingKill();

	for (UActorComponent* Comp : OwnedComponents)
	{
		if (Comp)
		{
			Comp->MarkPendingKill();
		}
	}
}

void AActor::Serialize(FArchive& Ar)
{
	if (Ar.IsSaving())// Save Actor property
	{
		FString ClassName = GetClass()->GetName();
		Ar.Serialize("Class", ClassName);
		Ar.Serialize("UUID", UUID);

		uint32 RootCompUUID = RootComponent ? RootComponent->UUID : 0;
		Ar.Serialize("RootComponentUUID", RootCompUUID);

		TArray<FArchive*> ComponentArchives;
		for (UActorComponent* Component : OwnedComponents)
		{
			if (Component)
			{
				FArchive* ComponentArchive = new FArchive(true);
				
				FString ComponentClassName = Component->GetClass()->GetName();
				FString ComponentName = Component->GetName();
				ComponentArchive->Serialize("Class", ComponentClassName);
				ComponentArchive->Serialize("Name", ComponentName);

				Component->Serialize(*ComponentArchive);
				ComponentArchives.push_back(ComponentArchive);
			}
		}

		Ar.Serialize("Components", ComponentArchives);

		for (FArchive* ComponentArchive : ComponentArchives) delete ComponentArchive;
	}
	else//Load 
	{
		if (Ar.Contains("UUID"))
		{
			uint32 SavedUUID = 0;
			Ar.Serialize("UUID", SavedUUID);

			GUUIDToObjectMap.erase(UUID);
			if (auto It = GUUIDToObjectMap.find(SavedUUID); It != GUUIDToObjectMap.end() && It->second != this)
			{
				It->second->UUID = 0;
				GUUIDToObjectMap.erase(It);
			}
			UUID = SavedUUID;
			GUUIDToObjectMap[SavedUUID] = this;

		}

		uint32 SavedRootCompUUID = 0;
		TMap<USceneComponent*, uint32> SavedAttachParents;
		if (Ar.Contains("RootComponentUUID"))
		{
			Ar.Serialize("RootComponentUUID", SavedRootCompUUID);
		}
		
		if (Ar.Contains("Components"))
		{
			TArray<FArchive*> ComponentArchives;
			Ar.Serialize("Components", ComponentArchives);
			TArray<UActorComponent*> MatchedComponents;

			for (FArchive* ComponentArchive: ComponentArchives)
			{
				if (ComponentArchive->Contains("Class"))
				{
					FString ComponentClassName;
					FString ComponentName;
					uint32 ComponentUUID = 0;
					uint32 SavedAttachParentUUID = 0;
					ComponentArchive->Serialize("Class", ComponentClassName);
					if (ComponentArchive->Contains("Name"))
					{
						ComponentArchive->Serialize("Name", ComponentName);
					}
					if (ComponentArchive->Contains("UUID"))
					{
						ComponentArchive->Serialize("UUID", ComponentUUID);
					}
					if (ComponentArchive->Contains("AttachParentUUID"))
					{
						ComponentArchive->Serialize("AttachParentUUID", SavedAttachParentUUID);
					}

					
					UClass* ComponentClass = UClass::FindClass(ComponentClassName);
					if (ComponentClass)
					{
						UActorComponent* TargetComponent = nullptr;
						auto IsMatched = [&MatchedComponents](UActorComponent* Component)
						{
							return std::find(MatchedComponents.begin(), MatchedComponents.end(), Component) != MatchedComponents.end();
						};

						if (ComponentUUID != 0)
						{
							for (UActorComponent* ExistingComponent : OwnedComponents)
							{
								if (ExistingComponent && ExistingComponent->UUID == ComponentUUID)
								{
									TargetComponent = ExistingComponent;
									break;
								}
							}
						}

						if (!TargetComponent && !ComponentName.empty())
						{
							for (UActorComponent* ExistingComponent : OwnedComponents)
							{
								if (ExistingComponent == nullptr || IsMatched(ExistingComponent))
								{
									continue;
								}

								if (ExistingComponent->GetClass() == ComponentClass && ExistingComponent->GetName() == ComponentName)
								{
									TargetComponent = ExistingComponent;
									break;
								}
							}
						}

						if (!TargetComponent)
						{
							for (UActorComponent* ExistingComponent : OwnedComponents)
							{
								if (ExistingComponent == nullptr || IsMatched(ExistingComponent))
								{
									continue;
								}

								if (ExistingComponent->GetClass() == ComponentClass)
								{
									TargetComponent = ExistingComponent;
									break;
								}
							}
						}

						if (!TargetComponent)
						{
							const FString NewComponentName = ComponentName.empty() ? ComponentClassName : ComponentName;
							UObject* NewObject = FObjectFactory::ConstructObject(ComponentClass, this, NewComponentName);
							TargetComponent = static_cast<UActorComponent*>(NewObject);

							if (TargetComponent)
							{
								AddOwnedComponent(TargetComponent);
							}
						}

						if (TargetComponent)
						{
							TargetComponent->Serialize(*ComponentArchive);
							MatchedComponents.push_back(TargetComponent);

							if (TargetComponent->IsA(USceneComponent::StaticClass()))
							{
								SavedAttachParents[static_cast<USceneComponent*>(TargetComponent)] = SavedAttachParentUUID;
							}
						}
					}
					else
					{
						UE_LOG("[Serialize] Unknown Component Class: %s", ComponentClassName.c_str());
					}
				}
			}
			for (FArchive* ComponentArchive : ComponentArchives) delete ComponentArchive;
		}

		if (SavedRootCompUUID != 0)
		{
			for (UActorComponent* Comp : OwnedComponents)
			{
				if (Comp && Comp->UUID == SavedRootCompUUID && Comp->IsA(USceneComponent::StaticClass()))
				{
					RootComponent = static_cast<USceneComponent*>(Comp);
					break;
				}
			}
		}

		TMap<uint32, USceneComponent*> SceneComponentByUUID;
		for (UActorComponent* Comp : OwnedComponents)
		{
			if (Comp && Comp->IsA(USceneComponent::StaticClass()))
			{
				USceneComponent* SceneComp = static_cast<USceneComponent*>(Comp);
				SceneComp->DetachFromParent();
				SceneComponentByUUID[SceneComp->UUID] = SceneComp;
			}
		}

		for (const auto& [SceneComp, ParentUUID] : SavedAttachParents)
		{
			if (SceneComp == nullptr || SceneComp == RootComponent)
			{
				continue;
			}

			USceneComponent* ParentComponent = nullptr;
			if (ParentUUID != 0)
			{
				auto FoundParent = SceneComponentByUUID.find(ParentUUID);
				if (FoundParent != SceneComponentByUUID.end())
				{
					ParentComponent = FoundParent->second;
				}
			}

			if (ParentComponent == nullptr && RootComponent && SceneComp != RootComponent)
			{
				ParentComponent = RootComponent;
			}

			if (ParentComponent && ParentComponent != SceneComp)
			{
				SceneComp->AttachTo(ParentComponent);
			}
		}

		for (UActorComponent* Component : OwnedComponents)
		{
			if (Component && !Component->IsRegistered())
			{
				Component->OnRegister();
			}

			if (UPrimitiveComponent* PrimComp = dynamic_cast<UPrimitiveComponent*>(Component))
			{
				PrimComp->UpdateBounds();
			}
		}
	}
}
const FVector& AActor::GetActorLocation() const
{
	if (RootComponent == nullptr)
	{
		return GZeroVector;
	}

	return RootComponent->GetRelativeLocation();
}

void AActor::SetActorLocation(const FVector& InLocation)
{
	if (RootComponent == nullptr)
	{
		return;
	}

	RootComponent->SetRelativeLocation(InLocation);
}
void AActor::FixupReferences(const FDuplicateionContext& Context)
{
	UObject::FixupReferences(Context);

	if (RootComponent)
	{
		// 원본 RootComponent에 대응하는 복제본으로 정확히 매핑
		USceneComponent* MappedRoot = static_cast<USceneComponent*>(Context.GetMappedObject(RootComponent));
		if (MappedRoot)
		{
			RootComponent = MappedRoot;
		}
	}
}


void AActor::DuplicateSubObjects(FDuplicateionContext& Context)
{
	UObject::DuplicateSubObjects(Context);

	// 복제 전 원본의 RootComponent 포인터를 기억함 (현재 this는 Clone된 상태라 에디터 객체를 가리키고 있음)
	USceneComponent* OldRoot = this->RootComponent;

	TArray<UActorComponent*> OldComponents = this->OwnedComponents;
	this->OwnedComponents.clear();


	for (UActorComponent* OldComp : OldComponents)
	{
		if (OldComp)
		{
			UActorComponent* NewComp = static_cast<UActorComponent*>(OldComp->Duplicate(Context, this));
			this->AddOwnedComponent(NewComp);
		}
	}

	// AddOwnedComponent가 임의로 잡은 루트 대신, 원본과 매칭되는 루트를 강제 설정
	if (OldRoot)
	{
		USceneComponent* NewRoot = static_cast<USceneComponent*>(Context.GetMappedObject(OldRoot));
		if (NewRoot)
		{
			this->RootComponent = NewRoot;
		}
	}
}
