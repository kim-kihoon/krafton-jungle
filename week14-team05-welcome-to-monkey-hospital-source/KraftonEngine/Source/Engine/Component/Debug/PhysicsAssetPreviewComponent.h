#pragma once

#include "Component/PrimitiveComponent.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "Physics/PhysicsAssetTypes.h"
#include "Render/Resource/Buffer.h"
#include "Render/Types/VertexTypes.h"

#include "Source/Engine/Component/Debug/PhysicsAssetPreviewComponent.generated.h"

class UPhysicsAsset;
class USkeletalMeshComponent;
struct FPhysicsAssetPreviewPoseCache;
struct ID3D11Device;

UCLASS()
class UPhysicsAssetPreviewComponent : public UPrimitiveComponent
{
public:
	GENERATED_BODY()

	UPhysicsAssetPreviewComponent();
	~UPhysicsAssetPreviewComponent() override;

	FPrimitiveSceneProxy* CreateSceneProxy() override;
	FMeshBuffer* GetMeshBuffer() const override;
	FMeshDataView GetMeshDataView() const override;
	bool LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult) override;
	void UpdateWorldAABB() const override;
	bool SupportsOutline() const override { return false; }

	static int32 EncodeSelectionFaceIndex(int32 BodyIndex, int32 ShapeIndex);
	static int32 EncodeConstraintFaceIndex(int32 ConstraintIndex);
	static bool DecodeSelectionFaceIndex(int32 FaceIndex, int32& OutBodyIndex, int32& OutShapeIndex);
	static bool DecodeConstraintFaceIndex(int32 FaceIndex, int32& OutConstraintIndex);

	void UpdatePreview(
		UPhysicsAsset* InPhysicsAsset,
		USkeletalMeshComponent* InPreviewComponent,
		int32 InSelectedBodyIndex,
		int32 InSelectedShapeIndex,
		int32 InSelectedConstraintIndex,
		bool bInShowBodies,
			bool bInShowConstraints,
			bool bInShowConstraintLimitAngles,
			bool bInShowConstraintLimitSurfaces,
			bool bInShowOnlySelectedConstraintLimitSurfaces,
			ID3D11Device* Device);

	void ClearPreview(ID3D11Device* Device = nullptr);

private:
	void RebuildPreviewMesh();
	void UploadPreviewMesh(ID3D11Device* Device);
	bool IsPreviewBuildCacheCurrent(
		UPhysicsAsset* InPhysicsAsset,
		USkeletalMeshComponent* InPreviewComponent,
		int32 InSelectedBodyIndex,
		int32 InSelectedShapeIndex,
		int32 InSelectedConstraintIndex,
		bool bInShowBodies,
		bool bInShowConstraints,
		bool bInShowConstraintLimitAngles,
		bool bInShowConstraintLimitSurfaces,
			bool bInShowOnlySelectedConstraintLimitSurfaces,
			uint64 InSkinnedRevision,
			uint64 InComponentWorldHash,
			uint64 InAssetHash) const;
	void StorePreviewBuildCache(
		UPhysicsAsset* InPhysicsAsset,
		USkeletalMeshComponent* InPreviewComponent,
		int32 InSelectedBodyIndex,
		int32 InSelectedShapeIndex,
		int32 InSelectedConstraintIndex,
		bool bInShowBodies,
		bool bInShowConstraints,
		bool bInShowConstraintLimitAngles,
		bool bInShowConstraintLimitSurfaces,
			bool bInShowOnlySelectedConstraintLimitSurfaces,
			uint64 InSkinnedRevision,
			uint64 InComponentWorldHash,
			uint64 InAssetHash);
	void InvalidatePreviewBuildCache();

	void AppendBox(const FTransform& ShapeWorld, const FVector& HalfExtent, const FVector4& Color);
	void AppendSphere(const FTransform& ShapeWorld, float Radius, const FVector4& Color);
	void AppendCapsuleZAxis(const FTransform& ShapeWorld, float Radius, float HalfHeight, const FVector4& Color);
	void AppendConstraintLimitSurfaces(int32 ConstraintIndex, const FPhysicsAssetPreviewPoseCache& PoseCache);
	void AppendSwingLimitSurface(const FTransform& ParentFrameWorld, const struct FConstraintLimitDesc& Limits, float Radius, const FVector4& Color);
	void AppendTwistLimitSurface(const FTransform& ParentFrameWorld, const struct FConstraintLimitDesc& Limits, float Radius, const FVector4& Color);

	uint32 AddVertexWorld(const FVector& WorldPosition, const FVector4& Color);
	void ExpandBoundsWorld(const FVector& WorldPosition);

private:
	TWeakObjectPtr<UPhysicsAsset> PhysicsAsset;
	TWeakObjectPtr<USkeletalMeshComponent> PreviewComponent;

	int32 SelectedBodyIndex = -1;
	int32 SelectedShapeIndex = -1;
	int32 SelectedConstraintIndex = -1;
	bool bShowBodies = true;
	bool bShowConstraints = true;
	bool bShowConstraintLimitAngles = true;
	bool bShowConstraintLimitSurfaces = true;
	bool bShowOnlySelectedConstraintLimitSurfaces = false;

	TMeshData<FVertex> PreviewMeshData;
	FMeshBuffer PreviewMeshBuffer;

	FBoundingBox CachedWorldBounds;
	bool bHasPreviewBounds = false;

	UPhysicsAsset* CachedBuildPhysicsAsset = nullptr;
	USkeletalMeshComponent* CachedBuildPreviewComponent = nullptr;
	int32 CachedBuildSelectedBodyIndex = -1;
	int32 CachedBuildSelectedShapeIndex = -1;
	int32 CachedBuildSelectedConstraintIndex = -1;
	bool bCachedBuildShowBodies = false;
	bool bCachedBuildShowConstraints = false;
	bool bCachedBuildShowConstraintLimitAngles = false;
	bool bCachedBuildShowConstraintLimitSurfaces = false;
	bool bCachedBuildShowOnlySelectedConstraintLimitSurfaces = false;
	uint64 CachedBuildSkinnedRevision = 0;
	uint64 CachedBuildComponentWorldHash = 0;
	uint64 CachedBuildAssetHash = 0;
	bool bHasValidPreviewBuildCache = false;
};
