#include "Physics/PhysicsAssetAutoBodyGenerator.h"

#include "Animation/Skeleton/Skeleton.h"
#include "Animation/Skeleton/SkeletonTypes.h"
#include "Math/Matrix.h"
#include "Math/MathUtils.h"
#include "Math/Transform.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetTypes.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cctype>

namespace
{
    struct FAutoBodyFitResult
    {
        FTransform BodyComponentTransform;
        FVector BoxHalfExtent = FVector(0.4f, 0.4f, 1.2f);
        float CapsuleRadius = 0.4f;
        float CapsuleHalfHeight = 1.2f;
    };

    struct FAutoBodyPointFit
    {
        bool bHasPoints = false;
        FVector Min = FVector::ZeroVector;
        FVector Max = FVector::ZeroVector;
        TArray<FVector> Points;

        void AddPoint(const FVector& Point)
        {
            Points.push_back(Point);
            if (!bHasPoints)
            {
                Min = Point;
                Max = Point;
                bHasPoints = true;
                return;
            }

            Min.X = (std::min)(Min.X, Point.X);
            Min.Y = (std::min)(Min.Y, Point.Y);
            Min.Z = (std::min)(Min.Z, Point.Z);
            Max.X = (std::max)(Max.X, Point.X);
            Max.Y = (std::max)(Max.Y, Point.Y);
            Max.Z = (std::max)(Max.Z, Point.Z);
        }

        FVector GetExtent() const
        {
            return (Max - Min) * 0.5f;
        }
    };

    struct FMergedAutoBodyData
    {
        TArray<FAutoBodyPointFit> ExtraFits;
        TArray<float> MergedSizes;
        TArray<bool> bMergedIntoParent;
        int32 ForcedRootBoneIndex = -1;
    };

    constexpr float AutoBodyMinExtent = 0.05f;
    constexpr float AutoBodyRadiusPadding = 1.10f;
    constexpr float AutoBodyFallbackRadiusRatio = 0.18f;
    constexpr float AutoBodyMinWeldSize = 1.0e-6f;

    bool HasBoneName(const FName& BoneName)
    {
        return BoneName.IsValid() && BoneName != FName::None;
    }

    FString ToLowerBoneName(FString BoneName)
    {
        std::transform(
            BoneName.begin(),
            BoneName.end(),
            BoneName.begin(),
            [](unsigned char Character)
            {
                return static_cast<char>(std::tolower(Character));
            });
        return BoneName;
    }

    bool ContainsToken(const FString& Value, const char* Token)
    {
        return Value.find(Token) != FString::npos;
    }

    bool IsLikelyHelperBoneName(const FString& BoneName)
    {
        if (BoneName.empty())
        {
            return true;
        }

        const FString LowerName = ToLowerBoneName(BoneName);
        return
            LowerName == "root" ||
            LowerName == "end" ||
            ContainsToken(LowerName, "ik_") ||
            ContainsToken(LowerName, "_ik") ||
            ContainsToken(LowerName, "ik-") ||
            ContainsToken(LowerName, "-ik") ||
            ContainsToken(LowerName, "socket") ||
            ContainsToken(LowerName, "weapon") ||
            ContainsToken(LowerName, "attach") ||
            ContainsToken(LowerName, "ctrl") ||
            ContainsToken(LowerName, "control") ||
            ContainsToken(LowerName, "helper") ||
            ContainsToken(LowerName, "virtual") ||
            ContainsToken(LowerName, "twist") ||
            ContainsToken(LowerName, "roll") ||
            ContainsToken(LowerName, "nub") ||
            ContainsToken(LowerName, "dummy") ||
            ContainsToken(LowerName, "marker");
    }

    FVector GetMatrixTranslation(const FMatrix& Matrix)
    {
        return Matrix.GetLocation();
    }

    FTransform MakeTransformNoScale(const FMatrix& Matrix)
    {
        FTransform Transform;
        Transform.Location = Matrix.GetLocation();
        Transform.Rotation = Matrix.ToQuat().GetNormalized();
        Transform.Scale = FVector::OneVector;
        return Transform;
    }

    FTransform ComposeComponentTransforms(const FTransform& Parent, const FTransform& Local)
    {
        FTransform Result = Local;
        Result.Location = Parent.Location + Parent.Rotation.RotateVector(Local.Location);
        Result.Rotation = (Parent.Rotation * Local.Rotation).GetNormalized();
        Result.Scale = FVector::OneVector;
        return Result;
    }

    FTransform MakeLocalTransformFromComponent(const FTransform& Parent, const FTransform& Component)
    {
        const FQuat ParentInv = Parent.Rotation.GetNormalized().Inverse();
        FTransform Local;
        Local.Location = ParentInv.RotateVector(Component.Location - Parent.Location);
        Local.Rotation = (ParentInv * Component.Rotation.GetNormalized()).GetNormalized();
        Local.Scale = FVector::OneVector;
        return Local;
    }

    FVector SafeNormal(const FVector& Value, const FVector& Fallback)
    {
        if (Value.IsNearlyZero(1.0e-6f))
        {
            return Fallback;
        }
        FVector Result = Value;
        Result.Normalize();
        return Result;
    }

