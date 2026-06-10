#include "Cloth/SkeletalClothRuntime.h"

#include "Cloth/ClothCollisionTypes.h"
#include "Cloth/NvClothBackend.h"
#include "Core/Logging/Log.h"
#include "Math/MathUtils.h"
#include "Math/Quat.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Render/Types/VertexTypes.h"

#include <NvCloth/Cloth.h>
#include <NvCloth/Fabric.h>
#include <NvCloth/Factory.h>
#include <NvCloth/PhaseConfig.h>
#include <NvCloth/Range.h>
#include <NvCloth/Solver.h>
#include <NvClothExt/ClothFabricCooker.h>
#include <NvClothExt/ClothMeshDesc.h>
#include <foundation/PxVec3.h>
#include <foundation/PxVec4.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float ClothDefaultInvMass = 1.0f;
    constexpr float ClothMinDeltaTime = 1.0e-5f;
    constexpr float ClothFixedStep = 1.0f / 60.0f;
    constexpr float ClothMaxFrameDelta = 1.0f / 15.0f;
    constexpr int32 ClothMaxSubsteps = 4;
    constexpr float ClothScaleTolerance = 1.0e-4f;
    constexpr float ClothMinFluidDensity = 1.0e-6f;

    FClothDebugRuntimeConfig GClothDebugRuntimeConfig;

    float Clamp01(float Value)
    {
        return std::max(0.0f, std::min(1.0f, Value));
    }

    physx::PxVec3 ToPxVec3(const FVector& Value)
    {
        return physx::PxVec3(Value.X, Value.Y, Value.Z);
    }

    physx::PxVec4 ToPxParticle(const FVector& Position, float InvMass)
    {
        return physx::PxVec4(Position.X, Position.Y, Position.Z, InvMass);
    }

    FVector ToFVector(const physx::PxVec4& Value)
    {
        return FVector(Value.x, Value.y, Value.z);
    }

    bool IsFinite(float Value)
    {
        return std::isfinite(Value);
    }

    bool IsFiniteVector(const FVector& Value)
    {
        return IsFinite(Value.X) && IsFinite(Value.Y) && IsFinite(Value.Z);
    }

    bool IsNearlyEqual(float A, float B, float Tolerance = ClothScaleTolerance)
    {
        return std::fabs(A - B) <= Tolerance;
    }

    bool IsUniformScale(const FVector& Scale)
    {
        return IsNearlyEqual(Scale.X, Scale.Y) && IsNearlyEqual(Scale.Y, Scale.Z);
    }

    bool IsValidClothScale(const FVector& Scale)
    {
        return IsFiniteVector(Scale) &&
            Scale.X > ClothScaleTolerance &&
            Scale.Y > ClothScaleTolerance &&
            Scale.Z > ClothScaleTolerance &&
            IsUniformScale(Scale);
    }

    bool IsPositiveFiniteVector(const FVector& Value)
    {
        return IsFiniteVector(Value) &&
            Value.X > 0.0f &&
            Value.Y > 0.0f &&
            Value.Z > 0.0f;
    }

    bool HasScaleChanged(const FVector& A, const FVector& B)
    {
        return !IsNearlyEqual(A.X, B.X) ||
            !IsNearlyEqual(A.Y, B.Y) ||
            !IsNearlyEqual(A.Z, B.Z);
    }

    float GetRotationDeltaDegrees(const FMatrix& A, const FMatrix& B)
    {
        const FQuat QA = A.ToQuat().GetNormalized();
        const FQuat QB = B.ToQuat().GetNormalized();
        const float Dot = std::clamp(
            std::fabs(QA.X * QB.X + QA.Y * QB.Y + QA.Z * QB.Z + QA.W * QB.W),
            0.0f,
            1.0f);
        return 2.0f * std::acos(Dot) * FMath::RadToDeg;
    }

    bool HasPositivePaintRadius(const FSkeletalClothData& ClothData)
    {
        for (float MaxDistance : ClothData.Paint.MaxDistanceValues)
        {
            if (MaxDistance > 0.0f)
            {
                return true;
            }
        }
        return false;
    }

    float GetPaintMaxDistance(const FSkeletalClothData& ClothData, uint32 ParticleIndex)
    {
        if (ParticleIndex < ClothData.Paint.MaxDistanceValues.size())
        {
            return std::max(0.0f, ClothData.Paint.MaxDistanceValues[ParticleIndex]);
        }
        return 0.0f;
    }
}

struct FSkeletalClothRuntime::FImpl
{
    struct FSectionRuntime
    {
        const FSkeletalClothData* SourceData = nullptr;
        uint32 LODIndex = 0;
        int32 SectionIndex = -1;
        uint32 SourceVertexCount = 0;
        uint32 SourceIndexCount = 0;
        nv::cloth::Fabric* Fabric = nullptr;
        nv::cloth::Cloth* Cloth = nullptr;
        TArray<physx::PxVec3> CookedPositions;
        TArray<float> InvMasses;
        TArray<uint32_t> CookedIndices;
        TArray<physx::PxVec4> CollisionSpheres;
        TArray<uint32_t> CollisionCapsuleSphereIndices;
        TArray<physx::PxVec4> CollisionPlanes;
        TArray<uint32_t> CollisionConvexMasks;
        TArray<FVector> NormalSums;
        TArray<FVector> TangentSums;
        FClothCollisionDebugStats CollisionStats;

