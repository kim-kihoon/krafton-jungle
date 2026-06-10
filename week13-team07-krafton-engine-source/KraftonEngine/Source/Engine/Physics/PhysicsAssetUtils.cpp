#include "Physics/PhysicsAssetUtils.h"

#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Object/FName.h"
#include "Physics/BodySetup/AggregateGeom.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsConstraintTemplate.h"
#include "Render/Types/VertexTypes.h"
#include "Math/MathUtils.h"
#include "Math/Matrix.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <limits>

namespace
{
	constexpr float AutoGenSmallNumber = 1.0e-6f;
	constexpr float AutoGenDefaultMinExtent = 0.1f;
	constexpr float AutoGenBoundsLowPercentile = 0.02f;
	constexpr float AutoGenBoundsHighPercentile = 0.98f;
	constexpr float AutoGenRadiusPercentile = 0.95f;

	struct FWeightedPoint
	{
		FVector Point = FVector::ZeroVector;
		float Weight = 1.0f;
	};

	struct FBoneAxisInfo
	{
		FVector Axis = FVector::XAxisVector;
		float Length = 0.0f;
		bool bHasUsableChild = false;
		int32 ChildCount = 0;
	};

	struct FPCAResult
	{
		FVector Mean = FVector::ZeroVector;
		FVector Axes[3] = { FVector::XAxisVector, FVector::YAxisVector, FVector::ZAxisVector };
		float EigenValues[3] = { 0.0f, 0.0f, 0.0f };
		bool bValid = false;
	};

	float SafeClampPositive(float Value, float Fallback = AutoGenDefaultMinExtent)
	{
		if (!std::isfinite(Value) || Value <= AutoGenSmallNumber)
		{
			return Fallback;
		}
		return Value;
	}

	FString ToLowerString(const FString& Value)
	{
		FString Result = Value;
		std::transform(Result.begin(), Result.end(), Result.begin(), [](unsigned char C)
		{
			return static_cast<char>(std::tolower(C));
		});
		return Result;
	}

	bool ContainsAnyToken(const FString& Name, std::initializer_list<const char*> Tokens)
	{
		const FString Lower = ToLowerString(Name);
		for (const char* Token : Tokens)
		{
			if (Lower.find(Token) != FString::npos)
			{
				return true;
			}
		}
		return false;
	}

	bool ShouldSkipBoneByName(const FString& Name)
	{
		return ContainsAnyToken(Name, {
			"ik", "twist", "helper", "socket", "weapon", "eye", "face", "cloth", "hair"
		});
	}

	bool PreferBoxForBoneName(const FString& Name)
	{
		return ContainsAnyToken(Name, {
			"root", "pelvis", "hip", "spine", "chest", "neck", "head", "hand", "foot", "ball", "clavicle"
		});
	}

	FVector GetBoneBindOrigin(const FBone& Bone)
	{
		return Bone.GetSkinBindGlobalPose().GetLocation();
	}

	FMatrix GetBoneShapeFrame(const FBone& Bone)
	{
		FMatrix Frame = Bone.GetSkinBindGlobalPose();
		Frame.RemoveScaling();
		return Frame;
	}

	FVector TransformMeshPointToBoneLocal(const FVector& MeshPoint, const FBone& Bone)
	{
		return MeshPoint * GetBoneShapeFrame(Bone).GetInverse();
	}

	void MakeBasisFromX(const FVector& InX, FVector& OutX, FVector& OutY, FVector& OutZ)
	{
		OutX = InX.GetSafeNormal(AutoGenSmallNumber, FVector::XAxisVector);
		const FVector Helper = (std::fabs(OutX.Dot(FVector::ZAxisVector)) < 0.9f)
			? FVector::ZAxisVector
			: FVector::YAxisVector;
		OutY = Helper.Cross(OutX).GetSafeNormal(AutoGenSmallNumber, FVector::YAxisVector);
		OutZ = OutX.Cross(OutY).GetSafeNormal(AutoGenSmallNumber, FVector::ZAxisVector);
		OutY = OutZ.Cross(OutX).GetSafeNormal(AutoGenSmallNumber, FVector::YAxisVector);
	}

