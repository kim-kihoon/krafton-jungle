#include "Render/Resource/TexturePool/UVManager/Allocator/BuddyTexturePoolAllocator.h"
#include <algorithm>
#include <cmath>

void FBuddyTexturePoolAllocator::Initialize(uint32 InSize, uint32 InLayerCount, uint32 InMinBlockSize)
{
	const uint32 SafeSize = FloorPowerOfTwo(InSize);
	const uint32 SafeMinBlockSize = InMinBlockSize == 0 ? 256u : CeilPowerOfTwo(InMinBlockSize);

	Size = SafeSize > 0 ? SafeSize : 256u;
	const uint32 ClampedMinBlockSize = std::min(SafeMinBlockSize, Size);

	FTexturePoolAllocatorBase::Initialize(Size, InLayerCount, ClampedMinBlockSize);
	NextHandle = 1;
	ResetAllocationState(InLayerCount);
}

bool FBuddyTexturePoolAllocator::AllocateHandle(float TextureSize, FTexturePoolHandle& OutHandle)
{
	const uint32 RequestSize = static_cast<uint32>(std::ceil(TextureSize));
	FBuddyBlock Block;
	if (!TryAllocateBlock(RequestSize, FreeLists, Block))
	{
		return false;
	}

	const uint32 HandleId = NextHandle++;
	AllocatedBlocks.emplace(HandleId, Block);

	OutHandle.InternalIndex = HandleId;
	OutHandle.ArrayIndex = Block.ArrayIndex;
	return true;
}

bool FBuddyTexturePoolAllocator::CanAllocateHandle(float TextureSize) const
{
	const uint32 RequestSize = static_cast<uint32>(std::ceil(TextureSize));
	if (RequestSize == 0 || RequestSize > Size)
	{
		return false;
	}

	uint32 SearchSize = QuantizeBlockSize(RequestSize);
	while (SearchSize <= Size)
	{
		auto It = FreeLists.find(SearchSize);
		if (It != FreeLists.end() && !It->second.empty())
		{
			return true;
		}

		if (SearchSize > Size / 2u)
		{
			break;
		}
		SearchSize <<= 1u;
	}

	return false;
}

bool FBuddyTexturePoolAllocator::CanAllocateHandleSet(const FTexturePoolHandleRequest& Request) const
{
	TArray<uint32> SortedSizes = Request.Sizes;
	std::sort(SortedSizes.begin(), SortedSizes.end(), std::greater<uint32>());

	TMap<uint32, TArray<FBuddyBlock>> TestFreeLists = FreeLists;
	for (uint32 RequestSize : SortedSizes)
	{
		FBuddyBlock TestBlock;
		if (!TryAllocateBlock(RequestSize, TestFreeLists, TestBlock))
		{
			return false;
		}
	}

	return true;
}

float FBuddyTexturePoolAllocator::EstimateAllocationCost(const FTexturePoolHandleRequest& Request) const
{
	if (MinBlockSize == 0)
	{
		return 0.0f;
	}

	float TotalCost = 0.0f;
	for (uint32 RequestSize : Request.Sizes)
	{
		if (RequestSize == 0 || RequestSize > Size)
		{
			continue;
		}

		const uint32 BlockSize = QuantizeBlockSize(RequestSize);
		const uint32 BlockCount = BlockSize / MinBlockSize;
		TotalCost += static_cast<float>(BlockCount * BlockCount);
	}
	return TotalCost;
}

FAtlasUV FBuddyTexturePoolAllocator::GetAtlasUV(const FTexturePoolHandle& InHandle)
{
	auto It = AllocatedBlocks.find(InHandle.InternalIndex);
	if (It == AllocatedBlocks.end() || Size == 0)
	{
		return {};
	}

	const FBuddyBlock& Block = It->second;
	const float InvSize = 1.0f / static_cast<float>(Size);

	FAtlasUV UV;
	UV.ArrayIndex = Block.ArrayIndex;
	UV.u1 = static_cast<float>(Block.X) * InvSize;
	UV.v1 = static_cast<float>(Block.Y) * InvSize;
	UV.u2 = static_cast<float>(Block.X + Block.Size) * InvSize;
	UV.v2 = static_cast<float>(Block.Y + Block.Size) * InvSize;
	return UV;
}

void FBuddyTexturePoolAllocator::ReleaseHandle(const FTexturePoolHandle& InHandle)
{
	auto It = AllocatedBlocks.find(InHandle.InternalIndex);
	if (It == AllocatedBlocks.end())
	{
		return;
	}

	FBuddyBlock Block = It->second;
	AllocatedBlocks.erase(It);

	while (Block.Size < Size && AreBuddiesFreeForMerge(Block))
	{
		RemoveBuddiesForMerge(Block);

		const uint32 ParentSize = Block.Size * 2u;
		const uint32 ParentX = (Block.X / ParentSize) * ParentSize;
		const uint32 ParentY = (Block.Y / ParentSize) * ParentSize;
		Block = { ParentX, ParentY, ParentSize, Block.ArrayIndex };
	}

	FreeLists[Block.Size].push_back(Block);
}