        void Destroy(nv::cloth::Solver* Solver)
        {
            if (Solver && Cloth)
            {
                Solver->removeCloth(Cloth);
            }

            delete Cloth;
            Cloth = nullptr;

            if (Fabric)
            {
                Fabric->decRefCount();
                Fabric = nullptr;
            }

            SourceData = nullptr;
            CookedPositions.clear();
            InvMasses.clear();
            CookedIndices.clear();
            CollisionSpheres.clear();
            CollisionCapsuleSphereIndices.clear();
            CollisionPlanes.clear();
            CollisionConvexMasks.clear();
            NormalSums.clear();
            TangentSums.clear();
            CollisionStats.Reset();
        }
    };

    FNvClothCpuBackend Backend;
    std::unique_ptr<IClothSolver> SolverOwner;
    TArray<FSectionRuntime> Sections;
    const FSkeletalMesh* BuiltAsset = nullptr;
    float AccumulatedSimulationTime = 0.0f;
    FMatrix PreviousComponentWorldMatrix = FMatrix::Identity;
    FMatrix CurrentComponentWorldMatrix = FMatrix::Identity;
    FVector LastComponentScale = FVector::OneVector;
    bool bHasComponentTransformHistory = false;
    bool bLoggedGpuSkip = false;

    ~FImpl()
    {
        Reset();
    }

    nv::cloth::Solver* GetNativeSolver() const
    {
        FNvClothSolver* Solver = dynamic_cast<FNvClothSolver*>(SolverOwner.get());
        return Solver ? Solver->GetNativeSolver() : nullptr;
    }

    void Reset()
    {
        nv::cloth::Solver* Solver = GetNativeSolver();
        for (FSectionRuntime& Section : Sections)
        {
            Section.Destroy(Solver);
        }
        Sections.clear();
        SolverOwner.reset();
        Backend.Shutdown();
        BuiltAsset = nullptr;
        AccumulatedSimulationTime = 0.0f;
        PreviousComponentWorldMatrix = FMatrix::Identity;
        CurrentComponentWorldMatrix = FMatrix::Identity;
        LastComponentScale = FVector::OneVector;
        bHasComponentTransformHistory = false;
        bLoggedGpuSkip = false;
    }

    bool PrepareComponentTransformHistory(const FMatrix& ComponentWorldMatrix)
    {
        const FVector ComponentScale = ComponentWorldMatrix.GetScale();
        if (!IsValidClothScale(ComponentScale))
        {
            Reset();
            return false;
        }

        if (!bHasComponentTransformHistory)
        {
            PreviousComponentWorldMatrix = ComponentWorldMatrix;
            CurrentComponentWorldMatrix = ComponentWorldMatrix;
            LastComponentScale = ComponentScale;
            bHasComponentTransformHistory = true;
            AccumulatedSimulationTime = 0.0f;
            return true;
        }

        if (HasScaleChanged(ComponentScale, LastComponentScale))
        {
            if (GClothDebugRuntimeConfig.bResetOnScaleChange)
            {
                Reset();
                InitializeComponentTransformHistory(ComponentWorldMatrix);
                return true;
            }
        }

        const float TeleportDistanceThreshold = std::max(0.0f, GClothDebugRuntimeConfig.TeleportDistanceThreshold);
        const float TeleportRotationThresholdDegrees =
            std::max(0.0f, GClothDebugRuntimeConfig.TeleportRotationThresholdDegrees);
        const bool bTeleportByDistance =
            TeleportDistanceThreshold > 0.0f &&
            FVector::Distance(CurrentComponentWorldMatrix.GetLocation(), ComponentWorldMatrix.GetLocation()) >
                TeleportDistanceThreshold;
        const bool bTeleportByRotation =
            TeleportRotationThresholdDegrees > 0.0f &&
            GetRotationDeltaDegrees(CurrentComponentWorldMatrix, ComponentWorldMatrix) >
                TeleportRotationThresholdDegrees;
        if (bTeleportByDistance || bTeleportByRotation)
        {
            Reset();
            InitializeComponentTransformHistory(ComponentWorldMatrix);
            return true;
        }

        PreviousComponentWorldMatrix = CurrentComponentWorldMatrix;
        CurrentComponentWorldMatrix = ComponentWorldMatrix;
        LastComponentScale = ComponentScale;
        return true;
    }

    void InitializeComponentTransformHistory(const FMatrix& ComponentWorldMatrix)
    {
        PreviousComponentWorldMatrix = ComponentWorldMatrix;
        CurrentComponentWorldMatrix = ComponentWorldMatrix;
        LastComponentScale = ComponentWorldMatrix.GetScale();
        bHasComponentTransformHistory = true;
        AccumulatedSimulationTime = 0.0f;
    }

    FVector TransformWorldVectorToComponentLocal(const FVector& WorldVector) const
    {
        return CurrentComponentWorldMatrix.GetInverse().TransformVector(WorldVector);
    }

    void UpdateWorldForces(const FClothWorldForceContext& ForceContext)
    {
        for (FSectionRuntime& Section : Sections)
        {
            if (!Section.Cloth || !Section.SourceData)
            {
                continue;
            }

            const FSkeletalClothData& ClothData = *Section.SourceData;
            const FVector LocalGravity = TransformWorldVectorToComponentLocal(ForceContext.WorldGravity);
            Section.Cloth->setGravity(ToPxVec3(LocalGravity * ClothData.Config.GravityScale));

            const bool bApplyWind = ForceContext.bHasWorldWindVelocity &&
                !ForceContext.WorldWindVelocity.IsNearlyZero();
            Section.Cloth->clearParticleAccelerations();

            const float WindScale = std::max(0.0f, ClothData.Config.WindScale);
            const float DragCoefficient = Clamp01(ClothData.Config.DragCoefficient);
            const float LiftCoefficient = Clamp01(ClothData.Config.LiftCoefficient);
            if (bApplyWind && WindScale > 0.0f && (DragCoefficient > 0.0f || LiftCoefficient > 0.0f))
            {
                const FVector LocalWindVelocity =
                    TransformWorldVectorToComponentLocal(ForceContext.WorldWindVelocity) * WindScale;
                Section.Cloth->setWindVelocity(ToPxVec3(LocalWindVelocity));
                Section.Cloth->setDragCoefficient(DragCoefficient);
                Section.Cloth->setLiftCoefficient(LiftCoefficient);
                Section.Cloth->setFluidDensity(std::max(ClothMinFluidDensity, ClothData.Config.FluidDensity));
            }
            else
            {
                Section.Cloth->setWindVelocity(physx::PxVec3(0.0f, 0.0f, 0.0f));
                Section.Cloth->setDragCoefficient(0.0f);
                Section.Cloth->setLiftCoefficient(0.0f);
            }
        }
    }

