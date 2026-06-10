#pragma once

#include "Core/Types/CoreTypes.h"

class USkeletalMesh;

class FAssetFactory
{
public:
	static bool CreateFloatCurve(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath);
	static bool CreateCameraShake(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath);
	static bool CreateAnimGraph(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath);
	static bool CreateParticleSystem(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath);
	static bool CreateVectorField(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath, int32 SizeX = 16, int32 SizeY = 16, int32 SizeZ = 16);
	static bool CreatePhysicsAssetForSkeletalMesh(const FString& DirectoryPath, const FString& AssetName, const USkeletalMesh* SkeletalMesh, FString& OutCreatedPath);
	static bool CreateMaterial(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath);
	static bool CreateLuaBlueprint(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath);
};
