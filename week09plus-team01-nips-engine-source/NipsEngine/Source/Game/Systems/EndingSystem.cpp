#include "Game/Systems/EndingSystem.h"

#include "Game/Systems/GameContext.h"
#include "Game/Systems/ItemSystem.h"

FEndingSystem& FEndingSystem::Get()
{
	static FEndingSystem Instance;
	return Instance;
}

FEndingResult FEndingSystem::EvaluateEnding() const
{
	FEndingResult Result;

	const GGameContext& Context = GGameContext::Get();
	const FItemSystem& ItemSystem = FItemSystem::Get();

	for (const FString& ItemId : Context.GetKeptItemIds())
	{
		const FGameItemData* ItemData = ItemSystem.FindItemData(ItemId);
		if (!ItemData)
		{
			continue;
		}

		switch (ItemData->EndingRole)
		{
		case EGameItemEndingRole::Bad:
			Result.KeptBadItemIds.push_back(ItemId);
			break;

		case EGameItemEndingRole::Good:
			Result.KeptGoodItemIds.push_back(ItemId);
			break;

		case EGameItemEndingRole::Normal:
			Result.KeptNormalItemIds.push_back(ItemId);
			break;

		case EGameItemEndingRole::None:
		default:
			break;
		}
	}

	if (!Result.KeptBadItemIds.empty())
	{
		Result.EndingId = "Ending_Bad";
	}
	else if (!Result.KeptGoodItemIds.empty())
	{
		Result.EndingId = "Ending_Good";
	}
	else
	{
		Result.EndingId = "Ending_Normal";
	}

	return Result;
}

FString FEndingSystem::BuildEndingIdFromContext() const
{
	return EvaluateEnding().EndingId;
}