    float GetVectorComponent(const FVector& Value, int32 Index)
    {
        if (Index == 0) return Value.X;
        if (Index == 1) return Value.Y;
        return Value.Z;
    }

    float GetMaxComponent(const FVector& Value)
    {
        return (std::max)(Value.X, (std::max)(Value.Y, Value.Z));
    }

    FVector ClampAutoBodyHalfExtent(const FVector& HalfExtent)
    {
        return FVector(
            (std::max)(HalfExtent.X, AutoBodyMinExtent),
            (std::max)(HalfExtent.Y, AutoBodyMinExtent),
            (std::max)(HalfExtent.Z, AutoBodyMinExtent));
    }

    void BuildBasisFromZAxis(const FVector& InAxisZ, FVector& OutAxisX, FVector& OutAxisY, FVector& OutAxisZ)
    {
        OutAxisZ = SafeNormal(InAxisZ, FVector::ZAxisVector);
        const FVector UpCandidate = std::fabs(OutAxisZ.Z) < 0.90f ? FVector::ZAxisVector : FVector::YAxisVector;
        OutAxisX = SafeNormal(UpCandidate.Cross(OutAxisZ), FVector::XAxisVector);
        OutAxisY = SafeNormal(OutAxisZ.Cross(OutAxisX), FVector::YAxisVector);
    }

    FQuat MakeQuatFromLocalAxes(const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ)
    {
        FMatrix Matrix = FMatrix::Identity;
        Matrix.SetAxes(AxisX, AxisY, AxisZ);
        return Matrix.ToQuat().GetNormalized();
    }

    int32 FindMeshBoneIndexByName(const FSkeletalMesh* MeshAsset, const FString& BoneName)
    {
        if (!MeshAsset || BoneName.empty())
        {
            return -1;
        }

        for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
        {
            if (MeshAsset->Bones[BoneIndex].Name == BoneName)
            {
                return BoneIndex;
            }
        }
        return -1;
    }