void FBuddyTexturePoolAllocator::BroadcastEntries()
{
	// UVs are computed on demand from current block state.
}

uint32 FBuddyTexturePoolAllocator::GetFreeRectCount() const
{
	uint32 Count = 0;
	for (const auto& Pair : FreeLists)
	{
		Count += static_cast<uint32>(Pair.second.size());
	}
	return Count;
}

uint64 FBuddyTexturePoolAllocator::GetTotalFreeArea() const
{
	uint64 TotalArea = 0;
	for (const auto& Pair : FreeLists)
	{
		const uint64 BlockSize = static_cast<uint64>(Pair.first);
		TotalArea += BlockSize * BlockSize * static_cast<uint64>(Pair.second.size());
	}
	return TotalArea;
}

uint64 FBuddyTexturePoolAllocator::GetLargestFreeRectArea() const
{
	uint64 LargestArea = 0;
	for (const auto& Pair : FreeLists)
	{
		if (!Pair.second.empty())
		{
			const uint64 BlockSize = static_cast<uint64>(Pair.first);
			LargestArea = std::max(LargestArea, BlockSize * BlockSize);
		}
	}
	return LargestArea;
}

float FBuddyTexturePoolAllocator::GetFragmentationRatio() const
{
	const uint64 TotalArea = GetTotalFreeArea();
	if (TotalArea == 0)
	{
		return 1.0f;
	}

	return 1.0f - (static_cast<float>(GetLargestFreeRectArea()) / static_cast<float>(TotalArea));
}

void FBuddyTexturePoolAllocator::GetFreeRects(TArray<FAtlasDebugRect>& OutRects) const
{
	for (const auto& Pair : FreeLists)
	{
		OutRects.reserve(OutRects.size() + Pair.second.size());
		for (const FBuddyBlock& Block : Pair.second)
		{
			OutRects.push_back(MakeDebugRect(Block, false));
		}
	}
}

void FBuddyTexturePoolAllocator::GetAllocatedRects(TArray<FAtlasDebugRect>& OutRects) const
{
	OutRects.reserve(OutRects.size() + AllocatedBlocks.size());
	for (const auto& Pair : AllocatedBlocks)
	{
		OutRects.push_back(MakeDebugRect(Pair.second, true, Pair.first));
	}
}

void FBuddyTexturePoolAllocator::SetSize(uint32 InNewTextureSize)
{
	const uint32 NewSize = FloorPowerOfTwo(InNewTextureSize);
	Size = NewSize > 0 ? NewSize : 256u;
	if (MinBlockSize > Size)
	{
		MinBlockSize = Size;
	}

	FTexturePoolAllocatorBase::SetSize(Size);
	ResetAllocationState(GetLayerCount());
}

void FBuddyTexturePoolAllocator::SetLayerCount(uint32 InNewLayerCount)
{
	FTexturePoolAllocatorBase::SetLayerCount(InNewLayerCount);
	ResetAllocationState(InNewLayerCount);
}

bool FBuddyTexturePoolAllocator::IsPowerOfTwo(uint32 Value)
{
	return Value != 0 && (Value & (Value - 1u)) == 0;
}

uint32 FBuddyTexturePoolAllocator::FloorPowerOfTwo(uint32 Value)
{
	if (Value == 0)
	{
		return 0;
	}

	uint32 Result = 1;
	while (Result <= Value / 2u)
	{
		Result <<= 1u;
	}
	return Result;
}

uint32 FBuddyTexturePoolAllocator::CeilPowerOfTwo(uint32 Value)
{
	if (Value <= 1)
	{
		return 1;
	}
	if (IsPowerOfTwo(Value))
	{
		return Value;
	}

	uint32 Result = 1;
	while (Result < Value && Result <= 0x80000000u / 2u)
	{
		Result <<= 1u;
	}
	return Result;
}

void FBuddyTexturePoolAllocator::ResetAllocationState(uint32 InLayerCount)
{
	FreeLists.clear();
	AllocatedBlocks.clear();

	if (Size == 0)
	{
		return;
	}

	for (uint32 SliceIndex = 0; SliceIndex < InLayerCount; ++SliceIndex)
	{
		FreeLists[Size].push_back({ 0, 0, Size, SliceIndex });
	}
}

uint32 FBuddyTexturePoolAllocator::QuantizeBlockSize(uint32 RequestSize) const
{
	if (RequestSize <= MinBlockSize)
	{
		return MinBlockSize;
	}

	uint32 BlockSize = MinBlockSize;
	while (BlockSize < RequestSize && BlockSize <= Size / 2u)
	{
		BlockSize <<= 1u;
	}
	return std::min(BlockSize, Size);
}

