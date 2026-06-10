#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"

#include <memory>

struct FSkeletalMesh;
struct FVertexPNCTT;
struct FClothCollisionSectionResult;

struct FClothWorldForceContext
{
    FVector WorldGravity = FVector(0.0f, 0.0f, -9.81f);
    FVector WorldWindVelocity = FVector::ZeroVector;
    bool bHasWorldWindVelocity = false;
};

struct FClothDebugRuntimeConfig
{
    float TeleportDistanceThreshold = 300.0f;
    float TeleportRotationThresholdDegrees = 45.0f;
    bool bResetOnScaleChange = true;
};

class FSkeletalClothRuntime
{
public:
    FSkeletalClothRuntime();
    ~FSkeletalClothRuntime();

    FSkeletalClothRuntime(const FSkeletalClothRuntime&) = delete;
    FSkeletalClothRuntime& operator=(const FSkeletalClothRuntime&) = delete;

    void Reset();
    bool Tick(
        const FSkeletalMesh& Asset,
        float DeltaTime,
        TArray<FVertexPNCTT>& InOutSkinnedVertices,
        const FMatrix& ComponentWorldMatrix,
        const FClothWorldForceContext& ForceContext,
        const TArray<FClothCollisionSectionResult>* CollisionResults = nullptr);
    void CopyCollisionDebugStats(TArray<FClothCollisionSectionResult>& InOutCollisionResults) const;
    bool IsActive() const;

    static FClothDebugRuntimeConfig& GetMutableDebugRuntimeConfig();
    static const FClothDebugRuntimeConfig& GetDebugRuntimeConfig();

private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
};
