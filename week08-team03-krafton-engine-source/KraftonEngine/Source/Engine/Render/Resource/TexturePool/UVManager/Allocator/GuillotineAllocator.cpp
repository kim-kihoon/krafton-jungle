#include "Render/Resource/TexturePool/UVManager/Allocator/GuillotineAllocator.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

void FGuillotineAllocator::Initialize(uint32 InAtlasSize, uint32 InLayerCount, uint32 InMinBlockSize)
{
	FTexturePoolAllocatorBase::Initialize(InAtlasSize, InLayerCount, InMinBlockSize);
	GridCount = MinBlockSize > 0 ? AtlasSize / MinBlockSize : 0;
	ResetAllocationState(InLayerCount);
	NextHandle = 1;
}

bool FGuillotineAllocator::AllocateHandle(float TextureSize, FTexturePoolHandle& OutHandle)
{
	const uint32 BlockCount = GetBlockCount(TextureSize);
	if (BlockCount == 0 || BlockCount > GridCount)
	{
		return false;
	}

	FAtlasRect UsedRect;
	if (!TryPlaceRectIntoFreeRects(FreeRects, BlockCount, BlockCount, UsedRect))
	{
		return false;
	}

	const uint32 HandleId = NextHandle++;
	AllocatedRects.emplace(HandleId, UsedRect);

	OutHandle.InternalIndex = HandleId;
	OutHandle.ArrayIndex = UsedRect.ArrayIndex;
	return true;
}

bool FGuillotineAllocator::CanAllocateHandle(float TextureSize) const
{
	const uint32 BlockCount = GetBlockCount(TextureSize);
	return BlockCount > 0 && BlockCount <= GridCount && FindBestFreeRect(BlockCount, BlockCount, FreeRects) >= 0;
}

bool FGuillotineAllocator::CanAllocateHandleSet(const FTexturePoolHandleRequest& Request) const
{
	TArray<uint32> SortedSizes = Request.Sizes;
	std::sort(SortedSizes.begin(), SortedSizes.end(), std::greater<uint32>());

	TArray<FAtlasRect> TestFreeRects = FreeRects;
	for (uint32 Size : SortedSizes)
	{
		const uint32 BlockCount = GetBlockCount(static_cast<float>(Size));
		if (BlockCount == 0 || BlockCount > GridCount)
		{
			return false;
		}

		FAtlasRect UsedRect;
		if (!TryPlaceRectIntoFreeRects(TestFreeRects, BlockCount, BlockCount, UsedRect))
		{
			return false;
		}
	}

	return true;
}

FAtlasUV FGuillotineAllocator::GetAtlasUV(const FTexturePoolHandle& InHandle)
{
	auto It = AllocatedRects.find(InHandle.InternalIndex);
	if (It == AllocatedRects.end())
	{
		return {};
	}

	const FAtlasRect& Entry = It->second;
	const float PixelX1 = static_cast<float>(Entry.X * MinBlockSize);
	const float PixelY1 = static_cast<float>(Entry.Y * MinBlockSize);
	const float PixelX2 = static_cast<float>((Entry.X + Entry.W) * MinBlockSize);
	const float PixelY2 = static_cast<float>((Entry.Y + Entry.H) * MinBlockSize);

	FAtlasUV UV;
	UV.ArrayIndex = Entry.ArrayIndex;
	UV.u1 = PixelX1 / static_cast<float>(AtlasSize);
	UV.v1 = PixelY1 / static_cast<float>(AtlasSize);
	UV.u2 = PixelX2 / static_cast<float>(AtlasSize);
	UV.v2 = PixelY2 / static_cast<float>(AtlasSize);
	return UV;
}

