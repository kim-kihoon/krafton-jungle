#include "Game/Systems/ItemSystem.h"

#include "Game/Systems/GameContext.h"

FItemSystem& FItemSystem::Get()
{
	static FItemSystem Instance;
	return Instance;
}

void FItemSystem::ResetRuntimeState()
{
	GGameContext::Get().ClearCurrentInspectedItem();
}

void FItemSystem::ClearItemData()
{
	Items.clear();
}

void FItemSystem::RegisterItemData(const FGameItemData& ItemData)
{
	if (ItemData.ItemId.empty())
	{
		return;
	}

	for (FGameItemData& ExistingItem : Items)
	{
		if (ExistingItem.ItemId == ItemData.ItemId)
		{
			ExistingItem = ItemData;
			return;
		}
	}

	Items.push_back(ItemData);
}

const FGameItemData* FItemSystem::FindItemData(const FString& ItemId) const
{
	for (const FGameItemData& Item : Items)
	{
		if (Item.ItemId == ItemId)
		{
			return &Item;
		}
	}

	return nullptr;
}

bool FItemSystem::DiscoverItem(const FString& ItemId)
{
	if (!FindItemData(ItemId))
	{
		return false;
	}

	return GGameContext::Get().MarkItemFound(ItemId);
}

bool FItemSystem::KeepItem(const FString& ItemId)
{
	return ClassifyItem(ItemId, EGameItemDisposition::Kept);
}

bool FItemSystem::DiscardItem(const FString& ItemId)
{
	return ClassifyItem(ItemId, EGameItemDisposition::Discarded);
}

bool FItemSystem::ClassifyItem(const FString& ItemId, EGameItemDisposition Disposition)
{
	const FGameItemData* ItemData = FindItemData(ItemId);
	if (!ItemData || !ItemData->bCanClassify)
	{
		return false;
	}

	GGameContext& Context = GGameContext::Get();
	switch (Disposition)
	{
	case EGameItemDisposition::Kept:
	{
		const bool bChanged = Context.MarkItemKept(ItemId);
		for (const FString& Flag : ItemData->StoryFlags)
		{
			Context.UnlockStoryFlag(Flag);
		}
		return bChanged;
	}

	case EGameItemDisposition::Discarded:
		return Context.MarkItemDiscarded(ItemId);

	default:
		return false;
	}
}

bool FItemSystem::PlaceItemInDecisionBox(const FString& ItemId, EItemDecisionBoxType BoxType)
{
	switch (BoxType)
	{
	case EItemDecisionBoxType::KeepBox:
		return ClassifyItem(ItemId, EGameItemDisposition::Kept);

	case EItemDecisionBoxType::DiscardBox:
		return ClassifyItem(ItemId, EGameItemDisposition::Discarded);

	default:
		return false;
	}
}

bool FItemSystem::InspectItem(const FString& ItemId)
{
	const FGameItemData* ItemData = FindItemData(ItemId);
	if (!ItemData || !ItemData->bCanInspect)
	{
		return false;
	}

	GGameContext& Context = GGameContext::Get();
	Context.MarkItemFound(ItemId);
	Context.SetCurrentInspectedItem(ItemId);
	return true;
}

FString FItemSystem::GetDescriptionForCurrentState(const FString& ItemId) const
{
	const FGameItemData* ItemData = FindItemData(ItemId);
	if (!ItemData)
	{
		return "";
	}

	switch (GGameContext::Get().GetItemDisposition(ItemId))
	{
	case EGameItemDisposition::Kept:
		return ItemData->DescriptionWhenKept.empty() ? ItemData->DescriptionWhenFound : ItemData->DescriptionWhenKept;

	case EGameItemDisposition::Discarded:
		return ItemData->DescriptionWhenDiscarded.empty() ? ItemData->DescriptionWhenFound : ItemData->DescriptionWhenDiscarded;

	case EGameItemDisposition::Found:
	case EGameItemDisposition::None:
	default:
		return ItemData->DescriptionWhenFound;
	}
}
