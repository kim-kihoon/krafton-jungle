#pragma once

#include "ShapeSceneProxy.h"

class UBoneDebugComponent;

class FBoneDebugSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FBoneDebugSceneProxy(UBoneDebugComponent* InComponent);
	~FBoneDebugSceneProxy() override;

	void UpdateTransform() override;

	const TArray<FWireLine>& GetCachedLines() const { return CachedLines; }
	const TArray<FWireLine>& GetCachedParentBoneLines() const { return CachedParentBoneLines; }
	const TArray<FWireLine>& GetCachedSocketLines() const { return CachedSocketLines; }

	const FVector4& GetBoneColor() const { return BoneColor; }
	const FVector4& GetParentBoneColor() const { return ParentBoneColor; }
	const FVector4& GetSocketColor() const { return SocketColor; }

private:
	void RebuildLines();

private:
	TArray<FWireLine> CachedLines;
	TArray<FWireLine> CachedParentBoneLines;
	TArray<FWireLine> CachedSocketLines;

	FVector4 BoneColor;
	FVector4 ParentBoneColor;
	FVector4 SocketColor;
};