void FGuillotineAllocator::ReleaseHandle(const FTexturePoolHandle& InHandle)
{
	auto It = AllocatedRects.find(InHandle.InternalIndex);
	if (It == AllocatedRects.end())
	{
		return;
	}

	const FAtlasRect FreedRect = It->second;
	AllocatedRects.erase(It);

	FreeRects.push_back(FreedRect);
	MergeAdjacentFreeRects(FreeRects);
	PruneContainedFreeRects(FreeRects);
}

void FGuillotineAllocator::BroadcastEntries()
{
	// UVs are computed on demand from the current atlas size.
}

uint32 FGuillotineAllocator::GetFreeRectCount() const
{
	return static_cast<uint32>(FreeRects.size());
}

uint64 FGuillotineAllocator::GetTotalFreeArea() const
{
	uint64 TotalArea = 0;
	for (const FAtlasRect& Rect : FreeRects)
	{
		TotalArea += static_cast<uint64>(Rect.W) * static_cast<uint64>(Rect.H);
	}
	return TotalArea;
}

uint64 FGuillotineAllocator::GetLargestFreeRectArea() const
{
	uint64 LargestArea = 0;
	for (const FAtlasRect& Rect : FreeRects)
	{
		LargestArea = std::max(LargestArea, static_cast<uint64>(Rect.W) * static_cast<uint64>(Rect.H));
	}
	return LargestArea;
}

float FGuillotineAllocator::GetFragmentationRatio() const
{
	const uint64 TotalArea = GetTotalFreeArea();
	if (TotalArea == 0)
	{
		return 1.0f;
	}

	return 1.0f - (static_cast<float>(GetLargestFreeRectArea()) / static_cast<float>(TotalArea));
}

void FGuillotineAllocator::GetFreeRects(TArray<FAtlasDebugRect>& OutRects) const
{
	OutRects.reserve(OutRects.size() + FreeRects.size());
	for (const FAtlasRect& Rect : FreeRects)
	{
		FAtlasDebugRect DebugRect = {};
		DebugRect.X = Rect.X * MinBlockSize;
		DebugRect.Y = Rect.Y * MinBlockSize;
		DebugRect.W = Rect.W * MinBlockSize;
		DebugRect.H = Rect.H * MinBlockSize;
		DebugRect.ArrayIndex = Rect.ArrayIndex;
		DebugRect.bAllocated = false;
		OutRects.push_back(DebugRect);
	}
}

void FGuillotineAllocator::GetAllocatedRects(TArray<FAtlasDebugRect>& OutRects) const
{
	OutRects.reserve(OutRects.size() + AllocatedRects.size());
	for (const auto& Pair : AllocatedRects)
	{
		const FAtlasRect& Rect = Pair.second;

		FAtlasDebugRect DebugRect = {};
		DebugRect.X = Rect.X * MinBlockSize;
		DebugRect.Y = Rect.Y * MinBlockSize;
		DebugRect.W = Rect.W * MinBlockSize;
		DebugRect.H = Rect.H * MinBlockSize;
		DebugRect.ArrayIndex = Rect.ArrayIndex;
		DebugRect.HandleIndex = Pair.first;
		DebugRect.bAllocated = true;
		OutRects.push_back(DebugRect);
	}
}

void FGuillotineAllocator::SetSize(uint32 InNewTextureSize)
{
	FTexturePoolAllocatorBase::SetSize(InNewTextureSize);
	AtlasSize = InNewTextureSize;
	GridCount = MinBlockSize > 0 ? AtlasSize / MinBlockSize : 0;
	ResetAllocationState(GetLayerCount());
}

void FGuillotineAllocator::SetLayerCount(uint32 InNewLayerCount)
{
	const uint32 OldLayerCount = GetLayerCount();
	FTexturePoolAllocatorBase::SetLayerCount(InNewLayerCount);

	if (InNewLayerCount >= OldLayerCount && !AllocatedRects.empty())
	{
		for (uint32 SliceIndex = OldLayerCount; SliceIndex < InNewLayerCount; ++SliceIndex)
		{
			FreeRects.push_back({ 0, 0, GridCount, GridCount, SliceIndex });
		}
		MergeAdjacentFreeRects(FreeRects);
		PruneContainedFreeRects(FreeRects);
		return;
	}

	ResetAllocationState(InNewLayerCount);
}