    void ClearCollision(FSectionRuntime& Section)
    {
        if (!Section.Cloth)
        {
            return;
        }

        const uint32_t ExistingConvexes = Section.Cloth->getNumConvexes();
        if (ExistingConvexes > 0)
        {
            Section.Cloth->setConvexes(
                nv::cloth::Range<const uint32_t>(),
                0,
                ExistingConvexes);
        }

        const uint32_t ExistingPlanes = Section.Cloth->getNumPlanes();
        if (ExistingPlanes > 0)
        {
            Section.Cloth->setPlanes(
                nv::cloth::Range<const physx::PxVec4>(),
                0,
                ExistingPlanes);
        }

        const uint32_t ExistingCapsules = Section.Cloth->getNumCapsules();
        if (ExistingCapsules > 0)
        {
            Section.Cloth->setCapsules(
                nv::cloth::Range<const uint32_t>(),
                0,
                ExistingCapsules);
        }

        const uint32_t ExistingSpheres = Section.Cloth->getNumSpheres();
        if (ExistingSpheres > 0)
        {
            Section.Cloth->setSpheres(
                nv::cloth::Range<const physx::PxVec4>(),
                0,
                ExistingSpheres);
        }
        Section.CollisionStats.Reset();
    }

    void UploadCollision(
        FSectionRuntime& Section,
        const FClothCollisionGatherResult* CollisionResult)
    {
        if (!Section.Cloth)
        {
            return;
        }

        if (!CollisionResult)
        {
            ClearCollision(Section);
            return;
        }

        TArray<physx::PxVec4>& Spheres = Section.CollisionSpheres;
        TArray<uint32_t>& CapsuleSphereIndices = Section.CollisionCapsuleSphereIndices;
        TArray<physx::PxVec4>& Planes = Section.CollisionPlanes;
        TArray<uint32_t>& ConvexMasks = Section.CollisionConvexMasks;
        Spheres.clear();
        CapsuleSphereIndices.clear();
        Planes.clear();
        ConvexMasks.clear();
        const FClothCollisionPrimitiveSet& Primitives = CollisionResult->SelectedPrimitives;
        Spheres.reserve(Primitives.Spheres.size() + Primitives.Capsules.size() * 2);
        CapsuleSphereIndices.reserve(Primitives.Capsules.size() * 2);
        Planes.reserve(Primitives.Boxes.size() * 6);
        ConvexMasks.reserve(Primitives.Boxes.size());

        for (const FClothCollisionSphere& Sphere : Primitives.Spheres)
        {
            if (Sphere.Radius <= 0.0f || !IsFiniteVector(Sphere.LocalCenter))
            {
                continue;
            }
            Spheres.push_back(physx::PxVec4(
                Sphere.LocalCenter.X,
                Sphere.LocalCenter.Y,
                Sphere.LocalCenter.Z,
                Sphere.Radius));
        }

        for (const FClothCollisionCapsule& Capsule : Primitives.Capsules)
        {
            if (Capsule.Radius <= 0.0f ||
                !IsFiniteVector(Capsule.LocalPoint0) ||
                !IsFiniteVector(Capsule.LocalPoint1))
            {
                continue;
            }

            const uint32_t FirstSphereIndex = static_cast<uint32_t>(Spheres.size());
            Spheres.push_back(physx::PxVec4(
                Capsule.LocalPoint0.X,
                Capsule.LocalPoint0.Y,
                Capsule.LocalPoint0.Z,
                Capsule.Radius));
            Spheres.push_back(physx::PxVec4(
                Capsule.LocalPoint1.X,
                Capsule.LocalPoint1.Y,
                Capsule.LocalPoint1.Z,
                Capsule.Radius));
            CapsuleSphereIndices.push_back(FirstSphereIndex);
            CapsuleSphereIndices.push_back(FirstSphereIndex + 1);
        }

        for (const FClothCollisionBox& Box : Primitives.Boxes)
        {
            if (!IsFiniteVector(Box.LocalTransform.Location) || !IsPositiveFiniteVector(Box.HalfExtent))
            {
                continue;
            }

            const FVector Center = Box.LocalTransform.Location;
            const FVector AxisX = Box.LocalTransform.Rotation.RotateVector(FVector(1.0f, 0.0f, 0.0f)).Normalized();
            const FVector AxisY = Box.LocalTransform.Rotation.RotateVector(FVector(0.0f, 1.0f, 0.0f)).Normalized();
            const FVector AxisZ = Box.LocalTransform.Rotation.RotateVector(FVector(0.0f, 0.0f, 1.0f)).Normalized();
            if (!IsFiniteVector(AxisX) || !IsFiniteVector(AxisY) || !IsFiniteVector(AxisZ))
            {
                continue;
            }

            const uint32_t FirstPlaneIndex = static_cast<uint32_t>(Planes.size());
            if (FirstPlaneIndex + 6 > 32)
            {
                break;
            }

            const auto AddPlane = [&Planes](const FVector& Normal, const FVector& Point)
            {
                const float D = -Normal.Dot(Point);
                Planes.push_back(physx::PxVec4(Normal.X, Normal.Y, Normal.Z, D));
            };

            AddPlane(AxisX, Center + AxisX * Box.HalfExtent.X);
            AddPlane(AxisX * -1.0f, Center - AxisX * Box.HalfExtent.X);
            AddPlane(AxisY, Center + AxisY * Box.HalfExtent.Y);
            AddPlane(AxisY * -1.0f, Center - AxisY * Box.HalfExtent.Y);
            AddPlane(AxisZ, Center + AxisZ * Box.HalfExtent.Z);
            AddPlane(AxisZ * -1.0f, Center - AxisZ * Box.HalfExtent.Z);

            uint32_t Mask = 0;
            for (uint32_t PlaneOffset = 0; PlaneOffset < 6; ++PlaneOffset)
            {
                Mask |= 1u << (FirstPlaneIndex + PlaneOffset);
            }
            ConvexMasks.push_back(Mask);
        }

        const uint32_t ExistingConvexes = Section.Cloth->getNumConvexes();
        if (ExistingConvexes > 0)
        {
            Section.Cloth->setConvexes(
                nv::cloth::Range<const uint32_t>(),
                0,
                ExistingConvexes);
        }

        if (!Planes.empty())
        {
            Section.Cloth->setPlanes(
                nv::cloth::Range<const physx::PxVec4>(Planes.data(), Planes.data() + Planes.size()),
                0,
                Section.Cloth->getNumPlanes());
        }
        else
        {
            const uint32_t ExistingPlanes = Section.Cloth->getNumPlanes();
            if (ExistingPlanes > 0)
            {
                Section.Cloth->setPlanes(
                    nv::cloth::Range<const physx::PxVec4>(),
                    0,
                    ExistingPlanes);
            }
        }

        if (!ConvexMasks.empty())
        {
            Section.Cloth->setConvexes(
                nv::cloth::Range<const uint32_t>(ConvexMasks.data(), ConvexMasks.data() + ConvexMasks.size()),
                0,
                Section.Cloth->getNumConvexes());
        }

        if (CapsuleSphereIndices.empty())
        {
            const uint32_t ExistingCapsules = Section.Cloth->getNumCapsules();
            if (ExistingCapsules > 0)
            {
                Section.Cloth->setCapsules(
                    nv::cloth::Range<const uint32_t>(),
                    0,
                    ExistingCapsules);
            }
        }

        if (!Spheres.empty())
        {
            Section.Cloth->setSpheres(
                nv::cloth::Range<const physx::PxVec4>(Spheres.data(), Spheres.data() + Spheres.size()),
                0,
                Section.Cloth->getNumSpheres());
        }
        else
        {
            const uint32_t ExistingSpheres = Section.Cloth->getNumSpheres();
            if (ExistingSpheres > 0)
            {
                Section.Cloth->setSpheres(
                    nv::cloth::Range<const physx::PxVec4>(),
                    0,
                    ExistingSpheres);
            }
        }

        if (!CapsuleSphereIndices.empty())
        {
            Section.Cloth->setCapsules(
                nv::cloth::Range<const uint32_t>(CapsuleSphereIndices.data(), CapsuleSphereIndices.data() + CapsuleSphereIndices.size()),
                0,
                Section.Cloth->getNumCapsules());
        }

        Section.CollisionStats = CollisionResult->Stats;
        Section.CollisionStats.UploadedSpheres = static_cast<uint32>(Spheres.size());
        Section.CollisionStats.UploadedCapsules = static_cast<uint32>(CapsuleSphereIndices.size() / 2);
        Section.CollisionStats.UploadedPlanes = static_cast<uint32>(Planes.size());
        Section.CollisionStats.UploadedConvexes = static_cast<uint32>(ConvexMasks.size());
    }