    int32 FindFirstChildMeshBoneIndex(const FSkeletalMesh* MeshAsset, int32 MeshBoneIndex)
    {
        if (!MeshAsset || MeshBoneIndex < 0 || MeshBoneIndex >= static_cast<int32>(MeshAsset->Bones.size()))
        {
            return -1;
        }

        for (int32 BoneIndex = MeshBoneIndex + 1; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
        {
            if (MeshAsset->Bones[BoneIndex].ParentIndex == MeshBoneIndex)
            {
                return BoneIndex;
            }
        }
        return -1;
    }

    const FMatrix& GetGenerationBoneGlobalPose(
        const FSkeletalMesh* MeshAsset,
        const TArray<FMatrix>* OverrideBoneGlobalMatrices,
        int32 MeshBoneIndex)
    {
        if (OverrideBoneGlobalMatrices &&
            MeshBoneIndex >= 0 &&
            MeshBoneIndex < static_cast<int32>(OverrideBoneGlobalMatrices->size()))
        {
            return (*OverrideBoneGlobalMatrices)[MeshBoneIndex];
        }

        return MeshAsset->Bones[MeshBoneIndex].GetReferenceGlobalPose();
    }

    bool GetBoneAxisSegment(
        const FSkeletalMesh* MeshAsset,
        const TArray<FMatrix>* OverrideBoneGlobalMatrices,
        int32 MeshBoneIndex,
        FVector& OutStart,
        FVector& OutEnd)
    {
        if (!MeshAsset || MeshBoneIndex < 0 || MeshBoneIndex >= static_cast<int32>(MeshAsset->Bones.size()))
        {
            return false;
        }

        OutStart = GetMatrixTranslation(GetGenerationBoneGlobalPose(MeshAsset, OverrideBoneGlobalMatrices, MeshBoneIndex));
        const int32 ChildIndex = FindFirstChildMeshBoneIndex(MeshAsset, MeshBoneIndex);
        if (ChildIndex >= 0)
        {
            OutEnd = GetMatrixTranslation(GetGenerationBoneGlobalPose(MeshAsset, OverrideBoneGlobalMatrices, ChildIndex));
            if (!FVector(OutEnd - OutStart).IsNearlyZero(1.0e-6f))
            {
                return true;
            }
        }

        const int32 ParentIndex = MeshAsset->Bones[MeshBoneIndex].ParentIndex;
        if (ParentIndex >= 0 && ParentIndex < static_cast<int32>(MeshAsset->Bones.size()))
        {
            const FVector ParentLocation = GetMatrixTranslation(GetGenerationBoneGlobalPose(MeshAsset, OverrideBoneGlobalMatrices, ParentIndex));
            FVector Direction = OutStart - ParentLocation;
            if (!Direction.IsNearlyZero(1.0e-6f))
            {
                Direction.Normalize();
                const float Length = (std::max)(FVector::Distance(OutStart, ParentLocation) * 0.35f, AutoBodyMinExtent * 2.0f);
                OutEnd = OutStart + Direction * Length;
                OutStart -= Direction * Length;
                return true;
            }
        }

        OutEnd = OutStart + FVector::ZAxisVector * (AutoBodyMinExtent * 4.0f);
        return true;
    }

    FVector TransformVertexToGenerationPose(
        const FSkeletalMesh* MeshAsset,
        const TArray<FMatrix>* OverrideBoneGlobalMatrices,
        const FVertexPNCTBW& Vertex)
    {
        if (!MeshAsset)
        {
            return Vertex.Position;
        }

        FVector PosedPosition = FVector::ZeroVector;
        float AccumulatedWeight = 0.0f;

        // Use the same bind-to-pose skinning convention as the renderer.
        // This keeps auto body fitting in the visible pose space instead of raw FBX
        // mesh/bind space, which can be 90 degrees off for some imported assets.
        for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
        {
            const int32 BoneIndex = Vertex.BoneIndices[InfluenceIndex];
            const float Weight = Vertex.BoneWeights[InfluenceIndex];
            if (Weight <= 0.0f || BoneIndex < 0 || BoneIndex >= static_cast<int32>(MeshAsset->Bones.size()))
            {
                continue;
            }

            const FMatrix PoseSkinMatrix =
                MeshAsset->Bones[BoneIndex].GetInverseBindPose() *
                GetGenerationBoneGlobalPose(MeshAsset, OverrideBoneGlobalMatrices, BoneIndex);
            PosedPosition += PoseSkinMatrix.TransformPositionWithW(Vertex.Position) * Weight;
            AccumulatedWeight += Weight;
        }

        if (AccumulatedWeight <= 1.0e-6f)
        {
            return Vertex.Position;
        }

        return PosedPosition;
    }

    void CollectInfluencedBoneVertices(
        const FSkeletalMesh* MeshAsset,
        const TArray<FMatrix>* OverrideBoneGlobalMatrices,
        int32 MeshBoneIndex,
        float MinInfluenceWeight,
        TArray<FVector>& OutPoints)
    {
        OutPoints.clear();
        if (!MeshAsset || MeshBoneIndex < 0)
        {
            return;
        }

        const float ClampedMinWeight = (std::min)((std::max)(MinInfluenceWeight, 0.0f), 1.0f);
        for (const FVertexPNCTBW& Vertex : MeshAsset->Vertices)
        {
            for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
            {
                const int32 CandidateBoneIndex = Vertex.BoneIndices[InfluenceIndex];
                const float CandidateWeight = Vertex.BoneWeights[InfluenceIndex];
                if (CandidateBoneIndex == MeshBoneIndex && CandidateWeight >= ClampedMinWeight)
                {
                    OutPoints.push_back(TransformVertexToGenerationPose(MeshAsset, OverrideBoneGlobalMatrices, Vertex));
                    break;
                }
            }
        }
    }


    float GetPointFitSize(const FAutoBodyPointFit& Fit)
    {
        return Fit.bHasPoints ? Fit.GetExtent().Length() : 0.0f;
    }

    bool IsValidRefBoneIndex(const FReferenceSkeleton& RefSkeleton, int32 BoneIndex)
    {
        return BoneIndex >= 0 && BoneIndex < RefSkeleton.GetNumBones();
    }

    void AppendPointFit(FAutoBodyPointFit& TargetFit, const FAutoBodyPointFit& SourceFit)
    {
        if (!SourceFit.bHasPoints)
        {
            return;
        }

        for (const FVector& Point : SourceFit.Points)
        {
            TargetFit.AddPoint(Point);
        }
    }

    FAutoBodyPointFit MakeCombinedPointFit(const FAutoBodyPointFit& OwnFit, const FAutoBodyPointFit& ExtraFit)
    {
        FAutoBodyPointFit CombinedFit;
        AppendPointFit(CombinedFit, OwnFit);
        AppendPointFit(CombinedFit, ExtraFit);
        return CombinedFit;
    }

    TArray<FAutoBodyPointFit> BuildBonePointFits(
        const FReferenceSkeleton& RefSkeleton,
        const FSkeletalMesh* MeshAsset,
        const TArray<FMatrix>* OverrideBoneGlobalMatrices,
        const FPhysicsAssetAutoBodyGeneratorOptions& Options)
    {
        TArray<FAutoBodyPointFit> Fits;
        Fits.resize(RefSkeleton.GetNumBones());
        if (!MeshAsset)
        {
            return Fits;
        }

        for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNumBones(); ++BoneIndex)
        {
            const int32 MeshBoneIndex = FindMeshBoneIndexByName(MeshAsset, RefSkeleton.Bones[BoneIndex].Name);
            if (MeshBoneIndex < 0 || MeshBoneIndex >= static_cast<int32>(MeshAsset->Bones.size()))
            {
                continue;
            }

            TArray<FVector> Points;
            CollectInfluencedBoneVertices(MeshAsset, OverrideBoneGlobalMatrices, MeshBoneIndex, Options.MinInfluenceWeight, Points);
            for (const FVector& Point : Points)
            {
                Fits[BoneIndex].AddPoint(Point);
            }
        }

        return Fits;
    }