	FRotator MakeRotationFromAxes(const FVector& XAxis, const FVector& YAxis, const FVector& ZAxis)
	{
		FMatrix Rotation = FMatrix::Identity;
		Rotation.M[0][0] = XAxis.X; Rotation.M[0][1] = XAxis.Y; Rotation.M[0][2] = XAxis.Z; Rotation.M[0][3] = 0.0f;
		Rotation.M[1][0] = YAxis.X; Rotation.M[1][1] = YAxis.Y; Rotation.M[1][2] = YAxis.Z; Rotation.M[1][3] = 0.0f;
		Rotation.M[2][0] = ZAxis.X; Rotation.M[2][1] = ZAxis.Y; Rotation.M[2][2] = ZAxis.Z; Rotation.M[2][3] = 0.0f;
		Rotation.M[3][0] = 0.0f;    Rotation.M[3][1] = 0.0f;    Rotation.M[3][2] = 0.0f;    Rotation.M[3][3] = 1.0f;
		return Rotation.ToRotator();
	}

	FRotator MakeRotationFromX(const FVector& XAxis)
	{
		FVector X, Y, Z;
		MakeBasisFromX(XAxis, X, Y, Z);
		return MakeRotationFromAxes(X, Y, Z);
	}

	FBoneAxisInfo GetBoneAxisInfo(const FSkeletalMesh& MeshAsset, int32 BoneIndex)
	{
		FBoneAxisInfo Result;
		if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(MeshAsset.Bones.size()))
		{
			return Result;
		}

		const FBone& Bone = MeshAsset.Bones[BoneIndex];
		const FMatrix MeshToBone = GetBoneShapeFrame(Bone).GetInverse();
		float BestLength = 0.0f;
		FVector BestVector = FVector::XAxisVector;

		for (int32 ChildIndex = 0; ChildIndex < static_cast<int32>(MeshAsset.Bones.size()); ++ChildIndex)
		{
			if (MeshAsset.Bones[ChildIndex].ParentIndex != BoneIndex)
			{
				continue;
			}

			++Result.ChildCount;
			const FVector ChildOriginLocal = GetBoneBindOrigin(MeshAsset.Bones[ChildIndex]) * MeshToBone;
			const float ChildLength = ChildOriginLocal.Length();
			if (ChildLength > BestLength)
			{
				BestLength = ChildLength;
				BestVector = ChildOriginLocal;
			}
		}

