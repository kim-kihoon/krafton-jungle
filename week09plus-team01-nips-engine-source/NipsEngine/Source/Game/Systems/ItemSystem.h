#pragma once

#include "Game/Systems/CleaningGameTypes.h"

class FItemSystem
{
public:
	static FItemSystem& Get();

	void ResetRuntimeState();
	void ClearItemData();

	void RegisterItemData(const FGameItemData& ItemData);
	const FGameItemData* FindItemData(const FString& ItemId) const;
	const TArray<FGameItemData>& GetAllItemData() const { return Items; }

	bool DiscoverItem(const FString& ItemId);
	bool KeepItem(const FString& ItemId);
	bool DiscardItem(const FString& ItemId);
	bool ClassifyItem(const FString& ItemId, EGameItemDisposition Disposition);
	bool PlaceItemInDecisionBox(const FString& ItemId, EItemDecisionBoxType BoxType);
	bool InspectItem(const FString& ItemId);

	FString GetDescriptionForCurrentState(const FString& ItemId) const;

private:
	FItemSystem() = default;

	TArray<FGameItemData> Items;
};