    FMergedAutoBodyData BuildMergedAutoBodyData(
        const FReferenceSkeleton& RefSkeleton,
        const TArray<FAutoBodyPointFit>& BoneFits,
        const FPhysicsAssetAutoBodyGeneratorOptions& Options)
    {
        FMergedAutoBodyData Data;
        const int32 BoneCount = RefSkeleton.GetNumBones();
        Data.ExtraFits.resize(BoneCount);
        Data.MergedSizes.resize(BoneCount, 0.0f);
        Data.bMergedIntoParent.resize(BoneCount, false);

        if (!Options.bMergeSmallBones)
        {
            return Data;
        }

        const float MinBoneSize = (std::max)(Options.MinBoneSize, 0.0f);
        const float MinWeldSize = (std::max)(Options.MinWeldSize, AutoBodyMinWeldSize);

        for (int32 BoneIndex = BoneCount - 1; BoneIndex >= 0; --BoneIndex)
        {
            if (!IsValidRefBoneIndex(RefSkeleton, BoneIndex))
            {
                continue;
            }

            const FReferenceBone& RefBone = RefSkeleton.Bones[BoneIndex];
            const float MyMergedSize = Data.MergedSizes[BoneIndex] +
                (BoneIndex < static_cast<int32>(BoneFits.size()) ? GetPointFitSize(BoneFits[BoneIndex]) : 0.0f);
            Data.MergedSizes[BoneIndex] = MyMergedSize;

            const bool bHelperBone = Options.bSkipHelperBones && IsLikelyHelperBoneName(RefBone.Name);
            const bool bSmallEnoughToMerge = MyMergedSize < MinBoneSize && MyMergedSize >= MinWeldSize;
            if (!bHelperBone && !bSmallEnoughToMerge)
            {
                continue;
            }

            const int32 ParentIndex = RefBone.ParentIndex;
            if (!IsValidRefBoneIndex(RefSkeleton, ParentIndex))
            {
                continue;
            }

            Data.MergedSizes[ParentIndex] += MyMergedSize;
            if (BoneIndex < static_cast<int32>(BoneFits.size()))
            {
                AppendPointFit(Data.ExtraFits[ParentIndex], BoneFits[BoneIndex]);
            }
            AppendPointFit(Data.ExtraFits[ParentIndex], Data.ExtraFits[BoneIndex]);
            Data.bMergedIntoParent[BoneIndex] = true;
        }

        int32 FirstParentBoneIndex = -1;
        for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
        {
            if (Data.MergedSizes[BoneIndex] <= MinBoneSize)
            {
                continue;
            }

            const int32 ParentBoneIndex = RefSkeleton.Bones[BoneIndex].ParentIndex;
            if (ParentBoneIndex == -1)
            {
                break;
            }

            if (FirstParentBoneIndex == -1)
            {
                FirstParentBoneIndex = ParentBoneIndex;
                continue;
            }

            if (ParentBoneIndex == FirstParentBoneIndex)
            {
                Data.ForcedRootBoneIndex = ParentBoneIndex;
                break;
            }
        }

        return Data;
    }

    bool ShouldGenerateBodyForBone(
        const FReferenceSkeleton& RefSkeleton,
        const FMergedAutoBodyData& MergedData,
        const FPhysicsAssetAutoBodyGeneratorOptions& Options,
        int32 BoneIndex)
    {
        if (!IsValidRefBoneIndex(RefSkeleton, BoneIndex))
        {
            return false;
        }

        if (Options.bSkipHelperBones &&
            IsLikelyHelperBoneName(RefSkeleton.Bones[BoneIndex].Name) &&
            BoneIndex != MergedData.ForcedRootBoneIndex)
        {
            return false;
        }

        if (!Options.bMergeSmallBones)
        {
            return true;
        }

        if (BoneIndex == MergedData.ForcedRootBoneIndex)
        {
            return true;
        }

        if (BoneIndex < static_cast<int32>(MergedData.bMergedIntoParent.size()) && MergedData.bMergedIntoParent[BoneIndex])
        {
            return false;
        }

        const float MinBoneSize = (std::max)(Options.MinBoneSize, 0.0f);
        if (BoneIndex < static_cast<int32>(MergedData.MergedSizes.size()) && MergedData.MergedSizes[BoneIndex] < MinBoneSize)
        {
            return false;
        }

        return true;
    }

    bool FitCapsuleToPointsOnBasis(
        const TArray<FVector>& Points,
        const FVector& Origin,
        const FVector& AxisX,
        const FVector& AxisY,
        const FVector& AxisZ,
        FAutoBodyFitResult& OutFit)
    {
        if (Points.empty())
        {
            return false;
        }

        FVector Min(FLT_MAX, FLT_MAX, FLT_MAX);
        FVector Max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        for (const FVector& Point : Points)
        {
            const FVector Delta = Point - Origin;
            const FVector Projected(Delta.Dot(AxisX), Delta.Dot(AxisY), Delta.Dot(AxisZ));
            Min.X = (std::min)(Min.X, Projected.X);
            Min.Y = (std::min)(Min.Y, Projected.Y);
            Min.Z = (std::min)(Min.Z, Projected.Z);
            Max.X = (std::max)(Max.X, Projected.X);
            Max.Y = (std::max)(Max.Y, Projected.Y);
            Max.Z = (std::max)(Max.Z, Projected.Z);
        }

        const FVector CenterLocal((Min.X + Max.X) * 0.5f, (Min.Y + Max.Y) * 0.5f, (Min.Z + Max.Z) * 0.5f);
        const FVector Center = Origin + AxisX * CenterLocal.X + AxisY * CenterLocal.Y + AxisZ * CenterLocal.Z;
        const float HalfLength = (std::max)((Max.Z - Min.Z) * 0.5f, AutoBodyMinExtent);
        const float RadiusX = (std::max)((Max.X - Min.X) * 0.5f, AutoBodyMinExtent);
        const float RadiusY = (std::max)((Max.Y - Min.Y) * 0.5f, AutoBodyMinExtent);
        const float Radius = (std::max)((std::max)(RadiusX, RadiusY) * AutoBodyRadiusPadding, AutoBodyMinExtent);

        OutFit.BodyComponentTransform.Location = Center;
        OutFit.BodyComponentTransform.Rotation = MakeQuatFromLocalAxes(AxisX, AxisY, AxisZ);
        OutFit.BodyComponentTransform.Scale = FVector::OneVector;
        OutFit.BoxHalfExtent = FVector(RadiusX, RadiusY, HalfLength);
        OutFit.CapsuleRadius = Radius;
        OutFit.CapsuleHalfHeight = (std::max)(HalfLength, Radius + AutoBodyMinExtent);
        return true;
    }