uint32 FGuillotineAllocator::CeilDiv(uint32 A, uint32 B)
{
	return B == 0 ? 0 : (A + B - 1) / B;
}

uint32 FGuillotineAllocator::Index(uint32 X, uint32 Y) const
{
	return Y * GridCount + X;
}

void FGuillotineAllocator::ResetFreeRects(uint32 InLayerCount)
{
	FreeRects.clear();
	if (GridCount == 0)
	{
		return;
	}

	FreeRects.reserve(InLayerCount);
	for (uint32 SliceIndex = 0; SliceIndex < InLayerCount; ++SliceIndex)
	{
		FreeRects.push_back({ 0, 0, GridCount, GridCount, SliceIndex });
	}
}

void FGuillotineAllocator::ResetAllocationState(uint32 InLayerCount)
{
	AllocatedRects.clear();
	ResetFreeRects(InLayerCount);
}

uint32 FGuillotineAllocator::GetBlockCount(float TextureSize) const
{
	if (MinBlockSize == 0)
	{
		return 0;
	}

	const uint32 RequestSize = static_cast<uint32>(std::ceil(TextureSize));
	return CeilDiv(RequestSize, MinBlockSize);
}

int32 FGuillotineAllocator::FindBestFreeRect(uint32 W, uint32 H, const TArray<FAtlasRect>& InFreeRects) const
{
	int32 BestIndex = -1;
	uint64 BestWaste = std::numeric_limits<uint64>::max();
	uint32 BestShortSideLeftover = std::numeric_limits<uint32>::max();

	for (uint32 Index = 0; Index < static_cast<uint32>(InFreeRects.size()); ++Index)
	{
		const FAtlasRect& Rect = InFreeRects[Index];
		if (Rect.W < W || Rect.H < H)
		{
			continue;
		}

		const uint64 RectArea = static_cast<uint64>(Rect.W) * static_cast<uint64>(Rect.H);
		const uint64 RequestArea = static_cast<uint64>(W) * static_cast<uint64>(H);
		const uint64 Waste = RectArea - RequestArea;
		const uint32 ShortSideLeftover = std::min(Rect.W - W, Rect.H - H);

		bool bBetter = Waste < BestWaste;
		if (!bBetter && Waste == BestWaste)
		{
			const FAtlasRect& BestRect = InFreeRects[BestIndex];
			bBetter = ShortSideLeftover < BestShortSideLeftover
				|| (ShortSideLeftover == BestShortSideLeftover && Rect.ArrayIndex < BestRect.ArrayIndex)
				|| (ShortSideLeftover == BestShortSideLeftover && Rect.ArrayIndex == BestRect.ArrayIndex && Rect.Y < BestRect.Y)
				|| (ShortSideLeftover == BestShortSideLeftover && Rect.ArrayIndex == BestRect.ArrayIndex && Rect.Y == BestRect.Y && Rect.X < BestRect.X);
		}

		if (bBetter)
		{
			BestIndex = static_cast<int32>(Index);
			BestWaste = Waste;
			BestShortSideLeftover = ShortSideLeftover;
		}
	}

	return BestIndex;
}

bool FGuillotineAllocator::TryPlaceRectIntoFreeRects(TArray<FAtlasRect>& InOutFreeRects, uint32 W, uint32 H, FAtlasRect& OutRect) const
{
	const int32 BestIndex = FindBestFreeRect(W, H, InOutFreeRects);
	if (BestIndex < 0)
	{
		return false;
	}

	const FAtlasRect& FreeRect = InOutFreeRects[BestIndex];
	OutRect = { FreeRect.X, FreeRect.Y, W, H, FreeRect.ArrayIndex };
	SplitFreeRect(InOutFreeRects, static_cast<uint32>(BestIndex), OutRect);
	MergeAdjacentFreeRects(InOutFreeRects);
	PruneContainedFreeRects(InOutFreeRects);
	return true;
}