		Result.Length = BestLength;
		if (BestLength > AutoGenSmallNumber)
		{
			Result.Axis = BestVector / BestLength;
			Result.bHasUsableChild = true;
		}
		return Result;
	}

	void CollectWeightedPointsForBone(
		const FSkeletalMesh& MeshAsset,
		int32 BoneIndex,
		const FPhysicsAssetAutoGenerateOptions& Options,
		TArray<FWeightedPoint>& OutPoints)
	{
		OutPoints.clear();
		if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(MeshAsset.Bones.size()))
		{
			return;
		}

		const FBone& Bone = MeshAsset.Bones[BoneIndex];
		for (const FVertexPNCTBW& Vertex : MeshAsset.Vertices)
		{
			if (Options.VertexWeightingType == EPhysicsAssetAutoGenerateVertexWeightingType::DominantWeight)
			{
				int32 DominantBone = -1;
				float DominantWeight = 0.0f;
				for (int32 Influence = 0; Influence < 4; ++Influence)
				{
					if (Vertex.BoneIndices[Influence] >= 0 && Vertex.BoneWeights[Influence] > DominantWeight)
					{
						DominantBone = Vertex.BoneIndices[Influence];
						DominantWeight = Vertex.BoneWeights[Influence];
					}
				}

				if (DominantBone == BoneIndex && DominantWeight >= Options.MinWeight)
				{
					OutPoints.push_back({ TransformMeshPointToBoneLocal(Vertex.Position, Bone), DominantWeight });
				}
				continue;
			}

			float AccumulatedWeight = 0.0f;
			for (int32 Influence = 0; Influence < 4; ++Influence)
			{
				if (Vertex.BoneIndices[Influence] == BoneIndex && Vertex.BoneWeights[Influence] >= Options.MinWeight)
				{
					AccumulatedWeight += Vertex.BoneWeights[Influence];
				}
			}

			if (AccumulatedWeight > 0.0f)
			{
				OutPoints.push_back({ TransformMeshPointToBoneLocal(Vertex.Position, Bone), AccumulatedWeight });
			}
		}
	}

	bool BuildWeightedPCA(const TArray<FWeightedPoint>& Points, FPCAResult& OutPCA)
	{
		OutPCA = FPCAResult();
		if (Points.empty())
		{
			return false;
		}

		double WeightSum = 0.0;
		double Mean[3] = { 0.0, 0.0, 0.0 };
		for (const FWeightedPoint& WeightedPoint : Points)
		{
			const double W = (std::max)(0.0f, WeightedPoint.Weight);
			if (W <= 0.0)
			{
				continue;
			}
			WeightSum += W;
			Mean[0] += static_cast<double>(WeightedPoint.Point.X) * W;
			Mean[1] += static_cast<double>(WeightedPoint.Point.Y) * W;
			Mean[2] += static_cast<double>(WeightedPoint.Point.Z) * W;
		}

		if (WeightSum <= AutoGenSmallNumber)
		{
			return false;
		}

		Mean[0] /= WeightSum;
		Mean[1] /= WeightSum;
		Mean[2] /= WeightSum;
		OutPCA.Mean = FVector(static_cast<float>(Mean[0]), static_cast<float>(Mean[1]), static_cast<float>(Mean[2]));

		double A[3][3] = {};
		for (const FWeightedPoint& WeightedPoint : Points)
		{
			const double W = (std::max)(0.0f, WeightedPoint.Weight);
			if (W <= 0.0)
			{
				continue;
			}

			const double D[3] = {
				static_cast<double>(WeightedPoint.Point.X) - Mean[0],
				static_cast<double>(WeightedPoint.Point.Y) - Mean[1],
				static_cast<double>(WeightedPoint.Point.Z) - Mean[2]
			};

			for (int32 Row = 0; Row < 3; ++Row)
			{
				for (int32 Col = Row; Col < 3; ++Col)
				{
					A[Row][Col] += W * D[Row] * D[Col];
				}
			}
		}

		for (int32 Row = 0; Row < 3; ++Row)
		{
			for (int32 Col = Row; Col < 3; ++Col)
			{
				A[Row][Col] /= WeightSum;
				A[Col][Row] = A[Row][Col];
			}
		}

		double V[3][3] = {
			{ 1.0, 0.0, 0.0 },
			{ 0.0, 1.0, 0.0 },
			{ 0.0, 0.0, 1.0 }
		};

		for (int32 Iteration = 0; Iteration < 32; ++Iteration)
		{
			int32 P = 0;
			int32 Q = 1;
			double MaxOffDiag = std::fabs(A[0][1]);
			if (std::fabs(A[0][2]) > MaxOffDiag)
			{
				P = 0;
				Q = 2;
				MaxOffDiag = std::fabs(A[0][2]);
			}
			if (std::fabs(A[1][2]) > MaxOffDiag)
			{
				P = 1;
				Q = 2;
				MaxOffDiag = std::fabs(A[1][2]);
			}

			if (MaxOffDiag < 1.0e-10)
			{
				break;
			}

			const double App = A[P][P];
			const double Aqq = A[Q][Q];
			const double Apq = A[P][Q];
			const double Tau = (Aqq - App) / (2.0 * Apq);
			const double T = (Tau >= 0.0)
				? 1.0 / (Tau + std::sqrt(1.0 + Tau * Tau))
				: -1.0 / (-Tau + std::sqrt(1.0 + Tau * Tau));
			const double C = 1.0 / std::sqrt(1.0 + T * T);
			const double S = T * C;

			A[P][P] = App - T * Apq;
			A[Q][Q] = Aqq + T * Apq;
			A[P][Q] = 0.0;
			A[Q][P] = 0.0;

			for (int32 K = 0; K < 3; ++K)
			{
				if (K == P || K == Q)
				{
					continue;
				}

				const double Akp = A[K][P];
				const double Akq = A[K][Q];
				A[K][P] = C * Akp - S * Akq;
				A[P][K] = A[K][P];
				A[K][Q] = S * Akp + C * Akq;
				A[Q][K] = A[K][Q];
			}

			for (int32 K = 0; K < 3; ++K)
			{
				const double Vkp = V[K][P];
				const double Vkq = V[K][Q];
				V[K][P] = C * Vkp - S * Vkq;
				V[K][Q] = S * Vkp + C * Vkq;
			}
		}

		std::array<int32, 3> Indices = { 0, 1, 2 };
		std::sort(Indices.begin(), Indices.end(), [&](int32 AIndex, int32 BIndex)
		{
			return A[AIndex][AIndex] > A[BIndex][BIndex];
		});

		for (int32 OutputIndex = 0; OutputIndex < 3; ++OutputIndex)
		{
			const int32 EigenIndex = Indices[OutputIndex];
			OutPCA.EigenValues[OutputIndex] = static_cast<float>((std::max)(0.0, A[EigenIndex][EigenIndex]));
			OutPCA.Axes[OutputIndex] = FVector(
				static_cast<float>(V[0][EigenIndex]),
				static_cast<float>(V[1][EigenIndex]),
				static_cast<float>(V[2][EigenIndex])).GetSafeNormal(AutoGenSmallNumber,
				OutputIndex == 0 ? FVector::XAxisVector : (OutputIndex == 1 ? FVector::YAxisVector : FVector::ZAxisVector));
		}

		FVector X = OutPCA.Axes[0].GetSafeNormal(AutoGenSmallNumber, FVector::XAxisVector);
		FVector Y = OutPCA.Axes[1] - X * X.Dot(OutPCA.Axes[1]);
		Y = Y.GetSafeNormal(AutoGenSmallNumber, FVector::YAxisVector);
		FVector Z = X.Cross(Y).GetSafeNormal(AutoGenSmallNumber, FVector::ZAxisVector);
		Y = Z.Cross(X).GetSafeNormal(AutoGenSmallNumber, FVector::YAxisVector);
		OutPCA.Axes[0] = X;
		OutPCA.Axes[1] = Y;
		OutPCA.Axes[2] = Z;
		OutPCA.bValid = OutPCA.EigenValues[0] > AutoGenSmallNumber;
		return OutPCA.bValid;
	}

	float ComputeExtentAlongAxis(const TArray<FWeightedPoint>& Points, const FVector& Center, const FVector& Axis)
	{
		if (Points.empty())
		{
			return 0.0f;
		}

		float MinT = std::numeric_limits<float>::max();
		float MaxT = -std::numeric_limits<float>::max();
		for (const FWeightedPoint& Point : Points)
		{
			const float T = (Point.Point - Center).Dot(Axis);
			MinT = (std::min)(MinT, T);
			MaxT = (std::max)(MaxT, T);
		}
		return (std::max)(0.0f, MaxT - MinT);
	}

	float ComputeSortedPercentile(TArray<float>& Values, float Percentile)
	{
		if (Values.empty())
		{
			return 0.0f;
		}

		std::sort(Values.begin(), Values.end());
		if (Values.size() == 1)
		{
			return Values[0];
		}

		const float ClampedPercentile = std::clamp(Percentile, 0.0f, 1.0f);
		const float FloatIndex = ClampedPercentile * static_cast<float>(Values.size() - 1);
		const int32 LowerIndex = static_cast<int32>(std::floor(FloatIndex));
		const int32 UpperIndex = (std::min)(LowerIndex + 1, static_cast<int32>(Values.size() - 1));
		const float Alpha = FloatIndex - static_cast<float>(LowerIndex);
		return Values[LowerIndex] * (1.0f - Alpha) + Values[UpperIndex] * Alpha;
	}

	void ComputeTrimmedRangeAlongAxis(
		const TArray<FWeightedPoint>& Points,
		const FVector& Center,
		const FVector& Axis,
		float& OutMinT,
		float& OutMaxT)
	{
		TArray<float> Values;
		Values.reserve(Points.size());
		for (const FWeightedPoint& Point : Points)
		{
			Values.push_back((Point.Point - Center).Dot(Axis));
		}

		OutMinT = ComputeSortedPercentile(Values, AutoGenBoundsLowPercentile);
		OutMaxT = ComputeSortedPercentile(Values, AutoGenBoundsHighPercentile);
		if (OutMinT > OutMaxT)
		{
			std::swap(OutMinT, OutMaxT);
		}
	}

	float ComputeDistancePercentile(TArray<float>& Distances)
	{
		return ComputeSortedPercentile(Distances, AutoGenRadiusPercentile);
	}

	FVector ComputePCAExtents(const TArray<FWeightedPoint>& Points, const FPCAResult& PCA)
	{
		if (Points.empty() || !PCA.bValid)
		{
			return FVector::ZeroVector;
		}

		float MinValues[3] = {};
		float MaxValues[3] = {};
		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			ComputeTrimmedRangeAlongAxis(Points, PCA.Mean, PCA.Axes[AxisIndex], MinValues[AxisIndex], MaxValues[AxisIndex]);
		}

		return FVector(
			(std::max)(0.0f, MaxValues[0] - MinValues[0]),
			(std::max)(0.0f, MaxValues[1] - MinValues[1]),
			(std::max)(0.0f, MaxValues[2] - MinValues[2]));
	}

	EPhysicsAssetAutoGeneratePrimitiveType ChoosePrimitiveType(
		const FSkeletalMesh& MeshAsset,
		int32 BoneIndex,
		const FBoneAxisInfo& BoneAxis,
		const FPCAResult& PCA,
		const TArray<FWeightedPoint>& Points,
		const FPhysicsAssetAutoGenerateOptions& Options)
	{
		if (Options.PrimitiveType != EPhysicsAssetAutoGeneratePrimitiveType::Auto)
		{
			return Options.PrimitiveType;
		}

		const FString& BoneName = MeshAsset.Bones[BoneIndex].Name;
		if (PreferBoxForBoneName(BoneName))
		{
			return EPhysicsAssetAutoGeneratePrimitiveType::Box;
		}

		if (!PCA.bValid)
		{
			return EPhysicsAssetAutoGeneratePrimitiveType::Sphere;
		}

		const FVector Extents = ComputePCAExtents(Points, PCA);
		const float Longest = (std::max)((std::max)(Extents.X, Extents.Y), Extents.Z);
		const float Middle = Extents.X + Extents.Y + Extents.Z
			- Longest
			- (std::min)((std::min)(Extents.X, Extents.Y), Extents.Z);

		if (BoneAxis.bHasUsableChild || Longest > Middle * 1.6f)
		{
			return EPhysicsAssetAutoGeneratePrimitiveType::Capsule;
		}
		return EPhysicsAssetAutoGeneratePrimitiveType::Box;
	}

	void BuildBoxElem(
		const TArray<FWeightedPoint>& Points,
		const FPCAResult& PCA,
		const FPhysicsAssetAutoGenerateOptions& Options,
		FKBoxElem& OutBox)
	{
		float MinValues[3] = {};
		float MaxValues[3] = {};
		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			ComputeTrimmedRangeAlongAxis(Points, PCA.Mean, PCA.Axes[AxisIndex], MinValues[AxisIndex], MaxValues[AxisIndex]);
		}

		OutBox.Center = PCA.Mean;
		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			const float CenterT = (MinValues[AxisIndex] + MaxValues[AxisIndex]) * 0.5f;
			OutBox.Center += PCA.Axes[AxisIndex] * CenterT;
		}

		OutBox.Rotation = MakeRotationFromAxes(PCA.Axes[0], PCA.Axes[1], PCA.Axes[2]);
		OutBox.X = SafeClampPositive((MaxValues[0] - MinValues[0]) * Options.BoxExtentScale);
		OutBox.Y = SafeClampPositive((MaxValues[1] - MinValues[1]) * Options.BoxExtentScale);
		OutBox.Z = SafeClampPositive((MaxValues[2] - MinValues[2]) * Options.BoxExtentScale);
	}

	void BuildSphereElem(const TArray<FWeightedPoint>& Points, const FPCAResult& PCA, const FPhysicsAssetAutoGenerateOptions& Options, FKSphereElem& OutSphere)
	{
		OutSphere.Center = PCA.Mean;
		TArray<float> Distances;
		Distances.reserve(Points.size());
		for (const FWeightedPoint& Point : Points)
		{
			Distances.push_back((Point.Point - OutSphere.Center).Length());
		}
		const float Radius = ComputeDistancePercentile(Distances);
		OutSphere.Radius = SafeClampPositive(Radius * Options.RadiusScale);
	}

	FVector ChooseCapsuleAxis(
		const FBoneAxisInfo& BoneAxis,
		const FPCAResult& PCA,
		const FPhysicsAssetAutoGenerateOptions& Options)
	{
		FVector Axis = PCA.bValid ? PCA.Axes[0] : FVector::XAxisVector;
		if (Options.OrientMethod == EPhysicsAssetAutoGenerateOrientMethod::BoneAxis)
		{
			Axis = BoneAxis.bHasUsableChild ? BoneAxis.Axis : Axis;
		}
		else if (Options.OrientMethod == EPhysicsAssetAutoGenerateOrientMethod::Hybrid)
		{
			if (BoneAxis.bHasUsableChild)
			{
				Axis = BoneAxis.Axis;
			}
		}

		if (BoneAxis.bHasUsableChild && Axis.Dot(BoneAxis.Axis) < 0.0f)
		{
			Axis = -Axis;
		}
		return Axis.GetSafeNormal(AutoGenSmallNumber, FVector::XAxisVector);
	}

	void BuildCapsuleElem(
		const TArray<FWeightedPoint>& Points,
		const FPCAResult& PCA,
		const FBoneAxisInfo& BoneAxis,
		const FPhysicsAssetAutoGenerateOptions& Options,
		FKSphylElem& OutCapsule)
	{
		const FVector Axis = ChooseCapsuleAxis(BoneAxis, PCA, Options);
		const FVector CenterLineOrigin = PCA.bValid ? PCA.Mean : FVector::ZeroVector;

		float MinT = 0.0f;
		float MaxT = 0.0f;
		ComputeTrimmedRangeAlongAxis(Points, CenterLineOrigin, Axis, MinT, MaxT);

		TArray<float> Distances;
		Distances.reserve(Points.size());
		for (const FWeightedPoint& Point : Points)
		{
			const FVector Delta = Point.Point - CenterLineOrigin;
			const float T = Delta.Dot(Axis);

			const FVector Closest = CenterLineOrigin + Axis * T;
			Distances.push_back((Point.Point - Closest).Length());
		}

		float Radius = ComputeDistancePercentile(Distances);
		Radius = SafeClampPositive(Radius * Options.RadiusScale);
		const float AxisExtent = (std::max)(0.0f, MaxT - MinT) * Options.LengthScale;
		OutCapsule.Center = CenterLineOrigin + Axis * ((MinT + MaxT) * 0.5f);
		OutCapsule.Rotation = MakeRotationFromX(Axis);
		OutCapsule.Radius = Radius;
		OutCapsule.Length = (std::max)(AutoGenDefaultMinExtent, AxisExtent - Radius * 2.0f);
	}

	bool ExistingConstraintConnects(const UPhysicsAsset& PhysicsAsset, FName A, FName B)
	{
		for (int32 ConstraintIndex = 0; ConstraintIndex < PhysicsAsset.GetConstraintSetupCount(); ++ConstraintIndex)
		{
			const UPhysicsConstraintTemplate* Constraint = PhysicsAsset.GetConstraintSetup(ConstraintIndex);
			if (!Constraint)
			{
				continue;
			}

			const bool bSame = Constraint->GetParentBoneName() == A && Constraint->GetChildBoneName() == B;
			const bool bReverse = Constraint->GetParentBoneName() == B && Constraint->GetChildBoneName() == A;
			if (bSame || bReverse)
			{
				return true;
			}
		}
		return false;
	}

	FString MakeConstraintNameString(FName ParentBoneName, FName ChildBoneName, int32 Suffix)
	{
		FString Name = ParentBoneName.ToString() + "_" + ChildBoneName.ToString() + "_Constraint";
		if (Suffix > 0)
		{
			Name += "_" + std::to_string(Suffix);
		}
		return Name;
	}

	FName MakeUniqueConstraintName(const UPhysicsAsset& PhysicsAsset, FName ParentBoneName, FName ChildBoneName)
	{
		for (int32 Suffix = 0; Suffix < 10000; ++Suffix)
		{
			const FName Candidate(MakeConstraintNameString(ParentBoneName, ChildBoneName, Suffix));
			if (!PhysicsAsset.FindConstraintSetup(Candidate))
			{
				return Candidate;
			}
		}
		return FName::None;
	}

	int32 FindParentBodyIndexForBone(const UPhysicsAsset& PhysicsAsset, const FSkeletalMesh& MeshAsset, int32 BoneIndex)
	{
		if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(MeshAsset.Bones.size()))
		{
			return -1;
		}

		int32 ParentIndex = MeshAsset.Bones[BoneIndex].ParentIndex;
		while (ParentIndex >= 0 && ParentIndex < static_cast<int32>(MeshAsset.Bones.size()))
		{
			const int32 ParentBodyIndex = PhysicsAsset.FindBodyIndex(FName(MeshAsset.Bones[ParentIndex].Name));
			if (ParentBodyIndex >= 0)
			{
				return ParentBodyIndex;
			}
			ParentIndex = MeshAsset.Bones[ParentIndex].ParentIndex;
		}
		return -1;
	}

	void ClearPhysicsAsset(UPhysicsAsset& PhysicsAsset)
	{
		while (PhysicsAsset.GetConstraintSetupCount() > 0)
		{
			PhysicsAsset.RemoveConstraintSetupAt(0);
		}
		while (PhysicsAsset.GetBodySetupCount() > 0)
		{
			PhysicsAsset.RemoveBodySetupAt(0);
		}
	}
}