    void ComputeSymmetricEigen3x3(const double InMatrix[3][3], double OutValues[3], FVector OutVectors[3])
    {
        double A[3][3] = {};
        double V[3][3] = {};
        for (int32 Row = 0; Row < 3; ++Row)
        {
            for (int32 Col = 0; Col < 3; ++Col)
            {
                A[Row][Col] = InMatrix[Row][Col];
                V[Row][Col] = Row == Col ? 1.0 : 0.0;
            }
        }

        for (int32 Iteration = 0; Iteration < 32; ++Iteration)
        {
            int32 P = 0;
            int32 Q = 1;
            double MaxOffDiagonal = std::fabs(A[0][1]);
            if (std::fabs(A[0][2]) > MaxOffDiagonal)
            {
                P = 0;
                Q = 2;
                MaxOffDiagonal = std::fabs(A[0][2]);
            }
            if (std::fabs(A[1][2]) > MaxOffDiagonal)
            {
                P = 1;
                Q = 2;
                MaxOffDiagonal = std::fabs(A[1][2]);
            }
            if (MaxOffDiagonal < 1.0e-10)
            {
                break;
            }

            const double App = A[P][P];
            const double Aqq = A[Q][Q];
            const double Apq = A[P][Q];
            const double Theta = (Aqq - App) / (2.0 * Apq);
            const double Sign = Theta >= 0.0 ? 1.0 : -1.0;
            const double T = Sign / (std::fabs(Theta) + std::sqrt(Theta * Theta + 1.0));
            const double C = 1.0 / std::sqrt(T * T + 1.0);
            const double S = T * C;

            for (int32 K = 0; K < 3; ++K)
            {
                if (K == P || K == Q)
                {
                    continue;
                }
                const double Akp = A[K][P];
                const double Akq = A[K][Q];
                A[K][P] = A[P][K] = C * Akp - S * Akq;
                A[K][Q] = A[Q][K] = S * Akp + C * Akq;
            }

            A[P][P] = C * C * App - 2.0 * S * C * Apq + S * S * Aqq;
            A[Q][Q] = S * S * App + 2.0 * S * C * Apq + C * C * Aqq;
            A[P][Q] = A[Q][P] = 0.0;

            for (int32 K = 0; K < 3; ++K)
            {
                const double Vkp = V[K][P];
                const double Vkq = V[K][Q];
                V[K][P] = C * Vkp - S * Vkq;
                V[K][Q] = S * Vkp + C * Vkq;
            }
        }

        int32 Order[3] = { 0, 1, 2 };
        std::sort(
            Order,
            Order + 3,
            [&](int32 AIndex, int32 BIndex)
            {
                return A[AIndex][AIndex] > A[BIndex][BIndex];
            });

        for (int32 Rank = 0; Rank < 3; ++Rank)
        {
            const int32 SourceIndex = Order[Rank];
            OutValues[Rank] = A[SourceIndex][SourceIndex];
            OutVectors[Rank] = FVector(
                static_cast<float>(V[0][SourceIndex]),
                static_cast<float>(V[1][SourceIndex]),
                static_cast<float>(V[2][SourceIndex]));
            OutVectors[Rank] = SafeNormal(OutVectors[Rank], Rank == 0 ? FVector::ZAxisVector : FVector::XAxisVector);
        }
    }

