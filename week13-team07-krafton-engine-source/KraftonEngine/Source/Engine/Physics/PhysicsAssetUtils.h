#pragma once

#include "Core/Types/CoreTypes.h"

class UPhysicsAsset;
class USkeletalMesh;

enum class EPhysicsAssetAutoGenerateVertexWeightingType : uint8
{
	DominantWeight,
	AnyWeight
};

enum class EPhysicsAssetAutoGeneratePrimitiveType : uint8
{
	Capsule,
	Box,
	Sphere,
	Auto
};

enum class EPhysicsAssetAutoGenerateOrientMethod : uint8
{
	BoneAxis,
	PCA,
	Hybrid
};

struct FPhysicsAssetAutoGenerateOptions
{
	EPhysicsAssetAutoGenerateVertexWeightingType VertexWeightingType = EPhysicsAssetAutoGenerateVertexWeightingType::DominantWeight;
	EPhysicsAssetAutoGeneratePrimitiveType PrimitiveType = EPhysicsAssetAutoGeneratePrimitiveType::Auto;
	EPhysicsAssetAutoGenerateOrientMethod OrientMethod = EPhysicsAssetAutoGenerateOrientMethod::Hybrid;

	float MinBoneSize = 5.0f;
	float MinWeight = 0.05f;
	int32 MinVertexCount = 6;

	float RadiusScale = 1.05f;
	float LengthScale = 1.0f;
	float BoxExtentScale = 1.05f;

	bool bCreateConstraints = true;
	bool bDisableAdjacentCollision = true;
	bool bClearExistingBodies = true;
};

struct FPhysicsAssetAutoGenerateResult
{
	int32 BodiesCreated = 0;
	int32 BodiesSkipped = 0;
	int32 ConstraintsCreated = 0;
};

class FPhysicsAssetUtils
{
public:
	static bool AutoGenerateBodiesAndConstraints(
		UPhysicsAsset* PhysicsAsset,
		USkeletalMesh* SkeletalMesh,
		const FPhysicsAssetAutoGenerateOptions& Options,
		FPhysicsAssetAutoGenerateResult* OutResult = nullptr);
};