bool FBuddyTexturePoolAllocator::TryAllocateBlock(
	uint32 RequestSize,
	TMap<uint32, TArray<FBuddyBlock>>& InOutFreeLists,
	FBuddyBlock& OutBlock) const
{
	if (RequestSize == 0 || RequestSize > Size)
	{
		return false;
	}

	const uint32 TargetSize = QuantizeBlockSize(RequestSize);
	uint32 SearchSize = TargetSize;
	while (SearchSize <= Size)
	{
		auto It = InOutFreeLists.find(SearchSize);
		if (It != InOutFreeLists.end() && !It->second.empty())
		{
			FBuddyBlock Block = It->second.back();
			It->second.pop_back();

			while (Block.Size > TargetSize)
			{
				const uint32 ChildSize = Block.Size / 2u;
				const FBuddyBlock Child00 = { Block.X,             Block.Y,             ChildSize, Block.ArrayIndex };
				const FBuddyBlock Child10 = { Block.X + ChildSize, Block.Y,             ChildSize, Block.ArrayIndex };
				const FBuddyBlock Child01 = { Block.X,             Block.Y + ChildSize, ChildSize, Block.ArrayIndex };
				const FBuddyBlock Child11 = { Block.X + ChildSize, Block.Y + ChildSize, ChildSize, Block.ArrayIndex };

				InOutFreeLists[ChildSize].push_back(Child10);
				InOutFreeLists[ChildSize].push_back(Child01);
				InOutFreeLists[ChildSize].push_back(Child11);
				Block = Child00;
			}

			OutBlock = Block;
			return true;
		}

		if (SearchSize > Size / 2u)
		{
			break;
		}
		SearchSize <<= 1u;
	}

	return false;
}

bool FBuddyTexturePoolAllocator::ContainsFreeBlock(const FBuddyBlock& Block) const
{
	auto It = FreeLists.find(Block.Size);
	if (It == FreeLists.end())
	{
		return false;
	}

	for (const FBuddyBlock& Candidate : It->second)
	{
		if (AreSameBlock(Candidate, Block))
		{
			return true;
		}
	}
	return false;
}

bool FBuddyTexturePoolAllocator::RemoveFreeBlock(const FBuddyBlock& Block)
{
	auto It = FreeLists.find(Block.Size);
	if (It == FreeLists.end())
	{
		return false;
	}

	TArray<FBuddyBlock>& Blocks = It->second;
	for (auto BlockIt = Blocks.begin(); BlockIt != Blocks.end(); ++BlockIt)
	{
		if (AreSameBlock(*BlockIt, Block))
		{
			Blocks.erase(BlockIt);
			return true;
		}
	}
	return false;
}

bool FBuddyTexturePoolAllocator::AreBuddiesFreeForMerge(const FBuddyBlock& Block) const
{
	if (Block.Size == 0 || Block.Size >= Size)
	{
		return false;
	}

	const TArray<FBuddyBlock> Siblings = MakeSiblingBlocks(Block);
	for (const FBuddyBlock& Sibling : Siblings)
	{
		if (!AreSameBlock(Sibling, Block) && !ContainsFreeBlock(Sibling))
		{
			return false;
		}
	}
	return true;
}

void FBuddyTexturePoolAllocator::RemoveBuddiesForMerge(const FBuddyBlock& Block)
{
	const TArray<FBuddyBlock> Siblings = MakeSiblingBlocks(Block);
	for (const FBuddyBlock& Sibling : Siblings)
	{
		if (!AreSameBlock(Sibling, Block))
		{
			RemoveFreeBlock(Sibling);
		}
	}
}

bool FBuddyTexturePoolAllocator::AreSameBlock(const FBuddyBlock& A, const FBuddyBlock& B) const
{
	return A.X == B.X
		&& A.Y == B.Y
		&& A.Size == B.Size
		&& A.ArrayIndex == B.ArrayIndex;
}

TArray<FBuddyTexturePoolAllocator::FBuddyBlock> FBuddyTexturePoolAllocator::MakeSiblingBlocks(const FBuddyBlock& Block) const
{
	const uint32 ParentSize = Block.Size * 2u;
	const uint32 ParentX = (Block.X / ParentSize) * ParentSize;
	const uint32 ParentY = (Block.Y / ParentSize) * ParentSize;

	return TArray<FBuddyBlock>
	{
		{ ParentX,              ParentY,              Block.Size, Block.ArrayIndex },
		{ ParentX + Block.Size, ParentY,              Block.Size, Block.ArrayIndex },
		{ ParentX,              ParentY + Block.Size, Block.Size, Block.ArrayIndex },
		{ ParentX + Block.Size, ParentY + Block.Size, Block.Size, Block.ArrayIndex }
	};
}

FAtlasDebugRect FBuddyTexturePoolAllocator::MakeDebugRect(const FBuddyBlock& Block, bool bAllocated, uint32 HandleIndex) const
{
	FAtlasDebugRect DebugRect = {};
	DebugRect.X = Block.X;
	DebugRect.Y = Block.Y;
	DebugRect.W = Block.Size;
	DebugRect.H = Block.Size;
	DebugRect.ArrayIndex = Block.ArrayIndex;
	DebugRect.HandleIndex = HandleIndex;
	DebugRect.bAllocated = bAllocated;
	return DebugRect;
}