    bool BuildPCAAutoBodyFit(
        const TArray<FVector>& Points,
        const FVector& BoneAxis,
        FAutoBodyFitResult& OutFit)
    {
        if (Points.size() < 4)
        {
            return false;
        }

        FVector Mean = FVector::ZeroVector;
        for (const FVector& Point : Points)
        {
            Mean += Point;
        }
        Mean /= static_cast<float>(Points.size());

        double Covariance[3][3] = {};
        for (const FVector& Point : Points)
        {
            const FVector Delta = Point - Mean;
            for (int32 Row = 0; Row < 3; ++Row)
            {
                for (int32 Col = 0; Col < 3; ++Col)
                {
                    Covariance[Row][Col] +=
                        static_cast<double>(GetVectorComponent(Delta, Row)) *
                        static_cast<double>(GetVectorComponent(Delta, Col));
                }
            }
        }
        for (int32 Row = 0; Row < 3; ++Row)
        {
            for (int32 Col = 0; Col < 3; ++Col)
            {
                Covariance[Row][Col] /= static_cast<double>(Points.size());
            }
        }

        double EigenValues[3] = {};
        FVector EigenVectors[3];
        ComputeSymmetricEigen3x3(Covariance, EigenValues, EigenVectors);
        if (EigenValues[0] <= 1.0e-8)
        {
            return false;
        }

        FVector AxisZ = SafeNormal(EigenVectors[0], FVector::ZAxisVector);
        const FVector StableBoneAxis = SafeNormal(BoneAxis, AxisZ);
        if (AxisZ.Dot(StableBoneAxis) < 0.0f)
        {
            AxisZ *= -1.0f;
        }

        FVector AxisX = EigenVectors[1] - AxisZ * EigenVectors[1].Dot(AxisZ);
        if (AxisX.IsNearlyZero(1.0e-5f))
        {
            FVector AxisY;
            BuildBasisFromZAxis(AxisZ, AxisX, AxisY, AxisZ);
            return FitCapsuleToPointsOnBasis(Points, Mean, AxisX, AxisY, AxisZ, OutFit);
        }
        AxisX.Normalize();
        FVector AxisY = SafeNormal(AxisZ.Cross(AxisX), FVector::YAxisVector);
        AxisX = SafeNormal(AxisY.Cross(AxisZ), AxisX);

        return FitCapsuleToPointsOnBasis(Points, Mean, AxisX, AxisY, AxisZ, OutFit);
    }

    bool BuildBoneAxisAutoBodyFit(
        const FSkeletalMesh* MeshAsset,
        const TArray<FMatrix>* OverrideBoneGlobalMatrices,
        int32 MeshBoneIndex,
        const TArray<FVector>& Points,
        FAutoBodyFitResult& OutFit)
    {
        FVector SegmentStart;
        FVector SegmentEnd;
        if (!GetBoneAxisSegment(MeshAsset, OverrideBoneGlobalMatrices, MeshBoneIndex, SegmentStart, SegmentEnd))
        {
            return false;
        }

        FVector AxisX;
        FVector AxisY;
        FVector AxisZ;
        BuildBasisFromZAxis(SegmentEnd - SegmentStart, AxisX, AxisY, AxisZ);

        if (FitCapsuleToPointsOnBasis(Points, SegmentStart, AxisX, AxisY, AxisZ, OutFit))
        {
            return true;
        }

        const float SegmentLength = (std::max)(FVector::Distance(SegmentStart, SegmentEnd), AutoBodyMinExtent * 2.0f);
        const float Radius = (std::max)(SegmentLength * AutoBodyFallbackRadiusRatio, AutoBodyMinExtent);
        OutFit.BodyComponentTransform.Location = (SegmentStart + SegmentEnd) * 0.5f;
        OutFit.BodyComponentTransform.Rotation = MakeQuatFromLocalAxes(AxisX, AxisY, AxisZ);
        OutFit.BodyComponentTransform.Scale = FVector::OneVector;
        OutFit.BoxHalfExtent = FVector(Radius, Radius, (std::max)(SegmentLength * 0.5f, AutoBodyMinExtent));
        OutFit.CapsuleRadius = Radius;
        OutFit.CapsuleHalfHeight = (std::max)(SegmentLength * 0.5f, Radius + AutoBodyMinExtent);
        return true;
    }

    bool BuildAutoBodyFit(
        const FSkeletalMesh* MeshAsset,
        const TArray<FMatrix>* OverrideBoneGlobalMatrices,
        int32 MeshBoneIndex,
        const TArray<FVector>& Points,
        EPhysicsAssetAutoBodyMethod Method,
        bool bAllowBoneAxisFallback,
        FAutoBodyFitResult& OutFit)
    {
        FVector SegmentStart;
        FVector SegmentEnd;
        GetBoneAxisSegment(MeshAsset, OverrideBoneGlobalMatrices, MeshBoneIndex, SegmentStart, SegmentEnd);
        const FVector BoneAxis = SegmentEnd - SegmentStart;
        if (Method == EPhysicsAssetAutoBodyMethod::PCAAnalysis)
        {
            if (BuildPCAAutoBodyFit(Points, BoneAxis, OutFit))
            {
                return true;
            }

            if (!bAllowBoneAxisFallback)
            {
                return false;
            }
        }

        return BuildBoneAxisAutoBodyFit(MeshAsset, OverrideBoneGlobalMatrices, MeshBoneIndex, Points, OutFit);
    }

