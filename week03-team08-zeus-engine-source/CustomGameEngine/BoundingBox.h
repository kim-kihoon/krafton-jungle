#pragma once
#include "EngineTypes.h"
#include "Math/Vector.h"

struct FBoundingBox
{
	float MinX, MinY, MinZ;
	float MaxX, MaxY, MaxZ;

	static FBoundingBox FromPoints(TArray<FVector>& vertices)
	{
		FBoundingBox box;
		if (vertices.empty())
		{
			box.MinX = box.MinY = box.MinZ = 0.0f;
			box.MaxX = box.MaxY = box.MaxZ = 0.0f;
			return box;
		}

		box.MinX = box.MaxX = vertices[0].x;
		box.MinY = box.MaxY = vertices[0].y;
		box.MinZ = box.MaxZ = vertices[0].z;
		for (const auto& vertex : vertices)
		{
			if (vertex.x < box.MinX) box.MinX = vertex.x;
			if (vertex.y < box.MinY) box.MinY = vertex.y;
			if (vertex.z < box.MinZ) box.MinZ = vertex.z;
			if (vertex.x > box.MaxX) box.MaxX = vertex.x;
			if (vertex.y > box.MaxY) box.MaxY = vertex.y;
			if (vertex.z > box.MaxZ) box.MaxZ = vertex.z;
		}
		return box;
	}

	TArray<FVector> GetCorners() const
	{
		TArray<FVector> corners(8);
		corners[0] = FVector(MinX, MinY, MinZ);
		corners[1] = FVector(MaxX, MinY, MinZ);
		corners[2] = FVector(MinX, MaxY, MinZ);
		corners[3] = FVector(MaxX, MaxY, MinZ);
		corners[4] = FVector(MinX, MinY, MaxZ);
		corners[5] = FVector(MaxX, MinY, MaxZ);
		corners[6] = FVector(MinX, MaxY, MaxZ);
		corners[7] = FVector(MaxX, MaxY, MaxZ);
		return corners;
	}

	FBoundingBox Transform(const FMatrix& mat) const
	{
		FVector localCenter = FVector((MaxX + MinX) * 0.5f, (MaxY + MinY) * 0.5f, (MaxZ + MinZ) * 0.5f);
		FVector Extent = FVector((MaxX - MinX) * 0.5f, (MaxY - MinY) * 0.5f, (MaxZ - MinZ) * 0.5f);

		FVector NewCenter = mat.TransformPoint(localCenter);

		FVector NewExtent(
			std::abs(mat.m[0][0]) * Extent.x + std::abs(mat.m[1][0]) * Extent.y + std::abs(mat.m[2][0]) * Extent.z,
			std::abs(mat.m[0][1]) * Extent.x + std::abs(mat.m[1][1]) * Extent.y + std::abs(mat.m[2][1]) * Extent.z,
			std::abs(mat.m[0][2]) * Extent.x + std::abs(mat.m[1][2]) * Extent.y + std::abs(mat.m[2][2]) * Extent.z
		);

		// 4. 새 중심점에서 새 반경을 더하고 빼서 완벽한 World Min/Max를 도출합니다!
		FBoundingBox Result;
		Result.MinX = NewCenter.x - NewExtent.x;
		Result.MaxX = NewCenter.x + NewExtent.x;
		Result.MinY = NewCenter.y - NewExtent.y;
		Result.MaxY = NewCenter.y + NewExtent.y;
		Result.MinZ = NewCenter.z - NewExtent.z;
		Result.MaxZ = NewCenter.z + NewExtent.z;

		return Result;
	}

	FBoundingBox EllipsoidTransform(const FMatrix& mat) const
	{
		float radius = (MaxX - MinX) * 0.5f;
		FVector localCenter = FVector((MaxX + MinX) * 0.5f, (MaxY + MinY) * 0.5f, (MaxZ + MinZ) * 0.5f);
		FVector Extent = FVector((MaxX - MinX) * 0.5f, (MaxY - MinY) * 0.5f, (MaxZ - MinZ) * 0.5f);

		FVector NewCenter = mat.TransformPoint(localCenter);

		float extentX = radius * std::sqrt(mat.m[0][0] * mat.m[0][0] + mat.m[1][0] * mat.m[1][0] + mat.m[2][0] * mat.m[2][0]);
		float extentY = radius * std::sqrt(mat.m[0][1] * mat.m[0][1] + mat.m[1][1] * mat.m[1][1] + mat.m[2][1] * mat.m[2][1]);
		float extentZ = radius * std::sqrt(mat.m[0][2] * mat.m[0][2] + mat.m[1][2] * mat.m[1][2] + mat.m[2][2] * mat.m[2][2]);

		FBoundingBox Result;
		Result.MinX = NewCenter.x - extentX;
		Result.MaxX = NewCenter.x + extentX;
		Result.MinY = NewCenter.y - extentY;
		Result.MaxY = NewCenter.y + extentY;
		Result.MinZ = NewCenter.z - extentZ;
		Result.MaxZ = NewCenter.z + extentZ;

		return Result;
	}
};