    const FClothCollisionGatherResult* FindCollisionResultForSection(
        const TArray<FClothCollisionSectionResult>* CollisionResults,
        const FSectionRuntime& Section) const
    {
        if (!CollisionResults)
        {
            return nullptr;
        }

        for (const FClothCollisionSectionResult& SectionResult : *CollisionResults)
        {
            if (SectionResult.LODIndex == Section.LODIndex &&
                SectionResult.SectionIndex == Section.SectionIndex)
            {
                return &SectionResult.GatherResult;
            }
        }
        return nullptr;
    }

    void CopyCollisionDebugStats(TArray<FClothCollisionSectionResult>& InOutCollisionResults) const
    {
        for (FClothCollisionSectionResult& SectionResult : InOutCollisionResults)
        {
            for (const FSectionRuntime& Section : Sections)
            {
                if (SectionResult.LODIndex == Section.LODIndex &&
                    SectionResult.SectionIndex == Section.SectionIndex)
                {
                    SectionResult.GatherResult.Stats = Section.CollisionStats;
                    break;
                }
            }
        }
    }

    bool ShouldBuildRuntimeCloth(const FSkeletalClothData& ClothData) const
    {
        return ClothData.bEnabled &&
            !ClothData.ParticleToRenderVertex.empty() &&
            !ClothData.ClothLocalIndices.empty() &&
            ClothData.ClothLocalIndices.size() % 3 == 0 &&
            HasPositivePaintRadius(ClothData);
    }