bool FPhysicsAssetUtils::AutoGenerateBodiesAndConstraints(
	UPhysicsAsset* PhysicsAsset,
	USkeletalMesh* SkeletalMesh,
	const FPhysicsAssetAutoGenerateOptions& Options,
	FPhysicsAssetAutoGenerateResult* OutResult)
{
	FPhysicsAssetAutoGenerateResult LocalResult;
	if (!PhysicsAsset || !SkeletalMesh)
	{
		if (OutResult)
		{
			*OutResult = LocalResult;
		}
		return false;
	}

	FSkeletalMesh* MeshAsset = SkeletalMesh->GetSkeletalMeshAsset();
	if (!MeshAsset || MeshAsset->Bones.empty() || MeshAsset->Vertices.empty())
	{
		if (OutResult)
		{
			*OutResult = LocalResult;
		}
		return false;
	}

	if (Options.bClearExistingBodies)
	{
		ClearPhysicsAsset(*PhysicsAsset);
	}

	TArray<FWeightedPoint> Points;
	TArray<int32> GeneratedBoneIndices;
	GeneratedBoneIndices.reserve(MeshAsset->Bones.size());

	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
	{
		const FBone& Bone = MeshAsset->Bones[BoneIndex];
		if (ShouldSkipBoneByName(Bone.Name))
		{
			++LocalResult.BodiesSkipped;
			continue;
		}

		CollectWeightedPointsForBone(*MeshAsset, BoneIndex, Options, Points);
		if (static_cast<int32>(Points.size()) < Options.MinVertexCount)
		{
			++LocalResult.BodiesSkipped;
			continue;
		}

		FPCAResult PCA;
		if (!BuildWeightedPCA(Points, PCA))
		{
			++LocalResult.BodiesSkipped;
			continue;
		}

		const FBoneAxisInfo BoneAxis = GetBoneAxisInfo(*MeshAsset, BoneIndex);
		const float PointCloudMeasure = std::sqrt((std::max)(0.0f, PCA.EigenValues[0])) * 2.0f;
		const float BoneMeasure = (std::max)(BoneAxis.Length, PointCloudMeasure);
		if (BoneMeasure < Options.MinBoneSize)
		{
			++LocalResult.BodiesSkipped;
			continue;
		}

		const FName BoneName(Bone.Name);
		USkeletalBodySetup* BodySetup = PhysicsAsset->AddBodySetup(BoneName);
		if (!BodySetup)
		{
			++LocalResult.BodiesSkipped;
			continue;
		}

		FKAggregateGeom& AggGeom = BodySetup->GetAggGeom();
		AggGeom.BoxElems.clear();
		AggGeom.SphereElems.clear();
		AggGeom.SphylElems.clear();

		const EPhysicsAssetAutoGeneratePrimitiveType PrimitiveType = ChoosePrimitiveType(
			*MeshAsset,
			BoneIndex,
			BoneAxis,
			PCA,
			Points,
			Options);

		switch (PrimitiveType)
		{
		case EPhysicsAssetAutoGeneratePrimitiveType::Box:
		{
			FKBoxElem Box;
			BuildBoxElem(Points, PCA, Options, Box);
			AggGeom.BoxElems.push_back(Box);
			break;
		}
		case EPhysicsAssetAutoGeneratePrimitiveType::Sphere:
		{
			FKSphereElem Sphere;
			BuildSphereElem(Points, PCA, Options, Sphere);
			AggGeom.SphereElems.push_back(Sphere);
			break;
		}
		case EPhysicsAssetAutoGeneratePrimitiveType::Capsule:
		case EPhysicsAssetAutoGeneratePrimitiveType::Auto:
		default:
		{
			FKSphylElem Capsule;
			BuildCapsuleElem(Points, PCA, BoneAxis, Options, Capsule);
			AggGeom.SphylElems.push_back(Capsule);
			break;
		}
		}

		GeneratedBoneIndices.push_back(BoneIndex);
		++LocalResult.BodiesCreated;
	}

	PhysicsAsset->UpdateBodySetupIndexMap();
	PhysicsAsset->UpdateBoundsBodiesArray();

	if (Options.bCreateConstraints)
	{
		for (int32 BoneIndex : GeneratedBoneIndices)
		{
			const FName ChildBoneName(MeshAsset->Bones[BoneIndex].Name);
			const int32 ChildBodyIndex = PhysicsAsset->FindBodyIndex(ChildBoneName);
			const int32 ParentBodyIndex = FindParentBodyIndexForBone(*PhysicsAsset, *MeshAsset, BoneIndex);
			USkeletalBodySetup* ParentBody = PhysicsAsset->GetBodySetup(ParentBodyIndex);
			USkeletalBodySetup* ChildBody = PhysicsAsset->GetBodySetup(ChildBodyIndex);
			if (!ParentBody || !ChildBody)
			{
				continue;
			}

			const FName ParentBoneName = ParentBody->GetBoneName();
			if (ExistingConstraintConnects(*PhysicsAsset, ParentBoneName, ChildBoneName))
			{
				continue;
			}

			const FName ConstraintName = MakeUniqueConstraintName(*PhysicsAsset, ParentBoneName, ChildBoneName);
			UPhysicsConstraintTemplate* Constraint = PhysicsAsset->AddConstraintSetup(ConstraintName, ParentBoneName, ChildBoneName);
			if (!Constraint)
			{
				continue;
			}

			FConstraintInstance& Instance = Constraint->GetDefaultInstance();
			Instance.ConstraintName = ConstraintName;
			Instance.ParentBoneName = ParentBoneName;
			Instance.ChildBoneName = ChildBoneName;
			Instance.bDisableCollision = Options.bDisableAdjacentCollision;
			Instance.SetLinearMotion(EConstraintMotion::Locked, EConstraintMotion::Locked, EConstraintMotion::Locked);
			Instance.SetAngularMotion(EConstraintMotion::Limited, EConstraintMotion::Limited, EConstraintMotion::Limited);
			Instance.SetAngularLimits(30.0f, 30.0f, -45.0f, 45.0f);

			if (Options.bDisableAdjacentCollision)
			{
				PhysicsAsset->DisableCollision(ParentBodyIndex, ChildBodyIndex);
			}

			++LocalResult.ConstraintsCreated;
		}
	}

	PhysicsAsset->RefreshPhysicsAssetChange();
	if (OutResult)
	{
		*OutResult = LocalResult;
	}
	return LocalResult.BodiesCreated > 0;
}