    FPhysicsAssetShapeSetup BuildShapeSetupFromFit(
        const FAutoBodyFitResult& Fit,
        const FPhysicsAssetAutoBodyGeneratorOptions& Options)
    {
        const float FitPadding = (std::max)(Options.FitPadding, 0.01f);
        const FVector BoxHalfExtent = ClampAutoBodyHalfExtent(Fit.BoxHalfExtent * FitPadding);
        const float SphereRadius = (std::max)(GetMaxComponent(BoxHalfExtent), AutoBodyMinExtent);
        const float CapsuleRadius = (std::max)(Fit.CapsuleRadius * FitPadding, AutoBodyMinExtent);
        const float CapsuleHalfHeight = (std::max)(Fit.CapsuleHalfHeight * FitPadding, CapsuleRadius + AutoBodyMinExtent);

        FPhysicsAssetShapeSetup Shape;
        Shape.LocalTransform = FTransform();
        Shape.BoxHalfExtent = BoxHalfExtent;
        Shape.SphereRadius = SphereRadius;
        Shape.CapsuleRadius = CapsuleRadius;
        Shape.CapsuleHalfHeight = CapsuleHalfHeight;

        switch (Options.PrimitiveType)
        {
        case EPhysicsAssetAutoBodyPrimitiveType::Box:
            Shape.Type = EPhysicsAssetShapeType::Box;
            break;
        case EPhysicsAssetAutoBodyPrimitiveType::Sphere:
            Shape.Type = EPhysicsAssetShapeType::Sphere;
            Shape.BoxHalfExtent = FVector(SphereRadius, SphereRadius, SphereRadius);
            Shape.CapsuleRadius = SphereRadius;
            Shape.CapsuleHalfHeight = SphereRadius;
            break;
        case EPhysicsAssetAutoBodyPrimitiveType::Capsule:
        default:
            Shape.Type = EPhysicsAssetShapeType::Capsule;
            break;
        }

        return Shape;
    }

    bool ComputeBodyComponentTransformFromSetup(
        const FSkeletalMesh* MeshAsset,
        const TArray<FMatrix>* OverrideBoneGlobalMatrices,
        const FPhysicsAssetBodySetup& BodySetup,
        FTransform& OutBodyComponentTransform)
    {
        if (!MeshAsset || !HasBoneName(BodySetup.BoneName))
        {
            return false;
        }

        const int32 MeshBoneIndex = FindMeshBoneIndexByName(MeshAsset, BodySetup.BoneName.ToString());
        if (MeshBoneIndex < 0 || MeshBoneIndex >= static_cast<int32>(MeshAsset->Bones.size()))
        {
            return false;
        }

        const FTransform BoneComponentTransform = MakeTransformNoScale(GetGenerationBoneGlobalPose(MeshAsset, OverrideBoneGlobalMatrices, MeshBoneIndex));
        OutBodyComponentTransform = ComposeComponentTransforms(BoneComponentTransform, BodySetup.BodyLocalFrame);
        return true;
    }
} //namespace 실화?