    bool HasRuntimeClothCandidate(const FSkeletalMesh& Asset) const
    {
        for (const FSkeletalClothLODData& LODData : Asset.ClothPayload.LODs)
        {
            for (const FSkeletalClothData& ClothData : LODData.Cloths)
            {
                if (ShouldBuildRuntimeCloth(ClothData))
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool MatchesBuiltPayload(const FSkeletalMesh& Asset) const
    {
        if (BuiltAsset != &Asset)
        {
            return false;
        }

        uint32 EnabledCount = 0;
        for (const FSkeletalClothLODData& LODData : Asset.ClothPayload.LODs)
        {
            for (const FSkeletalClothData& ClothData : LODData.Cloths)
            {
                if (!ShouldBuildRuntimeCloth(ClothData))
                {
                    continue;
                }

                if (EnabledCount >= Sections.size() || Sections[EnabledCount].SourceData != &ClothData)
                {
                    return false;
                }
                ++EnabledCount;
            }
        }

        return EnabledCount == Sections.size();
    }

    bool EnsureBackend()
    {
        if (!Backend.Initialize())
        {
            return false;
        }

        if (!SolverOwner)
        {
            SolverOwner = Backend.CreateSolver();
        }

        return SolverOwner && SolverOwner->IsValid() && GetNativeSolver();
    }

    bool Build(const FSkeletalMesh& Asset, const TArray<FVertexPNCTT>& SkinnedVertices)
    {
        Reset();

        if (!HasRuntimeClothCandidate(Asset))
        {
            BuiltAsset = &Asset;
            return true;
        }

        if (!EnsureBackend())
        {
            UE_LOG("[NvCloth] Failed to initialize CPU cloth runtime");
            return false;
        }

        nv::cloth::Factory* Factory = Backend.GetNativeFactory();
        nv::cloth::Solver* Solver = GetNativeSolver();
        if (!Factory || !Solver)
        {
            return false;
        }

        for (const FSkeletalClothLODData& LODData : Asset.ClothPayload.LODs)
        {
            for (const FSkeletalClothData& ClothData : LODData.Cloths)
            {
                if (!ShouldBuildRuntimeCloth(ClothData))
                {
                    continue;
                }

                FString Error;
                if (!Asset.ValidateClothData(ClothData, &Error))
                {
                    UE_LOG("[NvCloth] Skipping invalid cloth '%s': %s",
                        ClothData.Name.c_str(),
                        Error.c_str());
                    continue;
                }

                FSectionRuntime Runtime;
                Runtime.SourceData = &ClothData;
                Runtime.LODIndex = ClothData.Binding.LODIndex;
                Runtime.SectionIndex = ClothData.Binding.SectionIndex;
                Runtime.SourceVertexCount = ClothData.Binding.SourceVertexCount;
                Runtime.SourceIndexCount = ClothData.Binding.SourceIndexCount;

				Runtime.CookedPositions.resize(ClothData.ParticleToRenderVertex.size());
				Runtime.InvMasses.resize(ClothData.ParticleToRenderVertex.size());

                bool bValidMapping = true;
				for (uint32 ParticleIndex = 0; ParticleIndex < ClothData.ParticleToRenderVertex.size(); ++ParticleIndex)
				{
					const uint32 RenderVertexIndex = ClothData.ParticleToRenderVertex[ParticleIndex];
					if (RenderVertexIndex >= SkinnedVertices.size())
					{
                        bValidMapping = false;
                        break;
					}

					Runtime.CookedPositions[ParticleIndex] = ToPxVec3(SkinnedVertices[RenderVertexIndex].Position);
					Runtime.InvMasses[ParticleIndex] = GetPaintMaxDistance(ClothData, ParticleIndex) > 0.0f
						? ClothDefaultInvMass
						: 0.0f;
				}
                if (!bValidMapping)
                {
                    UE_LOG("[NvCloth] Skipping cloth '%s': particle/render vertex mapping is out of range",
                        ClothData.Name.c_str());
                    continue;
                }

                Runtime.CookedIndices.reserve(ClothData.ClothLocalIndices.size());
                for (uint32 LocalIndex : ClothData.ClothLocalIndices)
                {
                    Runtime.CookedIndices.push_back(static_cast<uint32_t>(LocalIndex));
                }

                nv::cloth::ClothMeshDesc MeshDesc;
                MeshDesc.points.data = Runtime.CookedPositions.data();
                MeshDesc.points.count = static_cast<physx::PxU32>(Runtime.CookedPositions.size());
                MeshDesc.points.stride = sizeof(physx::PxVec3);
                MeshDesc.invMasses.data = Runtime.InvMasses.data();
                MeshDesc.invMasses.count = static_cast<physx::PxU32>(Runtime.InvMasses.size());
                MeshDesc.invMasses.stride = sizeof(float);
                MeshDesc.triangles.data = Runtime.CookedIndices.data();
                MeshDesc.triangles.count = static_cast<physx::PxU32>(Runtime.CookedIndices.size() / 3);
                MeshDesc.triangles.stride = sizeof(uint32_t) * 3;

                Runtime.Fabric = NvClothCookFabricFromMesh(
                    Factory,
                    MeshDesc,
                    physx::PxVec3(0.0f, 0.0f, -1.0f),
                    nullptr,
                    false
                );

                if (!Runtime.Fabric)
                {
                    UE_LOG("[NvCloth] Failed to cook fabric for cloth '%s'", ClothData.Name.c_str());
                    Runtime.Destroy(Solver);
                    continue;
                }

                TArray<physx::PxVec4> InitialParticles;
                InitialParticles.resize(Runtime.CookedPositions.size());
                for (uint32 ParticleIndex = 0; ParticleIndex < InitialParticles.size(); ++ParticleIndex)
                {
                    const physx::PxVec3& Position = Runtime.CookedPositions[ParticleIndex];
                    InitialParticles[ParticleIndex] = physx::PxVec4(
                        Position.x,
                        Position.y,
                        Position.z,
                        Runtime.InvMasses[ParticleIndex]
                    );
                }

                Runtime.Cloth = Factory->createCloth(
                    nv::cloth::Range<const physx::PxVec4>(InitialParticles.data(), InitialParticles.data() + InitialParticles.size()),
                    *Runtime.Fabric
                );

                if (!Runtime.Cloth)
                {
                    UE_LOG("[NvCloth] Failed to create cloth '%s'", ClothData.Name.c_str());
                    Runtime.Destroy(Solver);
                    continue;
                }

                Runtime.Cloth->setGravity(physx::PxVec3(
                    0.0f,
                    0.0f,
                    -9.81f * ClothData.Config.GravityScale
                ));
                Runtime.Cloth->setDamping(physx::PxVec3(
                    Clamp01(ClothData.Config.Damping),
                    Clamp01(ClothData.Config.Damping),
                    Clamp01(ClothData.Config.Damping)
                ));
                Runtime.Cloth->setSolverFrequency(std::max(1.0f, ClothData.Config.SolverFrequency));

                TArray<nv::cloth::PhaseConfig> PhaseConfigs;
                PhaseConfigs.reserve(Runtime.Fabric->getNumPhases());
                for (uint32 PhaseIndex = 0; PhaseIndex < Runtime.Fabric->getNumPhases(); ++PhaseIndex)
                {
                    nv::cloth::PhaseConfig Phase(static_cast<uint16_t>(PhaseIndex));
                    Phase.mStiffness = Clamp01(ClothData.Config.Stiffness);
                    PhaseConfigs.push_back(Phase);
                }
                if (!PhaseConfigs.empty())
                {
                    Runtime.Cloth->setPhaseConfig(
                        nv::cloth::Range<const nv::cloth::PhaseConfig>(PhaseConfigs.data(), PhaseConfigs.data() + PhaseConfigs.size())
                    );
                }

                Solver->addCloth(Runtime.Cloth);
                Sections.push_back(std::move(Runtime));
            }
        }

        BuiltAsset = &Asset;
        return true;
    }

    FVector ApplyComponentInertiaToLocalPosition(
        const FVector& LocalPosition,
        float LinearScale,
        float AngularScale) const
    {
        const FMatrix CurrentWorldInverse = CurrentComponentWorldMatrix.GetInverse();
        const FVector CurrentWorldPosition = CurrentComponentWorldMatrix.TransformPositionWithW(LocalPosition);

        FVector TargetLocalPosition = LocalPosition;

        if (LinearScale > 0.0f)
        {
            const FVector TranslationDelta =
                CurrentComponentWorldMatrix.GetLocation() - PreviousComponentWorldMatrix.GetLocation();
            const FVector LinearTargetLocal =
                CurrentWorldInverse.TransformPositionWithW(CurrentWorldPosition - TranslationDelta);
            TargetLocalPosition += (LinearTargetLocal - LocalPosition) * LinearScale;
        }

        if (AngularScale > 0.0f)
        {
            const FVector PreviousRotatedOffset = PreviousComponentWorldMatrix.TransformVector(LocalPosition);
            const FVector CurrentRotatedOffset = CurrentComponentWorldMatrix.TransformVector(LocalPosition);
            const FVector AngularTargetLocal =
                CurrentWorldInverse.TransformPositionWithW(CurrentWorldPosition + PreviousRotatedOffset - CurrentRotatedOffset);
            TargetLocalPosition += (AngularTargetLocal - LocalPosition) * AngularScale;
        }

        return TargetLocalPosition;
    }

    void ApplyComponentInertia(FSectionRuntime& Section)
    {
        const FSkeletalClothData& ClothData = *Section.SourceData;
        const float LinearScale = Clamp01(ClothData.Config.InertiaLinearScale);
        const float AngularScale = Clamp01(ClothData.Config.InertiaAngularScale);
        if (LinearScale <= 0.0f && AngularScale <= 0.0f)
        {
            return;
        }

        nv::cloth::Range<physx::PxVec4> CurrentParticles = Section.Cloth->getCurrentParticles();
        nv::cloth::Range<physx::PxVec4> PreviousParticles = Section.Cloth->getPreviousParticles();
        const uint32 ParticleCount = static_cast<uint32>(ClothData.ParticleToRenderVertex.size());
        for (uint32 ParticleIndex = 0; ParticleIndex < ParticleCount; ++ParticleIndex)
        {
            if (GetPaintMaxDistance(ClothData, ParticleIndex) <= 0.0f)
            {
                continue;
            }

            if (ParticleIndex < CurrentParticles.size())
            {
                const float InvMass = CurrentParticles[ParticleIndex].w;
                FVector InertialPosition = ApplyComponentInertiaToLocalPosition(
                    ToFVector(CurrentParticles[ParticleIndex]),
                    LinearScale,
                    AngularScale);
                if (IsFiniteVector(InertialPosition))
                {
                    CurrentParticles[ParticleIndex] = ToPxParticle(InertialPosition, InvMass);
                }
            }

            if (ParticleIndex < PreviousParticles.size())
            {
                const float InvMass = PreviousParticles[ParticleIndex].w;
                FVector InertialPosition = ApplyComponentInertiaToLocalPosition(
                    ToFVector(PreviousParticles[ParticleIndex]),
                    LinearScale,
                    AngularScale);
                if (IsFiniteVector(InertialPosition))
                {
                    PreviousParticles[ParticleIndex] = ToPxParticle(InertialPosition, InvMass);
                }
            }
        }
    }

    void UpdateAnimatedPinsAndConstraints(FSectionRuntime& Section, const TArray<FVertexPNCTT>& SkinnedVertices)
    {
        const FSkeletalClothData& ClothData = *Section.SourceData;
        nv::cloth::Range<physx::PxVec4> CurrentParticles = Section.Cloth->getCurrentParticles();
        nv::cloth::Range<physx::PxVec4> PreviousParticles = Section.Cloth->getPreviousParticles();
        nv::cloth::Range<physx::PxVec4> MotionConstraints = Section.Cloth->getMotionConstraints();

        const uint32 ParticleCount = static_cast<uint32>(ClothData.ParticleToRenderVertex.size());
        for (uint32 ParticleIndex = 0; ParticleIndex < ParticleCount; ++ParticleIndex)
        {
            const uint32 RenderVertexIndex = ClothData.ParticleToRenderVertex[ParticleIndex];
            if (RenderVertexIndex >= SkinnedVertices.size())
            {
                continue;
            }

            const FVector& AnimatedPosition = SkinnedVertices[RenderVertexIndex].Position;
            const float MaxDistance = GetPaintMaxDistance(ClothData, ParticleIndex);

            if (ParticleIndex < CurrentParticles.size())
            {
                CurrentParticles[ParticleIndex].w = MaxDistance > 0.0f ? ClothDefaultInvMass : 0.0f;
                if (MaxDistance <= 0.0f)
                {
                    CurrentParticles[ParticleIndex] = ToPxParticle(AnimatedPosition, 0.0f);
                }
            }

            if (ParticleIndex < PreviousParticles.size() && MaxDistance <= 0.0f)
            {
                PreviousParticles[ParticleIndex] = ToPxParticle(AnimatedPosition, 0.0f);
            }

            if (ParticleIndex < MotionConstraints.size())
            {
                MotionConstraints[ParticleIndex] = ToPxParticle(AnimatedPosition, MaxDistance);
            }
        }
    }

    bool WriteBack(FSectionRuntime& Section, TArray<FVertexPNCTT>& InOutSkinnedVertices)
    {
        const FSkeletalClothData& ClothData = *Section.SourceData;
        nv::cloth::Range<const physx::PxVec4> CurrentParticles =
            static_cast<const nv::cloth::Cloth*>(Section.Cloth)->getCurrentParticles();

        bool bModified = false;
        for (uint32 ParticleIndex = 0; ParticleIndex < ClothData.ParticleToRenderVertex.size(); ++ParticleIndex)
        {
            const uint32 RenderVertexIndex = ClothData.ParticleToRenderVertex[ParticleIndex];
            if (ParticleIndex >= CurrentParticles.size() || RenderVertexIndex >= InOutSkinnedVertices.size())
            {
                continue;
            }

            InOutSkinnedVertices[RenderVertexIndex].Position = ToFVector(CurrentParticles[ParticleIndex]);
            bModified = true;
        }

        if (bModified)
        {
            RecomputeNormalsAndTangents(Section, InOutSkinnedVertices);
        }

        return bModified;
    }

    void RecomputeNormalsAndTangents(FSectionRuntime& Section, TArray<FVertexPNCTT>& InOutSkinnedVertices)
    {
        const FSkeletalClothData& ClothData = *Section.SourceData;
        const uint32 ParticleCount = static_cast<uint32>(ClothData.ParticleToRenderVertex.size());
        Section.NormalSums.assign(ParticleCount, FVector::ZeroVector);
        Section.TangentSums.assign(ParticleCount, FVector::ZeroVector);

        for (uint32 Index = 0; Index + 2 < ClothData.ClothLocalIndices.size(); Index += 3)
        {
            const uint32 I0 = ClothData.ClothLocalIndices[Index + 0];
            const uint32 I1 = ClothData.ClothLocalIndices[Index + 1];
            const uint32 I2 = ClothData.ClothLocalIndices[Index + 2];
            if (I0 >= ParticleCount || I1 >= ParticleCount || I2 >= ParticleCount)
            {
                continue;
            }

            const uint32 V0 = ClothData.ParticleToRenderVertex[I0];
            const uint32 V1 = ClothData.ParticleToRenderVertex[I1];
            const uint32 V2 = ClothData.ParticleToRenderVertex[I2];
            if (V0 >= InOutSkinnedVertices.size() || V1 >= InOutSkinnedVertices.size() || V2 >= InOutSkinnedVertices.size())
            {
                continue;
            }

            const FVector& P0 = InOutSkinnedVertices[V0].Position;
            const FVector& P1 = InOutSkinnedVertices[V1].Position;
            const FVector& P2 = InOutSkinnedVertices[V2].Position;
            FVector FaceNormal = (P1 - P0).Cross(P2 - P0);
            if (!FaceNormal.IsNearlyZero())
            {
                FaceNormal.Normalize();
                Section.NormalSums[I0] += FaceNormal;
                Section.NormalSums[I1] += FaceNormal;
                Section.NormalSums[I2] += FaceNormal;
            }

            const FVector2& UV0 = InOutSkinnedVertices[V0].UV;
            const FVector2& UV1 = InOutSkinnedVertices[V1].UV;
            const FVector2& UV2 = InOutSkinnedVertices[V2].UV;
            const float DU1 = UV1.X - UV0.X;
            const float DV1 = UV1.Y - UV0.Y;
            const float DU2 = UV2.X - UV0.X;
            const float DV2 = UV2.Y - UV0.Y;
            const float Denom = DU1 * DV2 - DV1 * DU2;
            if (std::fabs(Denom) > 1.0e-6f)
            {
                FVector Tangent = ((P1 - P0) * DV2 - (P2 - P0) * DV1) / Denom;
                if (!Tangent.IsNearlyZero())
                {
                    Tangent.Normalize();
                    Section.TangentSums[I0] += Tangent;
                    Section.TangentSums[I1] += Tangent;
                    Section.TangentSums[I2] += Tangent;
                }
            }
        }

        for (uint32 ParticleIndex = 0; ParticleIndex < ParticleCount; ++ParticleIndex)
        {
            const uint32 RenderVertexIndex = ClothData.ParticleToRenderVertex[ParticleIndex];
            if (RenderVertexIndex >= InOutSkinnedVertices.size())
            {
                continue;
            }

            FVertexPNCTT& Vertex = InOutSkinnedVertices[RenderVertexIndex];
            if (!Section.NormalSums[ParticleIndex].IsNearlyZero())
            {
                Section.NormalSums[ParticleIndex].Normalize();
                Vertex.Normal = Section.NormalSums[ParticleIndex];
            }

            if (!Section.TangentSums[ParticleIndex].IsNearlyZero())
            {
                Section.TangentSums[ParticleIndex].Normalize();
                Vertex.Tangent = FVector4(Section.TangentSums[ParticleIndex], Vertex.Tangent.W);
            }
        }
    }

    bool Tick(
        const FSkeletalMesh& Asset,
        float DeltaTime,
        TArray<FVertexPNCTT>& InOutSkinnedVertices,
        const FMatrix& ComponentWorldMatrix,
        const FClothWorldForceContext& ForceContext,
        const TArray<FClothCollisionSectionResult>* CollisionResults)
    {
        if (!PrepareComponentTransformHistory(ComponentWorldMatrix))
        {
            return false;
        }

        const bool bNeedsBuild = !MatchesBuiltPayload(Asset);
        if (bNeedsBuild && !Build(Asset, InOutSkinnedVertices))
        {
            return false;
        }
        if (bNeedsBuild)
        {
            InitializeComponentTransformHistory(ComponentWorldMatrix);
        }

        nv::cloth::Solver* Solver = GetNativeSolver();
        if (!Solver || Sections.empty() || DeltaTime <= ClothMinDeltaTime)
        {
            return false;
        }

        bool bHasSimulatedParticles = false;
        for (FSectionRuntime& Section : Sections)
        {
            if (!Section.Cloth || !Section.SourceData)
            {
                continue;
            }

            ApplyComponentInertia(Section);
            UpdateAnimatedPinsAndConstraints(Section, InOutSkinnedVertices);
            UploadCollision(Section, FindCollisionResultForSection(CollisionResults, Section));
            bHasSimulatedParticles = bHasSimulatedParticles || HasPositivePaintRadius(*Section.SourceData);
        }

        if (bHasSimulatedParticles)
        {
            UpdateWorldForces(ForceContext);
            AccumulatedSimulationTime += std::min(DeltaTime, ClothMaxFrameDelta);
            int32 SubstepCount = 0;
            while (AccumulatedSimulationTime >= ClothFixedStep && SubstepCount < ClothMaxSubsteps)
            {
                if (Solver->beginSimulation(ClothFixedStep))
                {
                    const int ChunkCount = Solver->getSimulationChunkCount();
                    for (int ChunkIndex = 0; ChunkIndex < ChunkCount; ++ChunkIndex)
                    {
                        Solver->simulateChunk(ChunkIndex);
                    }
                    Solver->endSimulation();
                }
                AccumulatedSimulationTime -= ClothFixedStep;
                ++SubstepCount;
            }

            if (SubstepCount >= ClothMaxSubsteps && AccumulatedSimulationTime >= ClothFixedStep)
            {
                AccumulatedSimulationTime = 0.0f;
            }
        }

        bool bModified = false;
        for (FSectionRuntime& Section : Sections)
        {
            if (Section.Cloth && Section.SourceData)
            {
                bModified = WriteBack(Section, InOutSkinnedVertices) || bModified;
            }
        }

        return bModified;
    }
};

FSkeletalClothRuntime::FSkeletalClothRuntime()
    : Impl(std::make_unique<FImpl>())
{
}

FSkeletalClothRuntime::~FSkeletalClothRuntime() = default;

void FSkeletalClothRuntime::Reset()
{
    Impl->Reset();
}

bool FSkeletalClothRuntime::Tick(
    const FSkeletalMesh& Asset,
    float DeltaTime,
    TArray<FVertexPNCTT>& InOutSkinnedVertices,
    const FMatrix& ComponentWorldMatrix,
    const FClothWorldForceContext& ForceContext,
    const TArray<FClothCollisionSectionResult>* CollisionResults)
{
    return Impl->Tick(Asset, DeltaTime, InOutSkinnedVertices, ComponentWorldMatrix, ForceContext, CollisionResults);
}

void FSkeletalClothRuntime::CopyCollisionDebugStats(TArray<FClothCollisionSectionResult>& InOutCollisionResults) const
{
    if (Impl)
    {
        Impl->CopyCollisionDebugStats(InOutCollisionResults);
    }
}

bool FSkeletalClothRuntime::IsActive() const
{
    return Impl && !Impl->Sections.empty();
}

FClothDebugRuntimeConfig& FSkeletalClothRuntime::GetMutableDebugRuntimeConfig()
{
    return GClothDebugRuntimeConfig;
}

const FClothDebugRuntimeConfig& FSkeletalClothRuntime::GetDebugRuntimeConfig()
{
    return GClothDebugRuntimeConfig;
}