void FGuillotineAllocator::SplitFreeRect(TArray<FAtlasRect>& InOutFreeRects, uint32 FreeRectIndex, const FAtlasRect& Used) const
{
	if (FreeRectIndex >= InOutFreeRects.size())
	{
		return;
	}

	const FAtlasRect FreeRect = InOutFreeRects[FreeRectIndex];
	InOutFreeRects.erase(InOutFreeRects.begin() + FreeRectIndex);

	if (FreeRect.W > Used.W)
	{
		InOutFreeRects.push_back({
			FreeRect.X + Used.W,
			FreeRect.Y,
			FreeRect.W - Used.W,
			Used.H,
			FreeRect.ArrayIndex });
	}

	if (FreeRect.H > Used.H)
	{
		InOutFreeRects.push_back({
			FreeRect.X,
			FreeRect.Y + Used.H,
			FreeRect.W,
			FreeRect.H - Used.H,
			FreeRect.ArrayIndex });
	}
}

void FGuillotineAllocator::PruneContainedFreeRects(TArray<FAtlasRect>& InOutFreeRects) const
{
	for (uint32 i = 0; i < InOutFreeRects.size(); ++i)
	{
		for (uint32 j = i + 1; j < InOutFreeRects.size();)
		{
			if (IsContained(InOutFreeRects[i], InOutFreeRects[j]))
			{
				InOutFreeRects.erase(InOutFreeRects.begin() + i);
				--i;
				break;
			}

			if (IsContained(InOutFreeRects[j], InOutFreeRects[i]))
			{
				InOutFreeRects.erase(InOutFreeRects.begin() + j);
				continue;
			}

			++j;
		}
	}
}

void FGuillotineAllocator::MergeAdjacentFreeRects(TArray<FAtlasRect>& InOutFreeRects) const
{
	bool bMerged = true;
	while (bMerged)
	{
		bMerged = false;
		for (uint32 i = 0; i < InOutFreeRects.size() && !bMerged; ++i)
		{
			for (uint32 j = i + 1; j < InOutFreeRects.size(); ++j)
			{
				FAtlasRect& A = InOutFreeRects[i];
				FAtlasRect& B = InOutFreeRects[j];
				if (A.ArrayIndex != B.ArrayIndex)
				{
					continue;
				}

				if (A.Y == B.Y && A.H == B.H)
				{
					if (A.X + A.W == B.X)
					{
						A.W += B.W;
						InOutFreeRects.erase(InOutFreeRects.begin() + j);
						bMerged = true;
						break;
					}
					if (B.X + B.W == A.X)
					{
						A.X = B.X;
						A.W += B.W;
						InOutFreeRects.erase(InOutFreeRects.begin() + j);
						bMerged = true;
						break;
					}
				}

				if (A.X == B.X && A.W == B.W)
				{
					if (A.Y + A.H == B.Y)
					{
						A.H += B.H;
						InOutFreeRects.erase(InOutFreeRects.begin() + j);
						bMerged = true;
						break;
					}
					if (B.Y + B.H == A.Y)
					{
						A.Y = B.Y;
						A.H += B.H;
						InOutFreeRects.erase(InOutFreeRects.begin() + j);
						bMerged = true;
						break;
					}
				}
			}
		}
	}
}

bool FGuillotineAllocator::IsContained(const FAtlasRect& Inner, const FAtlasRect& Outer) const
{
	return Inner.ArrayIndex == Outer.ArrayIndex
		&& Inner.X >= Outer.X
		&& Inner.Y >= Outer.Y
		&& Inner.X + Inner.W <= Outer.X + Outer.W
		&& Inner.Y + Inner.H <= Outer.Y + Outer.H;
}