bool FPhysicsAssetAutoBodyGenerator::Regenerate(
    UPhysicsAsset* PhysicsAsset,
    USkeletalMesh* SkeletalMesh,
    const FPhysicsAssetAutoBodyGeneratorOptions& Options,
    FPhysicsAssetAutoBodyGeneratorResult* OutResult,
    const TArray<FMatrix>* OverrideBoneGlobalMatrices)
{
    FPhysicsAssetAutoBodyGeneratorResult Result;

    if (!PhysicsAsset || !SkeletalMesh || !SkeletalMesh->GetSkeleton())
    {
        if (OutResult)
        {
            *OutResult = Result;
        }
        return false;
    }

    FSkeletalMesh* MeshAsset = SkeletalMesh->GetSkeletalMeshAsset();
    if (!MeshAsset)
    {
        if (OutResult)
        {
            *OutResult = Result;
        }
        return false;
    }

    const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetSkeleton()->GetReferenceSkeleton();
    if (RefSkeleton.GetNumBones() <= 0)
    {
        if (OutResult)
        {
            *OutResult = Result;
        }
        return false;
    }

    if (Options.bReplaceExisting)
    {
        PhysicsAsset->ClearBodySetups();
        Result.bAssetChanged = true;
    }

    const TArray<FAutoBodyPointFit> BoneFits = BuildBonePointFits(
        RefSkeleton,
        MeshAsset,
        OverrideBoneGlobalMatrices,
        Options);
    const FMergedAutoBodyData MergedData = BuildMergedAutoBodyData(
        RefSkeleton,
        BoneFits,
        Options);

    for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNumBones(); ++BoneIndex)
    {
        const FReferenceBone& RefBone = RefSkeleton.Bones[BoneIndex];
        const FName BoneName(RefBone.Name);
        if (PhysicsAsset->HasBodySetupForBone(BoneName))
        {
            continue;
        }

        if (!ShouldGenerateBodyForBone(RefSkeleton, MergedData, Options, BoneIndex))
        {
            ++Result.SkippedBoneCount;
            continue;
        }

        const int32 MeshBoneIndex = FindMeshBoneIndexByName(MeshAsset, RefBone.Name);
        if (MeshBoneIndex < 0 || MeshBoneIndex >= static_cast<int32>(MeshAsset->Bones.size()))
        {
            ++Result.SkippedBoneCount;
            continue;
        }

        const FAutoBodyPointFit EmptyFit;
        const FAutoBodyPointFit& OwnFit = BoneIndex < static_cast<int32>(BoneFits.size()) ? BoneFits[BoneIndex] : EmptyFit;
        const FAutoBodyPointFit& ExtraFit = BoneIndex < static_cast<int32>(MergedData.ExtraFits.size()) ? MergedData.ExtraFits[BoneIndex] : EmptyFit;
        const FAutoBodyPointFit CombinedFit = MakeCombinedPointFit(OwnFit, ExtraFit);
        const TArray<FVector>& Points = CombinedFit.Points;

        const int32 MinWeightedVertices = (std::max)(Options.MinWeightedVertices, 1);
        if (static_cast<int32>(Points.size()) < MinWeightedVertices && !Options.bAllowBoneAxisFallback)
        {
            ++Result.SkippedBoneCount;
            continue;
        }

        FAutoBodyFitResult Fit;
        if (!BuildAutoBodyFit(
                MeshAsset,
                OverrideBoneGlobalMatrices,
                MeshBoneIndex,
                Points,
                Options.Method,
                Options.bAllowBoneAxisFallback,
                Fit))
        {
            ++Result.SkippedBoneCount;
            continue;
        }

        const FTransform BoneComponentTransform = MakeTransformNoScale(GetGenerationBoneGlobalPose(MeshAsset, OverrideBoneGlobalMatrices, MeshBoneIndex));

        FPhysicsAssetBodySetup Body;
        Body.BoneName = BoneName;
        Body.BodyLocalFrame = MakeLocalTransformFromComponent(BoneComponentTransform, Fit.BodyComponentTransform);
        Body.Shapes.push_back(BuildShapeSetupFromFit(Fit, Options));

        const int32 NewBodyIndex = PhysicsAsset->AddBodySetup(Body);
        if (NewBodyIndex >= 0)
        {
            if (Result.FirstGeneratedBodyIndex < 0)
            {
                Result.FirstGeneratedBodyIndex = NewBodyIndex;
                Result.FirstGeneratedBoneName = BoneName;
            }
            ++Result.GeneratedBodyCount;
            Result.bAssetChanged = true;
        }
    }

    if (Options.bCreateConstraints)
    {
        for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNumBones(); ++BoneIndex)
        {
            const FName ChildBoneName(RefSkeleton.Bones[BoneIndex].Name);
            const int32 ChildBodyIndex = PhysicsAsset->FindBodySetupIndexByBoneName(ChildBoneName);
            if (ChildBodyIndex < 0)
            {
                continue;
            }

            int32 ParentBoneIndex = RefSkeleton.Bones[BoneIndex].ParentIndex;
            while (ParentBoneIndex >= 0 && ParentBoneIndex < RefSkeleton.GetNumBones())
            {
                const FName ParentBoneName(RefSkeleton.Bones[ParentBoneIndex].Name);
                const int32 ParentBodyIndex = PhysicsAsset->FindBodySetupIndexByBoneName(ParentBoneName);
                if (ParentBodyIndex >= 0)
                {
                    if (!PhysicsAsset->HasConstraintBetweenBones(ParentBoneName, ChildBoneName))
                    {
                        FPhysicsAssetConstraintSetup Constraint;
                        Constraint.ParentBoneName = ParentBoneName;
                        Constraint.ChildBoneName = ChildBoneName;
                        Constraint.bDisableCollisionBetweenBodies = Options.bDisableCollisionBetweenConstrainedBodies;

                        const int32 ChildMeshBoneIndex = FindMeshBoneIndexByName(MeshAsset, RefSkeleton.Bones[BoneIndex].Name);
                        FTransform ParentBodyComponentTransform;
                        FTransform ChildBodyComponentTransform;
                        if (ChildMeshBoneIndex >= 0 &&
                            ComputeBodyComponentTransformFromSetup(MeshAsset, OverrideBoneGlobalMatrices, PhysicsAsset->GetBodySetups()[ParentBodyIndex], ParentBodyComponentTransform) &&
                            ComputeBodyComponentTransformFromSetup(MeshAsset, OverrideBoneGlobalMatrices, PhysicsAsset->GetBodySetups()[ChildBodyIndex], ChildBodyComponentTransform))
                        {
                            FTransform JointComponentTransform;
                            JointComponentTransform.Location = GetGenerationBoneGlobalPose(MeshAsset, OverrideBoneGlobalMatrices, ChildMeshBoneIndex).GetLocation();
                            JointComponentTransform.Rotation = ChildBodyComponentTransform.Rotation;
                            JointComponentTransform.Scale = FVector::OneVector;
                            Constraint.ParentLocalFrame = MakeLocalTransformFromComponent(ParentBodyComponentTransform, JointComponentTransform);
                            Constraint.ChildLocalFrame = MakeLocalTransformFromComponent(ChildBodyComponentTransform, JointComponentTransform);
                        }

                        if (PhysicsAsset->AddConstraintSetup(Constraint) >= 0)
                        {
                            ++Result.GeneratedConstraintCount;
                            Result.bAssetChanged = true;
                        }
                    }
                    break;
                }
                ParentBoneIndex = RefSkeleton.Bones[ParentBoneIndex].ParentIndex;
            }
        }
    }

    if (!Result.bAssetChanged && !Options.bReplaceExisting)
    {
        if (OutResult)
        {
            *OutResult = Result;
        }
        return false;
    }

    if (OutResult)
    {
        *OutResult = Result;
    }
    return true;
}
