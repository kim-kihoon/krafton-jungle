#include <fbxsdk.h>

#include "FbxImporter.h"
#include "Asset/StaticMeshTypes.h"
#include "Core/Logging/Log.h"
#include "Core/PlatformTime.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cctype>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace
{
static FVector ToFVector(const FbxVector4& V)
{
	return FVector(static_cast<float>(V[0]), static_cast<float>(V[1]), static_cast<float>(V[2]));
}

static FVector ToFVector(const FbxDouble3& V)
{
    return FVector(static_cast<float>(V[0]), static_cast<float>(V[1]), static_cast<float>(V[2]));
}

static FVector2 ToFVector2(const FbxVector2& V)
{
	// OBJ   V  
	return FVector2(static_cast<float>(V[0]), 1.0f - static_cast<float>(V[1]));
}

static void GetTangentBitangent(FVector& OutT, FVector& OutB,
	const FVector& P0, const FVector& P1, const FVector& P2,
	const FVector2& UV0, const FVector2& UV1, const FVector2& UV2)
{
	FVector E1 = P1 - P0;
	FVector E2 = P2 - P0;
	FVector2 dUV1 = UV1 - UV0;
	FVector2 dUV2 = UV2 - UV0;
	float Det = dUV1.X * dUV2.Y - dUV1.Y * dUV2.X;
	float r = (fabs(Det) > 1e-8f) ? (1.0f / Det) : 0.0f;

	OutT = (E1 * dUV2.Y - E2 * dUV1.Y) * r;
	OutB = (E2 * dUV1.X - E1 * dUV2.X) * r;
}

static FMatrix ToFMatrix(const FbxAMatrix& M)
{
	// row-vector convention
    return FMatrix(
        static_cast<float>(M.Get(0, 0)), static_cast<float>(M.Get(0, 1)), static_cast<float>(M.Get(0, 2)), static_cast<float>(M.Get(0, 3)),
        static_cast<float>(M.Get(1, 0)), static_cast<float>(M.Get(1, 1)), static_cast<float>(M.Get(1, 2)), static_cast<float>(M.Get(1, 3)),
        static_cast<float>(M.Get(2, 0)), static_cast<float>(M.Get(2, 1)), static_cast<float>(M.Get(2, 2)), static_cast<float>(M.Get(2, 3)),
        static_cast<float>(M.Get(3, 0)), static_cast<float>(M.Get(3, 1)), static_cast<float>(M.Get(3, 2)), static_cast<float>(M.Get(3, 3)));
}

static float GetUpper3x3Determinant(const FMatrix& Matrix)
{
    return
        Matrix.M[0][0] * (Matrix.M[1][1] * Matrix.M[2][2] - Matrix.M[1][2] * Matrix.M[2][1]) -
        Matrix.M[0][1] * (Matrix.M[1][0] * Matrix.M[2][2] - Matrix.M[1][2] * Matrix.M[2][0]) +
        Matrix.M[0][2] * (Matrix.M[1][0] * Matrix.M[2][1] - Matrix.M[1][1] * Matrix.M[2][0]);
}

static void ExtractLocalTransformComponents(
    const FMatrix& LocalTransform,
    FVector& OutTranslation,
    FQuat& OutRotation,
    FVector& OutScale)
{
    static constexpr float Tolerance = 1.e-8f;

    OutTranslation = LocalTransform.GetOrigin();

    const FVector XAxis = LocalTransform.GetScaledAxis(EAxis::X);
    const FVector YAxis = LocalTransform.GetScaledAxis(EAxis::Y);
    const FVector ZAxis = LocalTransform.GetScaledAxis(EAxis::Z);

    OutScale = FVector(XAxis.Size(), YAxis.Size(), ZAxis.Size());

    if (OutScale.X <= Tolerance || OutScale.Y <= Tolerance || OutScale.Z <= Tolerance)
    {
        OutRotation = FQuat(LocalTransform).GetNormalized();
        return;
    }

    FVector UnitX = XAxis / OutScale.X;
    FVector UnitY = YAxis / OutScale.Y;
    FVector UnitZ = ZAxis / OutScale.Z;

    if (GetUpper3x3Determinant(LocalTransform) < 0.0f)
    {
        OutScale.X = -OutScale.X;
        UnitX = -UnitX;
    }

    FMatrix RotationMatrix = FMatrix::Identity;
    RotationMatrix.SetAxes(UnitX, UnitY, UnitZ, FVector::ZeroVector);
    OutRotation = FQuat(RotationMatrix).GetNormalized();
}

static double GetUpper3x3Determinant(const FbxAMatrix& Matrix)
{
    return
        Matrix.Get(0, 0) * (Matrix.Get(1, 1) * Matrix.Get(2, 2) - Matrix.Get(1, 2) * Matrix.Get(2, 1)) -
        Matrix.Get(0, 1) * (Matrix.Get(1, 0) * Matrix.Get(2, 2) - Matrix.Get(1, 2) * Matrix.Get(2, 0)) +
        Matrix.Get(0, 2) * (Matrix.Get(1, 0) * Matrix.Get(2, 1) - Matrix.Get(1, 1) * Matrix.Get(2, 0));
}

static bool HasMirroredHandedness(const FbxAMatrix& Matrix)
{
    constexpr double DeterminantEpsilon = 1.e-6;
    return GetUpper3x3Determinant(Matrix) < -DeterminantEpsilon;
}

static void AppendTriangleIndices(TArray<uint32>& OutIndices, uint32 I0, uint32 I1, uint32 I2, bool bFlipWinding)
{
    OutIndices.push_back(I0);
    if (bFlipWinding)
    {
        OutIndices.push_back(I2);
        OutIndices.push_back(I1);
    }
    else
    {
        OutIndices.push_back(I1);
        OutIndices.push_back(I2);
    }
}

struct FTempInfluence
{
    int32 BoneIndex = -1;
    float Weight = 0.0f;
};

namespace FbxVertexDedupInternal
{
static uint32 FloatToStableBits(float Value)
{
    if (Value == 0.0f)
    {
        Value = 0.0f;
    }

    uint32 Bits = 0;
    std::memcpy(&Bits, &Value, sizeof(float));
    return Bits;
}

static size_t HashCombineUInt32(size_t Seed, uint32 Value)
{
    return Seed ^ (static_cast<size_t>(Value) + 0x9e3779b9u + (Seed << 6) + (Seed >> 2));
}

template <size_t Count>
static size_t HashBits(const std::array<uint32, Count>& Bits)
{
    size_t Hash = 0;
    for (uint32 Bit : Bits)
    {
        Hash = HashCombineUInt32(Hash, Bit);
    }

    return Hash;
}
}

struct FFbxStaticVertexDedupKey
{
    std::array<uint32, 12> Bits = {};

    bool operator==(const FFbxStaticVertexDedupKey& Other) const
    {
        return Bits == Other.Bits;
    }
};

struct FFbxStaticVertexDedupKeyHasher
{
    size_t operator()(const FFbxStaticVertexDedupKey& Key) const
    {
        return FbxVertexDedupInternal::HashBits(Key.Bits);
    }
};

struct FFbxSkeletalVertexDedupKey
{
    std::array<uint32, 20> Bits = {};

    bool operator==(const FFbxSkeletalVertexDedupKey& Other) const
    {
        return Bits == Other.Bits;
    }
};

struct FFbxSkeletalVertexDedupKeyHasher
{
    size_t operator()(const FFbxSkeletalVertexDedupKey& Key) const
    {
        return FbxVertexDedupInternal::HashBits(Key.Bits);
    }
};

static FFbxStaticVertexDedupKey MakeStaticVertexDedupKey(const FNormalVertex& Vertex)
{
    using namespace FbxVertexDedupInternal;

    FFbxStaticVertexDedupKey Key;
    Key.Bits = {
        FloatToStableBits(Vertex.Position.X),
        FloatToStableBits(Vertex.Position.Y),
        FloatToStableBits(Vertex.Position.Z),
        FloatToStableBits(Vertex.Color.R),
        FloatToStableBits(Vertex.Color.G),
        FloatToStableBits(Vertex.Color.B),
        FloatToStableBits(Vertex.Color.A),
        FloatToStableBits(Vertex.Normal.X),
        FloatToStableBits(Vertex.Normal.Y),
        FloatToStableBits(Vertex.Normal.Z),
        FloatToStableBits(Vertex.UVs.X),
        FloatToStableBits(Vertex.UVs.Y),
    };

    return Key;
}

static FFbxSkeletalVertexDedupKey MakeSkeletalVertexDedupKey(const FSkeletalMeshVertex& Vertex)
{
    using namespace FbxVertexDedupInternal;

    FFbxSkeletalVertexDedupKey Key;
    Key.Bits = {
        FloatToStableBits(Vertex.Position.X),
        FloatToStableBits(Vertex.Position.Y),
        FloatToStableBits(Vertex.Position.Z),
        FloatToStableBits(Vertex.Color.R),
        FloatToStableBits(Vertex.Color.G),
        FloatToStableBits(Vertex.Color.B),
        FloatToStableBits(Vertex.Color.A),
        FloatToStableBits(Vertex.Normal.X),
        FloatToStableBits(Vertex.Normal.Y),
        FloatToStableBits(Vertex.Normal.Z),
        FloatToStableBits(Vertex.UVs.X),
        FloatToStableBits(Vertex.UVs.Y),
        static_cast<uint32>(Vertex.BoneIndices[0]),
        static_cast<uint32>(Vertex.BoneIndices[1]),
        static_cast<uint32>(Vertex.BoneIndices[2]),
        static_cast<uint32>(Vertex.BoneIndices[3]),
        FloatToStableBits(Vertex.BoneWeights[0]),
        FloatToStableBits(Vertex.BoneWeights[1]),
        FloatToStableBits(Vertex.BoneWeights[2]),
        FloatToStableBits(Vertex.BoneWeights[3]),
    };

    return Key;
}

static void DeduplicateStaticMeshVertices(FStaticMesh* Mesh)
{
    if (!Mesh || Mesh->Vertices.empty() || Mesh->Indices.empty())
    {
        return;
    }

    const uint64 OriginalVertexCount = Mesh->Vertices.size();

    TArray<FNormalVertex> DedupedVertices;
    DedupedVertices.reserve(Mesh->Vertices.size());

    TArray<uint32> RemappedIndices;
    RemappedIndices.reserve(Mesh->Indices.size());

    TMap<FFbxStaticVertexDedupKey, uint32, FFbxStaticVertexDedupKeyHasher> VertexToIndex;
    VertexToIndex.reserve(Mesh->Indices.size());

    for (uint32 OldIndex : Mesh->Indices)
    {
        if (OldIndex >= Mesh->Vertices.size())
        {
            UE_LOG_WARNING("[FbxImporter] Skip invalid static mesh index during dedup: %u", OldIndex);
            continue;
        }

        const FNormalVertex& Vertex = Mesh->Vertices[OldIndex];
        const FFbxStaticVertexDedupKey Key = MakeStaticVertexDedupKey(Vertex);

        auto Found = VertexToIndex.find(Key);
        if (Found != VertexToIndex.end())
        {
            RemappedIndices.push_back(Found->second);
            continue;
        }

        const uint32 NewIndex = static_cast<uint32>(DedupedVertices.size());
        DedupedVertices.push_back(Vertex);
        VertexToIndex.emplace(Key, NewIndex);
        RemappedIndices.push_back(NewIndex);
    }

    Mesh->Vertices = std::move(DedupedVertices);
    Mesh->Indices = std::move(RemappedIndices);

    if (Mesh->Vertices.size() < OriginalVertexCount)
    {
        UE_LOG("[FbxImporter] Static vertex dedup: %zu -> %zu",
               static_cast<size_t>(OriginalVertexCount),
               Mesh->Vertices.size());
    }
}

static void DeduplicateSkeletalMeshVertices(FSkeletalMesh* Mesh)
{
    if (!Mesh || Mesh->Vertices.empty() || Mesh->Indices.empty())
    {
        return;
    }

    const uint64 OriginalVertexCount = Mesh->Vertices.size();

    TArray<FSkeletalMeshVertex> DedupedVertices;
    DedupedVertices.reserve(Mesh->Vertices.size());

    TArray<uint32> RemappedIndices;
    RemappedIndices.reserve(Mesh->Indices.size());

    TMap<FFbxSkeletalVertexDedupKey, uint32, FFbxSkeletalVertexDedupKeyHasher> VertexToIndex;
    VertexToIndex.reserve(Mesh->Indices.size());

    for (uint32 OldIndex : Mesh->Indices)
    {
        if (OldIndex >= Mesh->Vertices.size())
        {
            UE_LOG_WARNING("[FbxImporter] Skip invalid skeletal mesh index during dedup: %u", OldIndex);
            continue;
        }

        const FSkeletalMeshVertex& Vertex = Mesh->Vertices[OldIndex];
        const FFbxSkeletalVertexDedupKey Key = MakeSkeletalVertexDedupKey(Vertex);

        auto Found = VertexToIndex.find(Key);
        if (Found != VertexToIndex.end())
        {
            RemappedIndices.push_back(Found->second);
            continue;
        }

        const uint32 NewIndex = static_cast<uint32>(DedupedVertices.size());
        DedupedVertices.push_back(Vertex);
        VertexToIndex.emplace(Key, NewIndex);
        RemappedIndices.push_back(NewIndex);
    }

    Mesh->Vertices = std::move(DedupedVertices);
    Mesh->Indices = std::move(RemappedIndices);

    if (Mesh->Vertices.size() < OriginalVertexCount)
    {
        UE_LOG("[FbxImporter] Skeletal vertex dedup: %zu -> %zu",
               static_cast<size_t>(OriginalVertexCount),
               Mesh->Vertices.size());
    }
}

/**
 * @brief   4 bone FSkeletalMeshVertex 
 */
static void AssignTop4Influences(
    const TArray<FTempInfluence>& SourceInfluences,
    FSkeletalMeshVertex& OutVertex)
{
    for (int32 i = 0; i < 4; ++i)
    {
        OutVertex.BoneIndices[i] = 0;
        OutVertex.BoneWeights[i] = 0.0f;
    }

    if (SourceInfluences.empty())
    {
        return;
    }

	//   standard sort 
    TArray<FTempInfluence> Sorted = SourceInfluences;
    std::sort(Sorted.begin(), Sorted.end(), [](const FTempInfluence& A, const FTempInfluence& B)
                { return A.Weight > B.Weight; });

    int32 WrittenCount = 0;
    float Sum = 0.0f;

    for (const FTempInfluence& Influence : Sorted)
    {
        if (WrittenCount >= 4)
        {
            break;
        }

		/*
		 * note:  BoneIndices uint8 bone  256  
		 */
        if (Influence.BoneIndex < 0 || Influence.BoneIndex > 255 || Influence.Weight <= 0.0f)
        {
            continue;
        }

        OutVertex.BoneIndices[WrittenCount] = static_cast<uint8>(Influence.BoneIndex);
        OutVertex.BoneWeights[WrittenCount] = Influence.Weight;
        Sum += Influence.Weight;

        ++WrittenCount;
    }

    if (Sum <= 1e-6f)
    {
        for (int32 i = 0; i < 4; ++i)
        {
            OutVertex.BoneIndices[i] = 0;
            OutVertex.BoneWeights[i] = 0.0f;
        }

        return;
    }

    for (int32 i = 0; i < WrittenCount; ++i)
    {
        OutVertex.BoneWeights[i] /= Sum;
    }
}

/**
 * @brief FBX node transform   mesh geometry   
 *          transform     
 */
static FbxAMatrix GetGeometryTransform(FbxNode* Node)
{
    FbxAMatrix Geometry;
    Geometry.SetIdentity();

    if (!Node)
    {
        return Geometry;
    }

    const FbxVector4 T = Node->GetGeometricTranslation(FbxNode::eSourcePivot);
    const FbxVector4 R = Node->GetGeometricRotation(FbxNode::eSourcePivot);
    const FbxVector4 S = Node->GetGeometricScaling(FbxNode::eSourcePivot);

    Geometry.SetTRS(T, R, S);
    return Geometry;
}

/**
 * @brief normal translate  
 */
static FbxAMatrix GetNormalTransform(FbxAMatrix Matrix)
{
    Matrix.SetT(FbxVector4(0, 0, 0, 0));
    return Matrix;
}

static FbxAMatrix MakeIdentityFbxMatrix()
{
    FbxAMatrix Matrix;
    Matrix.SetIdentity();
    return Matrix;
}

static FbxAMatrix GetGlobalTransformWithGeometry(FbxNode* Node)
{
    if (!Node)
    {
        return MakeIdentityFbxMatrix();
    }

    const FbxAMatrix GlobalTransform = Node->EvaluateGlobalTransform();
    const FbxAMatrix GeometryTransform = GetGeometryTransform(Node);

    //  Static Mesh importer  
    return GlobalTransform * GeometryTransform;
}

static FbxAMatrix GetNormalTransformFromPositionTransform(FbxAMatrix Matrix)
{
    Matrix.SetT(FbxVector4(0, 0, 0, 0));
    return Matrix;
}

static int32 FindNearestImportedBoneIndex(
    FbxNode* StartNode,
    const TMap<FbxNode*, int32>& BoneNodeToIndex)
{
    FbxNode* Current = StartNode;
    while (Current)
    {
        auto It = BoneNodeToIndex.find(Current);
        if (It != BoneNodeToIndex.end())
        {
            return It->second;
        }

        Current = Current->GetParent();
    }

    return -1;
}

static std::string ToLowerCopy(const char* InName)
{
    std::string Result = InName ? InName : "";
    std::transform(Result.begin(), Result.end(), Result.begin(), [](unsigned char C)
                    { return static_cast<char>(std::tolower(C)); });
    return Result;
}

static bool ContainsAnyToken(const std::string& Name, const std::initializer_list<const char*> Tokens)
{
    for (const char* Token : Tokens)
    {
        if (Name.find(Token) != std::string::npos)
        {
            return true;
        }
    }

    return false;
}

static bool ShouldSkipRigidMeshByName(FbxNode* OwnerNode)
{
    const std::string Name = ToLowerCopy(OwnerNode ? OwnerNode->GetName() : "");

    // helper / reference    skip
    return ContainsAnyToken(Name, { "floor",
                                    "ground",
                                    "grid",
                                    "reference",
                                    "helper",
                                    "collision",
                                    "collider",
                                    "dummy" });
}

static bool HasValidSkinInfluence(FbxMesh* Mesh)
{
    if (!Mesh)
    {
        return false;
    }

    const int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
    for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
    {
        FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
        if (!Skin)
        {
            continue;
        }

        const int32 ClusterCount = Skin->GetClusterCount();
        for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
        {
            FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
            if (!Cluster || !Cluster->GetLink())
            {
                continue;
            }

            const int32 IndexCount = Cluster->GetControlPointIndicesCount();
            double* Weights = Cluster->GetControlPointWeights();
            if (IndexCount <= 0 || !Weights)
            {
                continue;
            }

            for (int32 Index = 0; Index < IndexCount; ++Index)
            {
                if (Weights[Index] > 0.0)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

static void AddUniqueFbxNode(TArray<FbxNode*>& Nodes, FbxNode* Node)
{
    if (!Node || std::find(Nodes.begin(), Nodes.end(), Node) != Nodes.end())
    {
        return;
    }

    Nodes.push_back(Node);
}

static void CollectSkinnedClusterBoneNodesRecursive(FbxNode* Node, TArray<FbxNode*>& OutNodes)
{
    if (!Node)
    {
        return;
    }

    if (FbxMesh* Mesh = Node->GetMesh())
    {
        const int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
        for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
        {
            FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
            if (!Skin)
            {
                continue;
            }

            const int32 ClusterCount = Skin->GetClusterCount();
            for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
            {
                FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
                if (!Cluster || !Cluster->GetLink())
                {
                    continue;
                }

                AddUniqueFbxNode(OutNodes, Cluster->GetLink());
            }
        }
    }

    for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
    {
        CollectSkinnedClusterBoneNodesRecursive(Node->GetChild(ChildIndex), OutNodes);
    }
}

static void InspectMeshContentRecursive(FbxNode* Node, FFbxMeshContentInfo& OutInfo)
{
    if (!Node)
    {
        return;
    }

    if (OutInfo.bHasStaticMesh && OutInfo.bHasSkeletalMesh)
    {
        return;
    }

    if (FbxMesh* Mesh = Node->GetMesh())
    {
        const bool bHasGeometry =
            Mesh->GetControlPointsCount() > 0 &&
            Mesh->GetPolygonCount() > 0;

        if (bHasGeometry)
        {
            if (HasValidSkinInfluence(Mesh))
            {
                OutInfo.bHasSkeletalMesh = true;
            }
            else
            {
                OutInfo.bHasStaticMesh = true;
            }
        }
    }

    if (OutInfo.bHasStaticMesh && OutInfo.bHasSkeletalMesh)
    {
        return;
    }

    for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
    {
        InspectMeshContentRecursive(Node->GetChild(ChildIndex), OutInfo);

        if (OutInfo.bHasStaticMesh && OutInfo.bHasSkeletalMesh)
        {
			//   true  early exit
            return;
        }
    }
}

static void ResetVertexInfluences(FSkeletalMeshVertex& Vertex)
{
    for (int32 i = 0; i < 4; ++i)
    {
        Vertex.BoneIndices[i] = 0;
        Vertex.BoneWeights[i] = 0.0f;
    }
}

static void AssignRigidInfluence(FSkeletalMeshVertex& Vertex, int32 BoneIndex)
{
    ResetVertexInfluences(Vertex);

    if (BoneIndex < 0 || BoneIndex > 255)
    {
        /*
         * note:  BoneIndices uint8 BoneIndex 255  
         */
        return;
    }

    Vertex.BoneIndices[0] = static_cast<uint8>(BoneIndex);
    Vertex.BoneWeights[0] = 1.0f;
}

static EAnimationRotationOrder ToAnimationRotationOrder(EFbxRotationOrder Order)
{
    switch (Order)
    {
    case eEulerXYZ: return EAnimationRotationOrder::XYZ;
    case eEulerXZY: return EAnimationRotationOrder::XZY;
    case eEulerYZX: return EAnimationRotationOrder::YZX;
    case eEulerYXZ: return EAnimationRotationOrder::YXZ;
    case eEulerZXY: return EAnimationRotationOrder::ZXY;
    case eEulerZYX: return EAnimationRotationOrder::ZYX;
    case eSphericXYZ: return EAnimationRotationOrder::Spheric;
    default: return EAnimationRotationOrder::XYZ;
    }
}

static ECurveInterpMode ToAnimationInterpMode(FbxAnimCurveDef::EInterpolationType Interpolation)
{
    switch (Interpolation)
    {
    case FbxAnimCurveDef::eInterpolationConstant: return ECurveInterpMode::Constant;
    case FbxAnimCurveDef::eInterpolationLinear: return ECurveInterpMode::Linear;
    case FbxAnimCurveDef::eInterpolationCubic: return ECurveInterpMode::Cubic;
    default: return ECurveInterpMode::Linear;
    }
}

static ECurveTangentMode ToAnimationTangentMode(FbxAnimCurveDef::ETangentMode TangentMode)
{
    const int32 Mode = static_cast<int32>(TangentMode);
    if ((Mode & static_cast<int32>(FbxAnimCurveDef::eTangentGenericBreak)) != 0)
    {
        return ECurveTangentMode::Break;
    }
    if ((Mode & static_cast<int32>(FbxAnimCurveDef::eTangentUser)) != 0)
    {
        return ECurveTangentMode::User;
    }
    return ECurveTangentMode::Auto;
}

static bool HasAnyKeys(const FAnimationFloatCurve& Curve)
{
    return Curve.bHasCurve && !Curve.Keys.empty();
}

static void SortAnimationCurveKeys(FAnimationFloatCurve& Curve)
{
    std::sort(
        Curve.Keys.begin(),
        Curve.Keys.end(),
        [](const FAnimationCurveKey& A, const FAnimationCurveKey& B)
        {
            return A.TimeSeconds < B.TimeSeconds;
        });
}

static void SetupAnimationCurveDefaults(
    FAnimationFloatCurve& Curve,
    EAnimationCurveChannel Channel,
    float DefaultValue)
{
    Curve.Channel = Channel;
    Curve.DefaultValue = DefaultValue;
    Curve.bHasCurve = false;
    Curve.Keys.clear();
}

static float SanitizeOptionalCurveMetadata(float Value)
{
    return std::isfinite(Value) ? Value : 0.0f;
}

static void ExtractAnimationFloatCurve(FbxAnimCurve* SourceCurve, FAnimationFloatCurve& OutCurve, float ValueScale = 1.0f)
{
    if (!SourceCurve)
    {
        return;
    }

    const int32 KeyCount = SourceCurve->KeyGetCount();
    if (KeyCount <= 0)
    {
        return;
    }

    OutCurve.bHasCurve = true;
    OutCurve.Keys.reserve(KeyCount);

    for (int32 KeyIndex = 0; KeyIndex < KeyCount; ++KeyIndex)
    {
        FbxAnimCurveKey FbxKey = SourceCurve->KeyGet(KeyIndex);
        const FbxAnimCurveDef::EInterpolationType Interpolation = SourceCurve->KeyGetInterpolation(KeyIndex);
        const FbxAnimCurveDef::EConstantMode ConstantMode = FbxKey.GetConstantMode();
        const FbxAnimCurveDef::ETangentMode TangentMode = FbxKey.GetTangentMode(true);
        const FbxAnimCurveDef::EWeightedMode WeightedMode = FbxKey.GetTangentWeightMode();
        const FbxAnimCurveDef::EVelocityMode VelocityMode = FbxKey.GetTangentVelocityMode();

        FAnimationCurveKey Key;
        Key.TimeSeconds = static_cast<float>(SourceCurve->KeyGetTime(KeyIndex).GetSecondDouble());
        Key.Value = SourceCurve->KeyGetValue(KeyIndex) * ValueScale;
        Key.InterpMode = ToAnimationInterpMode(Interpolation);
        Key.TangentMode = ToAnimationTangentMode(TangentMode);
        Key.ArriveTangent = SanitizeOptionalCurveMetadata(SourceCurve->KeyGetLeftDerivative(KeyIndex));
        Key.LeaveTangent = SanitizeOptionalCurveMetadata(SourceCurve->KeyGetRightDerivative(KeyIndex));
        Key.ArriveTangentWeight = SanitizeOptionalCurveMetadata(SourceCurve->KeyGetLeftTangentWeight(KeyIndex));
        Key.LeaveTangentWeight = SanitizeOptionalCurveMetadata(SourceCurve->KeyGetRightTangentWeight(KeyIndex));
        Key.ArriveTangentVelocity = SanitizeOptionalCurveMetadata(SourceCurve->KeyGetLeftTangentVelocity(KeyIndex));
        Key.LeaveTangentVelocity = SanitizeOptionalCurveMetadata(SourceCurve->KeyGetRightTangentVelocity(KeyIndex));
        Key.bArriveWeighted = (static_cast<int32>(WeightedMode) & static_cast<int32>(FbxAnimCurveDef::eWeightedNextLeft)) != 0;
        Key.bLeaveWeighted = (static_cast<int32>(WeightedMode) & static_cast<int32>(FbxAnimCurveDef::eWeightedRight)) != 0;
        Key.bArriveHasVelocity = (static_cast<int32>(VelocityMode) & static_cast<int32>(FbxAnimCurveDef::eVelocityNextLeft)) != 0;
        Key.bLeaveHasVelocity = (static_cast<int32>(VelocityMode) & static_cast<int32>(FbxAnimCurveDef::eVelocityRight)) != 0;
        Key.RawInterpolation = static_cast<int32>(Interpolation);
        Key.RawConstantMode = static_cast<int32>(ConstantMode);
        Key.RawTangentMode = static_cast<int32>(TangentMode);

        OutCurve.Keys.push_back(Key);
    }

    SortAnimationCurveKeys(OutCurve);
}

static int32 CalculateAnimationFrameCount(float StartTimeSeconds, float EndTimeSeconds, float SourceFrameRate)
{
    if (SourceFrameRate <= 0.0f)
    {
        SourceFrameRate = 30.0f;
    }

    const float DurationSeconds = std::max(0.0f, EndTimeSeconds - StartTimeSeconds);
    return std::max(1, static_cast<int32>(std::floor(DurationSeconds * SourceFrameRate)) + 1);
}

static bool AreAllVectorKeysEqual(const TArray<FVector>& Keys)
{
    if (Keys.size() <= 1)
    {
        return true;
    }

    const FVector& First = Keys[0];
    for (size_t KeyIndex = 1; KeyIndex < Keys.size(); ++KeyIndex)
    {
        if (!Keys[KeyIndex].Equals(First))
        {
            return false;
        }
    }
    return true;
}

static bool AreAllQuatKeysEqual(const TArray<FQuat>& Keys)
{
    if (Keys.size() <= 1)
    {
        return true;
    }

    const FQuat& First = Keys[0];
    for (size_t KeyIndex = 1; KeyIndex < Keys.size(); ++KeyIndex)
    {
        if (!Keys[KeyIndex].Equals(First))
        {
            return false;
        }
    }
    return true;
}

static void CompressConstantRawAnimSequenceTrack(FRawAnimSequenceTrack& Track)
{
    if (AreAllVectorKeysEqual(Track.PosKeys) && Track.PosKeys.size() > 1)
    {
        Track.PosKeys.resize(1);
    }
    if (AreAllQuatKeysEqual(Track.RotKeys) && Track.RotKeys.size() > 1)
    {
        Track.RotKeys.resize(1);
    }
    if (AreAllVectorKeysEqual(Track.ScaleKeys) && Track.ScaleKeys.size() > 1)
    {
        Track.ScaleKeys.resize(1);
    }
}

static bool IsSkeletonNode(FbxNode* Node)
{
    if (!Node)
    {
        return false;
    }

    FbxNodeAttribute* Attr = Node->GetNodeAttribute();
    return Attr && Attr->GetAttributeType() == FbxNodeAttribute::eSkeleton;
}

static FbxNode* FindTopmostSkeletonNode(FbxNode* Node)
{
    if (!IsSkeletonNode(Node))
    {
        return nullptr;
    }

    FbxNode* Root = Node;
    while (Root->GetParent() && IsSkeletonNode(Root->GetParent()))
    {
        Root = Root->GetParent();
    }

    return Root;
}

static FbxNode* FindNearestSkeletonAncestor(FbxNode* Node)
{
    for (FbxNode* Current = Node; Current; Current = Current->GetParent())
    {
        if (FbxNode* Root = FindTopmostSkeletonNode(Current))
        {
            return Root;
        }
    }

    return nullptr;
}

static void CollectSkeletonNodeNames(FbxNode* Node, TArray<FString>& OutBoneNodeNames)
{
    if (!Node)
    {
        return;
    }

    if (IsSkeletonNode(Node))
    {
        OutBoneNodeNames.push_back(FString(Node->GetName()));
    }

    for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
    {
        CollectSkeletonNodeNames(Node->GetChild(ChildIndex), OutBoneNodeNames);
    }
}

static bool HasSkeletonDesc(const FFbxImportedAssetSet& AssetSet, const FString& RootNodeName)
{
    return std::any_of(
        AssetSet.Skeletons.begin(),
        AssetSet.Skeletons.end(),
        [&RootNodeName](const FFbxSkeletonImportDesc& Desc)
        {
            return Desc.RootNodeName == RootNodeName;
        });
}

static void AddSkeletonDesc(FbxNode* RootNode, FFbxImportedAssetSet& AssetSet)
{
    if (!RootNode)
    {
        return;
    }

    const FString RootNodeName(RootNode->GetName());
    if (HasSkeletonDesc(AssetSet, RootNodeName))
    {
        return;
    }

    FFbxSkeletonImportDesc Desc;
    Desc.RootNodeName = RootNodeName;
    CollectSkeletonNodeNames(RootNode, Desc.BoneNodeNames);
    AssetSet.Skeletons.push_back(Desc);
}

static TArray<FbxNode*> CollectSkinSkeletonRoots(FbxMesh* Mesh)
{
    TArray<FbxNode*> Roots;
    if (!Mesh)
    {
        return Roots;
    }

    const int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
    for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
    {
        FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
        if (!Skin)
        {
            continue;
        }

        const int32 ClusterCount = Skin->GetClusterCount();
        for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
        {
            FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
            if (!Cluster || !Cluster->GetLink())
            {
                continue;
            }

            FbxNode* Root = FindTopmostSkeletonNode(Cluster->GetLink());
            if (!Root)
            {
                continue;
            }

            if (std::find(Roots.begin(), Roots.end(), Root) == Roots.end())
            {
                Roots.push_back(Root);
            }
        }
    }

    std::sort(
        Roots.begin(),
        Roots.end(),
        [](const FbxNode* A, const FbxNode* B)
        {
            return FString(A ? A->GetName() : "") < FString(B ? B->GetName() : "");
        });

    return Roots;
}

static void SortImportedAssetSet(FFbxImportedAssetSet& AssetSet)
{
    std::sort(
        AssetSet.Skeletons.begin(),
        AssetSet.Skeletons.end(),
        [](const FFbxSkeletonImportDesc& A, const FFbxSkeletonImportDesc& B)
        {
            return A.RootNodeName < B.RootNodeName;
        });

    std::sort(
        AssetSet.SkeletalMeshes.begin(),
        AssetSet.SkeletalMeshes.end(),
        [](const FFbxSkeletalMeshImportDesc& A, const FFbxSkeletalMeshImportDesc& B)
        {
            if (A.SkeletonRootNodeName != B.SkeletonRootNodeName)
            {
                return A.SkeletonRootNodeName < B.SkeletonRootNodeName;
            }
            return A.MeshNodeName < B.MeshNodeName;
        });

    std::sort(
        AssetSet.Animations.begin(),
        AssetSet.Animations.end(),
        [](const FFbxAnimationImportDesc& A, const FFbxAnimationImportDesc& B)
        {
            if (A.StackIndex != B.StackIndex)
            {
                return A.StackIndex < B.StackIndex;
            }
            return A.SequenceName < B.SequenceName;
        });
}

static void AnalyzeSceneRecursive(FbxNode* Node, FFbxImportedAssetSet& AssetSet)
{
    if (!Node)
    {
        return;
    }

    if (FindTopmostSkeletonNode(Node) == Node)
    {
        AddSkeletonDesc(Node, AssetSet);
    }

    if (FbxMesh* Mesh = Node->GetMesh())
    {
        const bool bHasGeometry =
            Mesh->GetControlPointsCount() > 0 &&
            Mesh->GetPolygonCount() > 0;

        if (bHasGeometry)
        {
            const bool bSkinned = HasValidSkinInfluence(Mesh);
            if (bSkinned)
            {
                AssetSet.MeshContentInfo.bHasSkeletalMesh = true;

                TArray<FbxNode*> Roots = CollectSkinSkeletonRoots(Mesh);
                for (FbxNode* Root : Roots)
                {
                    AddSkeletonDesc(Root, AssetSet);
                }

                FFbxSkeletalMeshImportDesc Desc;
                Desc.MeshNodeName = FString(Node->GetName());
                Desc.SkeletonRootNodeName = Roots.empty() ? FString() : FString(Roots[0]->GetName());
                Desc.MaterialSlotCount = Node->GetMaterialCount();
                Desc.bSkinned = true;
                AssetSet.SkeletalMeshes.push_back(Desc);
            }
            else if (FbxNode* Root = FindNearestSkeletonAncestor(Node->GetParent()))
            {
                AssetSet.MeshContentInfo.bHasSkeletalMesh = true;
                AddSkeletonDesc(Root, AssetSet);

                FFbxSkeletalMeshImportDesc Desc;
                Desc.MeshNodeName = FString(Node->GetName());
                Desc.SkeletonRootNodeName = FString(Root->GetName());
                Desc.MaterialSlotCount = Node->GetMaterialCount();
                Desc.bRigidAttached = true;
                AssetSet.SkeletalMeshes.push_back(Desc);
            }
            else
            {
                AssetSet.MeshContentInfo.bHasStaticMesh = true;
            }
        }
    }

    for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
    {
        AnalyzeSceneRecursive(Node->GetChild(ChildIndex), AssetSet);
    }
}

static void CollectSkeletonNodesPreOrder(FbxNode* Node, TArray<FbxNode*>& OutNodes)
{
    if (!Node)
    {
        return;
    }

    if (IsSkeletonNode(Node))
    {
        OutNodes.push_back(Node);
    }

    for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
    {
        CollectSkeletonNodesPreOrder(Node->GetChild(ChildIndex), OutNodes);
    }
}

static FSkeleton* BuildSkeletonFromRoot(FbxNode* RootNode, const FString& SourcePath)
{
    if (!RootNode)
    {
        return nullptr;
    }

    TArray<FbxNode*> BoneNodes;
    CollectSkeletonNodesPreOrder(RootNode, BoneNodes);
    if (BoneNodes.empty())
    {
        return nullptr;
    }

    FSkeleton* Skeleton = new FSkeleton();
    Skeleton->PathFileName = SourcePath;
    Skeleton->RootNodeName = FString(RootNode->GetName());
    Skeleton->Bones.reserve(BoneNodes.size());

    TMap<FbxNode*, int32> BoneNodeToIndex;
    for (FbxNode* BoneNode : BoneNodes)
    {
        const int32 BoneIndex = static_cast<int32>(Skeleton->Bones.size());
        BoneNodeToIndex[BoneNode] = BoneIndex;

        FBoneInfo Bone = {};
        Bone.Name = FString(BoneNode->GetName());
        Bone.ParentIndex = -1;
        Bone.GlobalBindTransform = ToFMatrix(BoneNode->EvaluateGlobalTransform());
        Bone.InverseBindPose = Bone.GlobalBindTransform.GetInverse();
        Bone.LocalBindTransform = Bone.GlobalBindTransform;

        Skeleton->Bones.push_back(Bone);
    }

    for (FbxNode* BoneNode : BoneNodes)
    {
        auto BoneIt = BoneNodeToIndex.find(BoneNode);
        if (BoneIt == BoneNodeToIndex.end())
        {
            continue;
        }

        const int32 BoneIndex = BoneIt->second;
        int32 ParentIndex = -1;
        for (FbxNode* ParentNode = BoneNode->GetParent(); ParentNode; ParentNode = ParentNode->GetParent())
        {
            auto ParentIt = BoneNodeToIndex.find(ParentNode);
            if (ParentIt != BoneNodeToIndex.end())
            {
                ParentIndex = ParentIt->second;
                break;
            }
        }

        FBoneInfo& Bone = Skeleton->Bones[BoneIndex];
        Bone.ParentIndex = ParentIndex;
        if (ParentIndex >= 0)
        {
            const FMatrix ParentGlobalInv = Skeleton->Bones[ParentIndex].GlobalBindTransform.GetInverse();
            Bone.LocalBindTransform = Bone.GlobalBindTransform * ParentGlobalInv;
        }
        else
        {
            Bone.LocalBindTransform = Bone.GlobalBindTransform;
        }
    }

    return Skeleton;
}

static void CollectTopmostSkeletonRootNodes(FbxNode* Node, TArray<FbxNode*>& OutRootNodes)
{
    if (!Node)
    {
        return;
    }

    if (FindTopmostSkeletonNode(Node) == Node)
    {
        OutRootNodes.push_back(Node);
        return;
    }

    for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
    {
        CollectTopmostSkeletonRootNodes(Node->GetChild(ChildIndex), OutRootNodes);
    }
}

static FbxNode* FindNearestIncludedParentNode(FbxNode* BoneNode, const TArray<FbxNode*>& IncludedNodes)
{
    for (FbxNode* ParentNode = BoneNode ? BoneNode->GetParent() : nullptr; ParentNode; ParentNode = ParentNode->GetParent())
    {
        if (std::find(IncludedNodes.begin(), IncludedNodes.end(), ParentNode) != IncludedNodes.end())
        {
            return ParentNode;
        }
    }

    return nullptr;
}

static void ExtractRawAnimSequenceTrack(
    FbxAnimLayer* AnimLayer,
    FbxNode* BoneNode,
    FbxNode* ParentBoneNode,
    const FAnimationSequence& Sequence,
    FRawAnimSequenceTrack& OutTrack)
{
    if (!AnimLayer || !BoneNode)
    {
        return;
    }

    const float FrameRate = Sequence.SourceFrameRate > 0.0f ? Sequence.SourceFrameRate : 30.0f;
    const int32 FrameCount = CalculateAnimationFrameCount(
        Sequence.StartTimeSeconds,
        Sequence.EndTimeSeconds,
        FrameRate);

    OutTrack.PosKeys.clear();
    OutTrack.RotKeys.clear();
    OutTrack.ScaleKeys.clear();
    OutTrack.PosKeys.reserve(FrameCount);
    OutTrack.RotKeys.reserve(FrameCount);
    OutTrack.ScaleKeys.reserve(FrameCount);

    for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
    {
        const double SampleTimeSeconds = std::min<double>(
            Sequence.EndTimeSeconds,
            static_cast<double>(Sequence.StartTimeSeconds) + static_cast<double>(FrameIndex) / static_cast<double>(FrameRate));

        FbxTime SampleTime;
        SampleTime.SetSecondDouble(SampleTimeSeconds);

        FVector Translation;
        FQuat Rotation;
        FVector Scale;
        FMatrix SampleTransform = ToFMatrix(BoneNode->EvaluateGlobalTransform(SampleTime));
        if (ParentBoneNode)
        {
            const FMatrix ParentTransform = ToFMatrix(ParentBoneNode->EvaluateGlobalTransform(SampleTime));
            SampleTransform = SampleTransform * ParentTransform.GetInverse();
        }
        ExtractLocalTransformComponents(SampleTransform, Translation, Rotation, Scale);

        OutTrack.PosKeys.push_back(Translation);
        OutTrack.RotKeys.push_back(Rotation);
        OutTrack.ScaleKeys.push_back(Scale);
    }

    CompressConstantRawAnimSequenceTrack(OutTrack);
}

static bool IsFinite(float Value)
{
    return std::isfinite(Value);
}

static bool IsFinite(const FVector& Value)
{
    return IsFinite(Value.X) && IsFinite(Value.Y) && IsFinite(Value.Z);
}

static bool IsFinite(const FQuat& Value)
{
    return IsFinite(Value.X) && IsFinite(Value.Y) && IsFinite(Value.Z) && IsFinite(Value.W);
}

static FString NormalizeBlendShapeChannelName(const FString& BlendShapeName, const FString& ChannelName)
{
    if (!BlendShapeName.empty() &&
        ChannelName.size() > BlendShapeName.size() + 1 &&
        ChannelName.compare(0, BlendShapeName.size(), BlendShapeName) == 0 &&
        ChannelName[BlendShapeName.size()] == '_')
    {
        return ChannelName.substr(BlendShapeName.size() + 1);
    }

    return ChannelName;
}

static void ExtractShapeKeyTracksRecursive(
    FbxNode* Node,
    FbxAnimLayer* AnimLayer,
    FAnimationSequence& OutSequence)
{
    if (!Node || !AnimLayer)
    {
        return;
    }

    if (FbxNodeAttribute* Attr = Node->GetNodeAttribute())
    {
        if (Attr->GetAttributeType() == FbxNodeAttribute::eMesh)
        {
            FbxGeometry* Geometry = static_cast<FbxGeometry*>(Attr);
            const int32 BlendShapeCount = Geometry->GetDeformerCount(FbxDeformer::eBlendShape);
            for (int32 BlendShapeIndex = 0; BlendShapeIndex < BlendShapeCount; ++BlendShapeIndex)
            {
                FbxBlendShape* BlendShape = static_cast<FbxBlendShape*>(
                    Geometry->GetDeformer(BlendShapeIndex, FbxDeformer::eBlendShape));
                if (!BlendShape)
                {
                    continue;
                }

                const FString BlendShapeName = FString(BlendShape->GetName());
                const int32 ChannelCount = BlendShape->GetBlendShapeChannelCount();
                for (int32 ChannelIndex = 0; ChannelIndex < ChannelCount; ++ChannelIndex)
                {
                    FbxBlendShapeChannel* Channel = BlendShape->GetBlendShapeChannel(ChannelIndex);
                    if (!Channel)
                    {
                        continue;
                    }

                    FbxAnimCurve* Curve = Geometry->GetShapeChannel(BlendShapeIndex, ChannelIndex, AnimLayer);
                    if (!Curve || Curve->KeyGetCount() <= 0)
                    {
                        continue;
                    }

                    FShapeKeyAnimationTrack Track;
                    Track.MeshNodeName = FString(Node->GetName());
                    Track.ShapeKeyName = NormalizeBlendShapeChannelName(BlendShapeName, FString(Channel->GetName()));
                    Track.ShapeKeyIndex = ChannelIndex;
                    SetupAnimationCurveDefaults(Track.WeightCurve, EAnimationCurveChannel::ShapeWeight, 0.0f);
                    ExtractAnimationFloatCurve(Curve, Track.WeightCurve, 0.01f);

                    if (HasAnyKeys(Track.WeightCurve))
                    {
                        OutSequence.ShapeKeyTracks.push_back(Track);
                    }
                }
            }
        }
    }

    for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
    {
        ExtractShapeKeyTracksRecursive(Node->GetChild(ChildIndex), AnimLayer, OutSequence);
    }
}

static bool ValidateAnimationFloatCurve(
    const FAnimationFloatCurve& Curve,
    const char* SequenceName,
    const char* TrackName,
    float SequenceStartSeconds,
    float SequenceEndSeconds)
{
    if (!IsFinite(Curve.DefaultValue))
    {
        UE_LOG_WARNING("[FbxImporter] Animation curve has non-finite default value: Sequence=%s Track=%s",
                       SequenceName,
                       TrackName);
        return false;
    }

    float PreviousTime = -FLT_MAX;
    for (const FAnimationCurveKey& Key : Curve.Keys)
    {
        if (!IsFinite(Key.TimeSeconds) ||
            !IsFinite(Key.Value))
        {
            UE_LOG_WARNING("[FbxImporter] Animation curve has non-finite key time/value: Sequence=%s Track=%s",
                           SequenceName,
                           TrackName);
            return false;
        }

        if (!IsFinite(Key.ArriveTangent) ||
            !IsFinite(Key.LeaveTangent) ||
            !IsFinite(Key.ArriveTangentWeight) ||
            !IsFinite(Key.LeaveTangentWeight) ||
            !IsFinite(Key.ArriveTangentVelocity) ||
            !IsFinite(Key.LeaveTangentVelocity))
        {
            UE_LOG_WARNING("[FbxImporter] Animation curve has non-finite optional tangent metadata: Sequence=%s Track=%s",
                           SequenceName,
                           TrackName);
        }

        if (Key.TimeSeconds < PreviousTime)
        {
            UE_LOG_WARNING("[FbxImporter] Animation curve keys are not sorted: Sequence=%s Track=%s",
                           SequenceName,
                           TrackName);
            return false;
        }

        if (Key.TimeSeconds < SequenceStartSeconds - 0.001f || Key.TimeSeconds > SequenceEndSeconds + 0.001f)
        {
            UE_LOG_WARNING("[FbxImporter] Animation curve key is outside sequence range: Sequence=%s Track=%s Time=%.6f Range=[%.6f, %.6f]",
                           SequenceName,
                           TrackName,
                           Key.TimeSeconds,
                           SequenceStartSeconds,
                           SequenceEndSeconds);
        }

        PreviousTime = Key.TimeSeconds;
    }

    return true;
}

static bool ValidateBoneAnimationTrack(const FBoneAnimationTrack& Track, const FAnimationSequence& Sequence)
{
    if (Track.BoneName.empty())
    {
        UE_LOG_WARNING("[FbxImporter] Animation bone track has an empty bone name: Sequence=%s", Sequence.Name.c_str());
        return false;
    }

    if (!IsFinite(Track.DefaultTranslation) ||
        !IsFinite(Track.DefaultRotationEuler) ||
        !IsFinite(Track.DefaultScale) ||
        !IsFinite(Track.PreRotation) ||
        !IsFinite(Track.PostRotation) ||
        !IsFinite(Track.RotationOffset) ||
        !IsFinite(Track.RotationPivot) ||
        !IsFinite(Track.ScalingOffset) ||
        !IsFinite(Track.ScalingPivot))
    {
        UE_LOG_WARNING("[FbxImporter] Animation bone track has non-finite transform metadata: Sequence=%s Bone=%s",
                       Sequence.Name.c_str(),
                       Track.BoneName.c_str());
        return false;
    }

    bool bValid = true;
    const int32 ExpectedRawKeyCount = Sequence.NumberOfKeys > 0 ? Sequence.NumberOfKeys : Sequence.NumberOfFrames;
    const auto IsValidRawKeyCount = [ExpectedRawKeyCount](size_t KeyCount)
    {
        return KeyCount == 1 || (ExpectedRawKeyCount > 0 && KeyCount == static_cast<size_t>(ExpectedRawKeyCount));
    };
    if (Track.InternalTrack.PosKeys.empty() ||
        Track.InternalTrack.RotKeys.empty() ||
        Track.InternalTrack.ScaleKeys.empty() ||
        !IsValidRawKeyCount(Track.InternalTrack.PosKeys.size()) ||
        !IsValidRawKeyCount(Track.InternalTrack.RotKeys.size()) ||
        !IsValidRawKeyCount(Track.InternalTrack.ScaleKeys.size()))
    {
        UE_LOG_WARNING("[FbxImporter] Animation raw track key count mismatch: Sequence=%s Bone=%s Expected=%d Pos=%zu Rot=%zu Scale=%zu",
                       Sequence.Name.c_str(),
                       Track.BoneName.c_str(),
                       ExpectedRawKeyCount,
                       Track.InternalTrack.PosKeys.size(),
                       Track.InternalTrack.RotKeys.size(),
                       Track.InternalTrack.ScaleKeys.size());
        bValid = false;
    }

    for (const FVector& Key : Track.InternalTrack.PosKeys)
    {
        bValid &= IsFinite(Key);
    }
    for (const FQuat& Key : Track.InternalTrack.RotKeys)
    {
        bValid &= IsFinite(Key);
    }
    for (const FVector& Key : Track.InternalTrack.ScaleKeys)
    {
        bValid &= IsFinite(Key);
    }

    return bValid;
}

static bool ValidateAnimationSequence(const FAnimationSequence& Sequence)
{
    bool bValid = true;

    if (Sequence.Name.empty())
    {
        UE_LOG_WARNING("[FbxImporter] Animation sequence has an empty name: Source=%s", Sequence.SourcePath.c_str());
        bValid = false;
    }

    if (!IsFinite(Sequence.StartTimeSeconds) ||
        !IsFinite(Sequence.EndTimeSeconds) ||
        !IsFinite(Sequence.DurationSeconds) ||
        !IsFinite(Sequence.SourceFrameRate) ||
        Sequence.EndTimeSeconds < Sequence.StartTimeSeconds ||
        Sequence.DurationSeconds < 0.0f)
    {
        UE_LOG_WARNING("[FbxImporter] Animation sequence has an invalid time range: Sequence=%s Start=%.6f End=%.6f Duration=%.6f",
                       Sequence.Name.c_str(),
                       Sequence.StartTimeSeconds,
                       Sequence.EndTimeSeconds,
                       Sequence.DurationSeconds);
        bValid = false;
    }

    std::unordered_set<FString> BoneNames;
    for (const FBoneAnimationTrack& Track : Sequence.BoneTracks)
    {
        if (!Track.BoneName.empty() && !BoneNames.insert(Track.BoneName).second)
        {
            UE_LOG_WARNING("[FbxImporter] Animation sequence has duplicate bone tracks: Sequence=%s Bone=%s",
                           Sequence.Name.c_str(),
                           Track.BoneName.c_str());
            bValid = false;
        }

        bValid &= ValidateBoneAnimationTrack(Track, Sequence);
    }

    for (const FShapeKeyAnimationTrack& Track : Sequence.ShapeKeyTracks)
    {
        if (Track.MeshNodeName.empty() || Track.ShapeKeyName.empty())
        {
            UE_LOG_WARNING("[FbxImporter] Animation shape key track has an empty binding: Sequence=%s", Sequence.Name.c_str());
            bValid = false;
        }

        bValid &= ValidateAnimationFloatCurve(Track.WeightCurve, Sequence.Name.c_str(), Track.ShapeKeyName.c_str(), Sequence.StartTimeSeconds, Sequence.EndTimeSeconds);
    }

    return bValid;
}

}

FStaticMesh* FFbxImporter::Load(const FString& Path, const FStaticMeshLoadOptions& LoadOptions)
{
	const double StartTime = FPlatformTime::Seconds();
	UE_LOG("[FbxImporter] Start loading FBX: %s", Path.c_str());

	FbxManager* Manager = FbxManager::Create();
	if (!Manager)
	{
		UE_LOG_ERROR("[FbxImporter] Failed to create FbxManager");
		return nullptr;
	}
	
	FbxIOSettings* IOSettings = FbxIOSettings::Create(Manager, IOSROOT);
	Manager->SetIOSettings(IOSettings);

	FbxScene* Scene = FbxScene::Create(Manager, "ImportScene");
	if (!Scene)
	{
		UE_LOG_ERROR("[FbxImporter] Failed to create FbxScene");
		Manager->Destroy();
		return nullptr;
	}

	if (!ImportScene(Path, Manager, Scene))
	{
		Manager->Destroy();
		return nullptr;
	}

	// Triangulate   
	FbxGeometryConverter Converter(Manager);
	Converter.Triangulate(Scene, /*pReplace=*/true);

	FStaticMesh* StaticMesh = new FStaticMesh();
	StaticMesh->PathFileName = Path;

	if (FbxNode* RootNode = Scene->GetRootNode())
	{
		for (int32 i = 0; i < RootNode->GetChildCount(); ++i)
		{
			CollectMeshes(RootNode->GetChild(i), StaticMesh);
		}
	}

	Manager->Destroy();

	if (StaticMesh->Vertices.empty() || StaticMesh->Indices.empty())
	{
		UE_LOG_ERROR("[FbxImporter] No geometry found in FBX: %s", Path.c_str());
		delete StaticMesh;
		return nullptr;
	}

    DeduplicateStaticMeshVertices(StaticMesh);

	if (LoadOptions.bNormalizeToUnitCube)
	{
		UE_LOG("[FbxImporter] NormalizeToUnitCube enabled: %s", Path.c_str());
		NormalizePositionsToUnitCube(StaticMesh);
	}

	StaticMesh->LocalBounds = BuildLocalBounds(StaticMesh);

	ComputeTangents(StaticMesh);

	UE_LOG("[FbxImporter] FBX Loaded: %s (Vertices: %zu, Indices: %zu, Sections: %zu, Slots: %zu)",
		Path.c_str(),
		StaticMesh->Vertices.size(),
		StaticMesh->Indices.size(),
		StaticMesh->Sections.size(),
		StaticMesh->Slots.size());

	const double EndTime = FPlatformTime::Seconds();
	UE_LOG("[FbxImporter] Loaded %s in %.3f sec", Path.c_str(), EndTime - StartTime);

	return StaticMesh;
}

bool FFbxImporter::SupportsExtension(const FString& Extension) const
{
	return Extension == FString("fbx") || Extension == FString(".fbx") ||
		   Extension == FString("FBX") || Extension == FString(".FBX");
}

FString FFbxImporter::GetLoaderName() const
{
	return FString{ "FFbxImporter" };
}

FSkeletalMesh* FFbxImporter::LoadSkeletalMesh(const FString& Path, const FStaticMeshLoadOptions& LoadOptions)
{
    (void)LoadOptions;

    const double StartTime = FPlatformTime::Seconds();
    UE_LOG("[FbxImporter] Start loading Skeletal FBX: %s", Path.c_str());

    FbxManager* Manager = FbxManager::Create();
    if (!Manager)
    {
        UE_LOG_ERROR("[FbxImporter] Failed to create FbxManager");
        return nullptr;
    }

    FbxIOSettings* IOSettings = FbxIOSettings::Create(Manager, IOSROOT);
    Manager->SetIOSettings(IOSettings);

    FbxScene* Scene = FbxScene::Create(Manager, "ImportSkeletalScene");
    if (!Scene)
    {
        UE_LOG_ERROR("[FbxImporter] Failed to create FbxScene");
        Manager->Destroy();
        return nullptr;
    }

    if (!ImportScene(Path, Manager, Scene))
    {
        Manager->Destroy();
        return nullptr;
    }

    FbxGeometryConverter Converter(Manager);
    Converter.Triangulate(Scene, true);

    FSkeletalMesh* SkeletalMesh = new FSkeletalMesh();
    SkeletalMesh->PathFileName = Path;

    TMap<FbxNode*, int32> BoneNodeToIndex;

    bool bHasImportedSkinnedMesh = false;

    if (FbxNode* RootNode = Scene->GetRootNode())
    {
        // 1-pass: skin deformer  mesh  
        for (int32 i = 0; i < RootNode->GetChildCount(); ++i)
        {
            CollectSkeletalMeshes(
                RootNode->GetChild(i),
                SkeletalMesh,
                ESkeletalMeshImportPass::SkinnedMeshes,
                BoneNodeToIndex,
                bHasImportedSkinnedMesh);
        }

        // 2-pass: skin deformer  mesh  bone   mesh rigid mesh 
        for (int32 i = 0; i < RootNode->GetChildCount(); ++i)
        {
            CollectSkeletalMeshes(
                RootNode->GetChild(i),
                SkeletalMesh,
                ESkeletalMeshImportPass::RigidAttachedMeshes,
                BoneNodeToIndex,
                bHasImportedSkinnedMesh);
        }
    }

    Manager->Destroy();

    if (SkeletalMesh->Vertices.empty() || SkeletalMesh->Indices.empty() || SkeletalMesh->Bones.empty())
    {
        UE_LOG_ERROR("[FbxImporter] No skeletal geometry or bones found: %s", Path.c_str());
        delete SkeletalMesh;
        return nullptr;
    }

    DeduplicateSkeletalMeshVertices(SkeletalMesh);

    SkeletalMesh->LocalBounds = BuildLocalBounds(SkeletalMesh);
    ComputeTangents(SkeletalMesh);

    const double EndTime = FPlatformTime::Seconds();
    UE_LOG("[FbxImporter] Skeletal FBX Loaded: %s (Vertices=%zu, Indices=%zu, Bones=%zu, Sections=%zu, Slots=%zu, %.3f sec)",
           Path.c_str(),
           SkeletalMesh->Vertices.size(),
           SkeletalMesh->Indices.size(),
           SkeletalMesh->Bones.size(),
           SkeletalMesh->Sections.size(),
           SkeletalMesh->MaterialSlots.size(),
           EndTime - StartTime);

    return SkeletalMesh;
}

TArray<FSkeleton*> FFbxImporter::LoadSkeletons(const FString& Path)
{
    TArray<FSkeleton*> Result;

    FbxManager* Manager = FbxManager::Create();
    if (!Manager)
    {
        UE_LOG_ERROR("[FbxImporter] Failed to create FbxManager for skeleton import");
        return Result;
    }

    FbxIOSettings* IOSettings = FbxIOSettings::Create(Manager, IOSROOT);
    Manager->SetIOSettings(IOSettings);

    FbxScene* Scene = FbxScene::Create(Manager, "ImportSkeletonScene");
    if (!Scene)
    {
        UE_LOG_ERROR("[FbxImporter] Failed to create FbxScene for skeleton import");
        Manager->Destroy();
        return Result;
    }

    if (!ImportScene(Path, Manager, Scene))
    {
        Manager->Destroy();
        return Result;
    }

    TArray<FbxNode*> RootNodes;
    if (FbxNode* RootNode = Scene->GetRootNode())
    {
        CollectTopmostSkeletonRootNodes(RootNode, RootNodes);
    }

    std::sort(
        RootNodes.begin(),
        RootNodes.end(),
        [](const FbxNode* A, const FbxNode* B)
        {
            return FString(A ? A->GetName() : "") < FString(B ? B->GetName() : "");
        });

    for (FbxNode* RootNode : RootNodes)
    {
        if (FSkeleton* Skeleton = BuildSkeletonFromRoot(RootNode, Path))
        {
            Result.push_back(Skeleton);
        }
    }

    Manager->Destroy();

    UE_LOG("[FbxImporter] FBX skeletons loaded: %s (Skeletons=%zu)",
           Path.c_str(),
           Result.size());

    return Result;
}

TArray<FAnimationSequence*> FFbxImporter::LoadAnimationSequences(const FString& Path, const FAnimationImportOptions& ImportOptions)
{
    const double StartTime = FPlatformTime::Seconds();
    UE_LOG("[FbxImporter] Start loading FBX animations: %s", Path.c_str());

    TArray<FAnimationSequence*> Result;

    FbxManager* Manager = FbxManager::Create();
    if (!Manager)
    {
        UE_LOG_ERROR("[FbxImporter] Failed to create FbxManager for animation import");
        return Result;
    }

    FbxIOSettings* IOSettings = FbxIOSettings::Create(Manager, IOSROOT);
    Manager->SetIOSettings(IOSettings);

    FbxScene* Scene = FbxScene::Create(Manager, "ImportAnimationScene");
    if (!Scene)
    {
        UE_LOG_ERROR("[FbxImporter] Failed to create FbxScene for animation import");
        Manager->Destroy();
        return Result;
    }

    if (!ImportScene(Path, Manager, Scene))
    {
        Manager->Destroy();
        return Result;
    }

    TArray<FbxNode*> BoneNodes;
    if (FbxNode* RootNode = Scene->GetRootNode())
    {
        CollectSkinnedClusterBoneNodesRecursive(RootNode, BoneNodes);
        if (BoneNodes.empty())
        {
            CollectSkeletonNodes(RootNode, BoneNodes);
        }
    }

    if (BoneNodes.empty())
    {
        UE_LOG_WARNING("[FbxImporter] No skeleton nodes found for animation import: %s", Path.c_str());
    }

    const int32 AnimStackCount = Scene->GetSrcObjectCount<FbxAnimStack>();
    for (int32 StackIndex = 0; StackIndex < AnimStackCount; ++StackIndex)
    {
        FbxAnimStack* AnimStack = Scene->GetSrcObject<FbxAnimStack>(StackIndex);
        if (!AnimStack)
        {
            continue;
        }

        FAnimationSequence* Sequence = new FAnimationSequence();
        Sequence->SourcePath = Path;
        Sequence->SkeletonSourcePath = ImportOptions.SkeletonSourcePath;

        if (ExtractAnimationStack(Scene, AnimStack, BoneNodes, ImportOptions, *Sequence))
        {
            Result.push_back(Sequence);
        }
        else
        {
            delete Sequence;
        }

        if (!ImportOptions.bImportAllStacks)
        {
            break;
        }
    }

    Manager->Destroy();

    const double EndTime = FPlatformTime::Seconds();
    UE_LOG("[FbxImporter] FBX animations loaded: %s (Sequences=%zu, %.3f sec)",
           Path.c_str(),
           Result.size(),
           EndTime - StartTime);

    return Result;
}

FFbxImportedAssetSet FFbxImporter::AnalyzeImportedAssets(const FString& Path)
{
    FFbxImportedAssetSet Result;
    Result.SourcePath = Path;

    FbxManager* Manager = FbxManager::Create();
    if (!Manager)
    {
        UE_LOG_ERROR("[FbxImporter] Failed to create FbxManager for scene analysis");
        return Result;
    }

    FbxIOSettings* IOSettings = FbxIOSettings::Create(Manager, IOSROOT);
    Manager->SetIOSettings(IOSettings);

    FbxScene* Scene = FbxScene::Create(Manager, "AnalyzeFbxScene");
    if (!Scene)
    {
        UE_LOG_ERROR("[FbxImporter] Failed to create FbxScene for scene analysis");
        Manager->Destroy();
        return Result;
    }

    if (ImportScene(Path, Manager, Scene))
    {
        if (FbxNode* RootNode = Scene->GetRootNode())
        {
            AnalyzeSceneRecursive(RootNode, Result);
        }

        FString TargetSkeletonRootNodeName;
        if (!Result.Skeletons.empty())
        {
            const auto FirstSkeleton = std::min_element(
                Result.Skeletons.begin(),
                Result.Skeletons.end(),
                [](const FFbxSkeletonImportDesc& A, const FFbxSkeletonImportDesc& B)
                {
                    return A.RootNodeName < B.RootNodeName;
                });
            TargetSkeletonRootNodeName = FirstSkeleton->RootNodeName;
        }

        const int32 AnimStackCount = Scene->GetSrcObjectCount<FbxAnimStack>();
        for (int32 StackIndex = 0; StackIndex < AnimStackCount; ++StackIndex)
        {
            FbxAnimStack* AnimStack = Scene->GetSrcObject<FbxAnimStack>(StackIndex);
            if (!AnimStack)
            {
                continue;
            }

            FFbxAnimationImportDesc Desc;
            Desc.SequenceName = AnimStack->GetName() && AnimStack->GetName()[0] != '\0'
                ? FString(AnimStack->GetName())
                : FString("AnimStack_") + std::to_string(StackIndex);
            Desc.TargetSkeletonRootNodeName = TargetSkeletonRootNodeName;
            Desc.StackIndex = StackIndex;
            Result.Animations.push_back(Desc);
        }
    }

    SortImportedAssetSet(Result);
    Manager->Destroy();
    return Result;
}

FFbxMeshContentInfo FFbxImporter::InspectMeshContent(const FString& Path)
{
    return AnalyzeImportedAssets(Path).MeshContentInfo;
}

void FFbxImporter::CollectSkeletonNodes(FbxNode* Node, TArray<FbxNode*>& OutNodes) const
{
    if (!Node)
    {
        return;
    }

    if (FbxNodeAttribute* Attr = Node->GetNodeAttribute())
    {
        if (Attr->GetAttributeType() == FbxNodeAttribute::eSkeleton)
        {
            OutNodes.push_back(Node);
        }
    }

    for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
    {
        CollectSkeletonNodes(Node->GetChild(ChildIndex), OutNodes);
    }
}

bool FFbxImporter::ExtractAnimationStack(
    FbxScene* Scene,
    FbxAnimStack* AnimStack,
    const TArray<FbxNode*>& BoneNodes,
    const FAnimationImportOptions& ImportOptions,
    FAnimationSequence& OutSequence) const
{
    if (!Scene || !AnimStack)
    {
        return false;
    }

    const int32 LayerCount = AnimStack->GetMemberCount<FbxAnimLayer>();
    if (LayerCount <= 0)
    {
        UE_LOG_WARNING("[FbxImporter] Skip animation stack without layers: %s", AnimStack->GetName());
        return false;
    }

    if (LayerCount > 1)
    {
        UE_LOG_WARNING("[FbxImporter] Animation stack has %d layers. Only base layer is imported: %s",
                       LayerCount,
                       AnimStack->GetName());
    }

    FbxAnimLayer* AnimLayer = AnimStack->GetMember<FbxAnimLayer>(0);
    if (!AnimLayer)
    {
        return false;
    }

    Scene->SetCurrentAnimationStack(AnimStack);

    OutSequence.Name = FString(AnimStack->GetName());
    const FbxTimeSpan TimeSpan = AnimStack->GetLocalTimeSpan();
    OutSequence.StartTimeSeconds = static_cast<float>(TimeSpan.GetStart().GetSecondDouble());
    OutSequence.EndTimeSeconds = static_cast<float>(TimeSpan.GetStop().GetSecondDouble());
    OutSequence.DurationSeconds = std::max(0.0f, OutSequence.EndTimeSeconds - OutSequence.StartTimeSeconds);
    OutSequence.SourceFrameRate = static_cast<float>(FbxTime::GetFrameRate(Scene->GetGlobalSettings().GetTimeMode()));
    OutSequence.NumberOfFrames = CalculateAnimationFrameCount(
        OutSequence.StartTimeSeconds,
        OutSequence.EndTimeSeconds,
        OutSequence.SourceFrameRate);
    OutSequence.NumberOfKeys = OutSequence.NumberOfFrames;

    if (ImportOptions.bImportBoneTransforms)
    {
        ExtractBoneAnimationTracks(AnimLayer, BoneNodes, OutSequence);
    }

    if (ImportOptions.bImportShapeKeys)
    {
        ExtractShapeKeyTracks(AnimLayer, Scene, OutSequence);
    }

    if (OutSequence.BoneTracks.empty() && OutSequence.ShapeKeyTracks.empty())
    {
        UE_LOG_WARNING("[FbxImporter] Skip animation stack without supported curves: %s", AnimStack->GetName());
        return false;
    }

    if (!ValidateAnimationSequence(OutSequence))
    {
        UE_LOG_WARNING("[FbxImporter] Skip invalid animation stack: %s", AnimStack->GetName());
        return false;
    }

    return true;
}

void FFbxImporter::ExtractBoneAnimationTracks(
    FbxAnimLayer* AnimLayer,
    const TArray<FbxNode*>& BoneNodes,
    FAnimationSequence& OutSequence) const
{
    if (!AnimLayer)
    {
        return;
    }

    for (FbxNode* BoneNode : BoneNodes)
    {
        if (!BoneNode)
        {
            continue;
        }

        FBoneAnimationTrack Track;
        Track.BoneName = FString(BoneNode->GetName());
        Track.DefaultTranslation = ToFVector(BoneNode->LclTranslation.Get());
        Track.DefaultRotationEuler = ToFVector(BoneNode->LclRotation.Get());
        Track.DefaultScale = ToFVector(BoneNode->LclScaling.Get());
        ExtractRawAnimSequenceTrack(
            AnimLayer,
            BoneNode,
            FindNearestIncludedParentNode(BoneNode, BoneNodes),
            OutSequence,
            Track.InternalTrack);
        Track.bRotationActive = BoneNode->GetRotationActive();

        EFbxRotationOrder RotationOrder = eEulerXYZ;
        BoneNode->GetRotationOrder(FbxNode::eSourcePivot, RotationOrder);
        Track.RotationOrder = ToAnimationRotationOrder(RotationOrder);

        FbxTransform::EInheritType InheritType = FbxTransform::eInheritRrSs;
        BoneNode->GetTransformationInheritType(InheritType);
        Track.TransformInheritType = static_cast<int32>(InheritType);

        Track.PreRotation = ToFVector(BoneNode->GetPreRotation(FbxNode::eSourcePivot));
        Track.PostRotation = ToFVector(BoneNode->GetPostRotation(FbxNode::eSourcePivot));
        Track.RotationOffset = ToFVector(BoneNode->GetRotationOffset(FbxNode::eSourcePivot));
        Track.RotationPivot = ToFVector(BoneNode->GetRotationPivot(FbxNode::eSourcePivot));
        Track.ScalingOffset = ToFVector(BoneNode->GetScalingOffset(FbxNode::eSourcePivot));
        Track.ScalingPivot = ToFVector(BoneNode->GetScalingPivot(FbxNode::eSourcePivot));

        if (!Track.InternalTrack.PosKeys.empty() &&
            !Track.InternalTrack.RotKeys.empty() &&
            !Track.InternalTrack.ScaleKeys.empty())
        {
            OutSequence.BoneTracks.push_back(Track);
        }
    }
}

void FFbxImporter::ExtractShapeKeyTracks(FbxAnimLayer* AnimLayer, FbxScene* Scene, FAnimationSequence& OutSequence) const
{
    if (!AnimLayer || !Scene)
    {
        return;
    }

    ExtractShapeKeyTracksRecursive(Scene->GetRootNode(), AnimLayer, OutSequence);
}

bool FFbxImporter::ImportScene(const FString& Path, FbxManager* Manager, FbxScene* Scene)
{
	FbxImporter* Importer = FbxImporter::Create(Manager, "");
	if (!Importer->Initialize(Path.c_str(), -1, Manager->GetIOSettings()))
	{
		UE_LOG_ERROR("[FbxImporter] Initialize failed: %s (%s)", Path.c_str(), Importer->GetStatus().GetErrorString());
		Importer->Destroy();
		return false;
	}

	const bool bResult = Importer->Import(Scene);
	if (!bResult)
	{
		UE_LOG_ERROR("[FbxImporter] Import failed: %s (%s)", Path.c_str(), Importer->GetStatus().GetErrorString());
	}

	Importer->Destroy();

	if (bResult)
	{
		// Engine import policy: left-handed, Z-up, X-forward, meter.
		// FBX SDK mesh/transform/anim      swap .
		const FbxAxisSystem TargetAxis(
			FbxAxisSystem::eZAxis,
			FbxAxisSystem::eParityOdd,
			FbxAxisSystem::eLeftHanded);
		TargetAxis.DeepConvertScene(Scene);

		FbxSystemUnit::m.ConvertScene(Scene);
	}

	return bResult;
}

void FFbxImporter::CollectMeshes(FbxNode* Node, FStaticMesh* InStaticMesh)
{
	if (!Node) return;

	if (FbxNodeAttribute* Attr = Node->GetNodeAttribute())
	{
		if (Attr->GetAttributeType() == FbxNodeAttribute::eMesh)
		{
			ProcessMesh(static_cast<FbxMesh*>(Attr), InStaticMesh);
		}
	}

	for (int32 i = 0; i < Node->GetChildCount(); ++i)
	{
		CollectMeshes(Node->GetChild(i), InStaticMesh);
	}
}

void FFbxImporter::ProcessMesh(FbxMesh* Mesh, FStaticMesh* InStaticMesh)
{
	if (!Mesh || Mesh->GetPolygonCount() <= 0)
	{
		return;
	}

	FbxNode* OwnerNode = Mesh->GetNode();

	// FbxAxisSystem/FbxSystemUnit::ConvertScene  transform  baked.
	//  control point GlobalTransform * GeometricTransform  / .
	FbxAMatrix VertexTransform;
	FbxAMatrix NormalTransform;
	if (OwnerNode)
	{
		const FbxVector4 T = OwnerNode->GetGeometricTranslation(FbxNode::eSourcePivot);
		const FbxVector4 R = OwnerNode->GetGeometricRotation(FbxNode::eSourcePivot);
		const FbxVector4 S = OwnerNode->GetGeometricScaling(FbxNode::eSourcePivot);
		FbxAMatrix GeomTransform;
		GeomTransform.SetTRS(T, R, S);

		const FbxAMatrix GlobalTransform = OwnerNode->EvaluateGlobalTransform();
		VertexTransform = GlobalTransform * GeomTransform;

		// Normal  ? translation 
		NormalTransform = VertexTransform;
		NormalTransform.SetT(FbxVector4(0, 0, 0, 0));
	}

	const FbxVector4* ControlPoints = Mesh->GetControlPoints();
	if (!ControlPoints) return;

	//     (per-polygon ,     )
	FbxLayerElementArrayTemplate<int32>* MaterialIndices = nullptr;
	FbxGeometryElement::EMappingMode MaterialMappingMode = FbxGeometryElement::eByPolygon;
	if (Mesh->GetElementMaterial())
	{
		MaterialIndices = &Mesh->GetElementMaterial()->GetIndexArray();
		MaterialMappingMode = Mesh->GetElementMaterial()->GetMappingMode();
	}

	//     (OBJ   )
	TArray<TArray<uint32>> SlotIndices;

	const int32 PolygonCount = Mesh->GetPolygonCount();
	for (int32 PolyIdx = 0; PolyIdx < PolygonCount; ++PolyIdx)
	{
		// Triangulate  PolygonSize == 3 
		const int32 PolygonSize = Mesh->GetPolygonSize(PolyIdx);
		if (PolygonSize != 3) continue;

		//   
		FString MaterialName = "DefaultWhite";
		if (MaterialIndices && OwnerNode)
		{
			int32 MatIdx = 0;
			if (MaterialMappingMode == FbxGeometryElement::eByPolygon &&
				PolyIdx < MaterialIndices->GetCount())
			{
				MatIdx = MaterialIndices->GetAt(PolyIdx);
			}
			else if (MaterialMappingMode == FbxGeometryElement::eAllSame &&
				MaterialIndices->GetCount() > 0)
			{
				MatIdx = MaterialIndices->GetAt(0);
			}

			if (MatIdx >= 0 && MatIdx < OwnerNode->GetMaterialCount())
			{
				if (FbxSurfaceMaterial* SurfMat = OwnerNode->GetMaterial(MatIdx))
				{
					MaterialName = FString(SurfMat->GetName());
				}
			}
		}

		const int32 SlotIdx = GetOrAddMaterialSlot(InStaticMesh, MaterialName);
		if (SlotIdx >= static_cast<int32>(SlotIndices.size()))
		{
			SlotIndices.resize(SlotIdx + 1);
		}

		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const int32 CtrlPointIdx = Mesh->GetPolygonVertex(PolyIdx, Corner);

			FNormalVertex Vertex = {};

			// Position
			FbxVector4 Pos = ControlPoints[CtrlPointIdx];
			Pos = VertexTransform.MultT(Pos);
			Vertex.Position = ToFVector(Pos);

			// Normal
			FbxVector4 Normal(0, 0, 1, 0);
			if (Mesh->GetPolygonVertexNormal(PolyIdx, Corner, Normal))
			{
				Normal[3] = 0.0;  // direction vector ? translation 
				Normal = NormalTransform.MultT(Normal);
				Vertex.Normal = ToFVector(Normal);
				const float Len = Vertex.Normal.Size();
				if (Len > 1e-6f) Vertex.Normal = Vertex.Normal / Len;
			}
			else
			{
				Vertex.Normal = FVector(0.0f, 0.0f, 1.0f);
			}

			// UV (   )
			Vertex.UVs = FVector2(0.0f, 0.0f);
			if (Mesh->GetElementUVCount() > 0)
			{
				FbxStringList UVNames;
				Mesh->GetUVSetNames(UVNames);
				if (const char* UVName = UVNames.GetStringAt(0))
				{
					FbxVector2 UV;
					bool bUnmapped = false;
					if (Mesh->GetPolygonVertexUV(PolyIdx, Corner, UVName, UV, bUnmapped))
					{
						Vertex.UVs = ToFVector2(UV);
					}
				}
			}

			Vertex.Color = FColor{ 1.0f, 1.0f, 1.0f, 1.0f };

			const uint32 NewIndex = static_cast<uint32>(InStaticMesh->Vertices.size());
			InStaticMesh->Vertices.push_back(Vertex);
			SlotIndices[SlotIdx].push_back(NewIndex);
		}
	}

	//   Mesh.Indices  Section 
	for (int32 SlotIdx = 0; SlotIdx < static_cast<int32>(SlotIndices.size()); ++SlotIdx)
	{
		TArray<uint32>& IndicesPerSlot = SlotIndices[SlotIdx];
		if (IndicesPerSlot.empty()) continue;

		FStaticMeshSection NewSection;
		NewSection.StartIndex = static_cast<uint32>(InStaticMesh->Indices.size());
		NewSection.IndexCount = static_cast<uint32>(IndicesPerSlot.size());
		NewSection.MaterialSlotIndex = SlotIdx;

		InStaticMesh->Indices.insert(
			InStaticMesh->Indices.end(),
			IndicesPerSlot.begin(),
			IndicesPerSlot.end());

		InStaticMesh->Sections.push_back(NewSection);
	}
}

int32 FFbxImporter::GetOrAddMaterialSlot(FStaticMesh* InStaticMesh, const FString& MaterialName)
{
	const FString SlotName = MaterialName.empty() ? FString("DefaultWhite") : MaterialName;

	for (int32 i = 0; i < static_cast<int32>(InStaticMesh->Slots.size()); ++i)
	{
		if (InStaticMesh->Slots[i].SlotName == SlotName)
		{
			return i;
		}
	}

	FStaticMeshMaterialSlot NewSlot;
	NewSlot.SlotName = SlotName;
	NewSlot.Material = nullptr;
	InStaticMesh->Slots.push_back(NewSlot);
	return static_cast<int32>(InStaticMesh->Slots.size() - 1);
}

FAABB FFbxImporter::BuildLocalBounds(FStaticMesh* InStaticMesh) const
{
	FAABB Bounds;
	Bounds.Reset();

	for (const FNormalVertex& Vertex : InStaticMesh->Vertices)
	{
		Bounds.Expand(Vertex.Position);
	}

	return Bounds;
}

void FFbxImporter::NormalizePositionsToUnitCube(FStaticMesh* InStaticMesh)
{
	if (InStaticMesh->Vertices.empty()) return;

	FVector Min(FLT_MAX, FLT_MAX, FLT_MAX);
	FVector Max(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (const FNormalVertex& V : InStaticMesh->Vertices)
	{
		Min.X = std::min(Min.X, V.Position.X);
		Min.Y = std::min(Min.Y, V.Position.Y);
		Min.Z = std::min(Min.Z, V.Position.Z);
		Max.X = std::max(Max.X, V.Position.X);
		Max.Y = std::max(Max.Y, V.Position.Y);
		Max.Z = std::max(Max.Z, V.Position.Z);
	}

	const FVector Center = (Min + Max) * 0.5f;
	const FVector Size = Max - Min;
	const float MaxDim = std::max(Size.X, std::max(Size.Y, Size.Z));
	if (MaxDim <= 1e-6f) return;

	const float Scale = 1.0f / MaxDim;
	for (FNormalVertex& V : InStaticMesh->Vertices)
	{
		V.Position = (V.Position - Center) * Scale;
	}
}

void FFbxImporter::ComputeTangents(FStaticMesh* InStaticMesh)
{
	const uint64 VertexCount = InStaticMesh->Vertices.size();
	TArray<FVector> TangentAcc(VertexCount, FVector(0, 0, 0));
	TArray<FVector> BitangentAcc(VertexCount, FVector(0, 0, 0));

	const TArray<uint32>& Idx = InStaticMesh->Indices;
	for (uint64 i = 0; i + 2 < Idx.size(); i += 3)
	{
		const uint32 I0 = Idx[i], I1 = Idx[i + 1], I2 = Idx[i + 2];
		const FNormalVertex& V0 = InStaticMesh->Vertices[I0];
		const FNormalVertex& V1 = InStaticMesh->Vertices[I1];
		const FNormalVertex& V2 = InStaticMesh->Vertices[I2];

		FVector T, B;
		GetTangentBitangent(T, B, V0.Position, V1.Position, V2.Position,
			V0.UVs, V1.UVs, V2.UVs);
		TangentAcc[I0] += T; TangentAcc[I1] += T; TangentAcc[I2] += T;
		BitangentAcc[I0] += B; BitangentAcc[I1] += B; BitangentAcc[I2] += B;
	}

	for (uint64 i = 0; i < VertexCount; ++i)
	{
		const FVector& N = InStaticMesh->Vertices[i].Normal;
		FVector T = TangentAcc[i];

		// Gram-Schmidt
		T = (T - N * FVector::DotProduct(N, T));
		const float Len = T.Size();
		T = (Len > 1e-6f) ? T / Len : FVector(1, 0, 0);

		const FVector ExpectedB = FVector::CrossProduct(N, T);
		const float Sign = (FVector::DotProduct(ExpectedB, BitangentAcc[i]) < 0.0f) ? -1.0f : 1.0f;

		InStaticMesh->Vertices[i].Tangent = FVector4(T.X, T.Y, T.Z, Sign);
	}
}

void FFbxImporter::CollectSkeletalMeshes(
    FbxNode* Node,
    FSkeletalMesh* InSkeletalMesh,
    ESkeletalMeshImportPass Pass,
    TMap<FbxNode*, int32>& BoneNodeToIndex,
    bool& bHasImportedSkinnedMesh)
{
    if (!Node)
    {
        return;
    }

    if (FbxMesh* Mesh = Node->GetMesh())
    {
        ProcessSkeletalMesh(
            Mesh,
            InSkeletalMesh,
            Pass,
            BoneNodeToIndex,
            bHasImportedSkinnedMesh);
    }

    for (int32 i = 0; i < Node->GetChildCount(); ++i)
    {
        CollectSkeletalMeshes(
            Node->GetChild(i),
            InSkeletalMesh,
            Pass,
            BoneNodeToIndex,
            bHasImportedSkinnedMesh);
    }
}

void FFbxImporter::ProcessSkeletalMesh(
    FbxMesh* Mesh,
    FSkeletalMesh* InSkeletalMesh,
    ESkeletalMeshImportPass Pass,
    TMap<FbxNode*, int32>& BoneNodeToIndex,
    bool& bHasImportedSkinnedMesh)
{
    if (!Mesh || !InSkeletalMesh || Mesh->GetPolygonCount() <= 0)
    {
        return;
    }

    const int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);

    if (Pass == ESkeletalMeshImportPass::RigidAttachedMeshes)
    {
        if (SkinCount > 0)
        {
            return;
        }

        ProcessRigidAttachedMesh(
            Mesh,
            InSkeletalMesh,
            BoneNodeToIndex,
            bHasImportedSkinnedMesh);
        return;
    }

    if (Pass != ESkeletalMeshImportPass::SkinnedMeshes)
    {
        return;
    }

    if (SkinCount <= 0)
    {
        return;
    }

    FbxNode* OwnerNode = Mesh->GetNode();
    const size_t VertexCountBefore = InSkeletalMesh->Vertices.size();
    const size_t IndexCountBefore = InSkeletalMesh->Indices.size();
    const size_t SectionCountBefore = InSkeletalMesh->Sections.size();
    const size_t SlotCountBefore = InSkeletalMesh->MaterialSlots.size();

    const FbxVector4* ControlPoints = Mesh->GetControlPoints();
    if (!ControlPoints)
    {
        return;
    }

    const int32 ControlPointCount = Mesh->GetControlPointsCount();

    const FbxAMatrix MeshGeometry = GetGeometryTransform(OwnerNode);

    FbxAMatrix MeshBindGlobalWithGeometry;
    MeshBindGlobalWithGeometry.SetIdentity();
    bool bHasMeshBindGlobalWithGeometry = false;

    TArray<TArray<FTempInfluence>> InfluencesByControlPoint;
    InfluencesByControlPoint.resize(ControlPointCount);

    // cluster link node bone 
    for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
    {
        FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
        if (!Skin)
        {
            continue;
        }

        const int32 ClusterCount = Skin->GetClusterCount();
        for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
        {
            FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
            if (!Cluster || !Cluster->GetLink())
            {
                continue;
            }

            FbxNode* BoneNode = Cluster->GetLink();

            FbxAMatrix MeshBindGlobal;
            FbxAMatrix LinkBindGlobal;

            Cluster->GetTransformMatrix(MeshBindGlobal);
            Cluster->GetTransformLinkMatrix(LinkBindGlobal);

            const FbxAMatrix ClusterMeshBindGlobalWithGeometry = MeshBindGlobal * MeshGeometry;
            if (!bHasMeshBindGlobalWithGeometry)
            {
                MeshBindGlobalWithGeometry = ClusterMeshBindGlobalWithGeometry;
                bHasMeshBindGlobalWithGeometry = true;
            }

            if (!bHasImportedSkinnedMesh)
            {
                bHasImportedSkinnedMesh = true;
            }

            if (BoneNodeToIndex.find(BoneNode) != BoneNodeToIndex.end())
            {
                continue;
            }

            const int32 NewBoneIndex = static_cast<int32>(InSkeletalMesh->Bones.size());
            BoneNodeToIndex[BoneNode] = NewBoneIndex;

            FBoneInfo Bone = {};
            Bone.Name = FString(BoneNode->GetName());
            Bone.ParentIndex = -1;

            Bone.GlobalBindTransform = ToFMatrix(LinkBindGlobal);
            Bone.InverseBindPose = Bone.GlobalBindTransform.GetInverse();
            Bone.LocalBindTransform = Bone.GlobalBindTransform;

            InSkeletalMesh->Bones.push_back(Bone);
        }
    }

    if (!bHasMeshBindGlobalWithGeometry)
    {
        return;
    }

    const FbxAMatrix NormalBindGlobalWithGeometry =
        GetNormalTransformFromPositionTransform(MeshBindGlobalWithGeometry);
    const bool bFlipWinding = HasMirroredHandedness(MeshBindGlobalWithGeometry);
    if (bFlipWinding)
    {
        UE_LOG(
            "[FbxImporter] Mirrored skinned mesh transform detected. Flipping winding | Node=%s",
            OwnerNode ? OwnerNode->GetName() : "<unknown>");
    }

    // parentIndex LocalBindTransform 
    for (auto& Pair : BoneNodeToIndex)
    {
        FbxNode* BoneNode = Pair.first;
        const int32 BoneIndex = Pair.second;

        int32 ParentIndex = -1;
        FbxNode* ParentNode = BoneNode ? BoneNode->GetParent() : nullptr;

        while (ParentNode)
        {
            auto ParentIt = BoneNodeToIndex.find(ParentNode);
            if (ParentIt != BoneNodeToIndex.end())
            {
                ParentIndex = ParentIt->second;
                break;
            }

            ParentNode = ParentNode->GetParent();
        }

        FBoneInfo& Bone = InSkeletalMesh->Bones[BoneIndex];
        Bone.ParentIndex = ParentIndex;

        if (ParentIndex >= 0)
        {
            const FMatrix ParentGlobalInv =
                InSkeletalMesh->Bones[ParentIndex].GlobalBindTransform.GetInverse();

            Bone.LocalBindTransform = Bone.GlobalBindTransform * ParentGlobalInv;
        }
        else
        {
            Bone.LocalBindTransform = Bone.GlobalBindTransform;
        }
    }

    // control point influence 
    int32 SkippedInfluenceCountForUint8Limit = 0;
    for (int32 SkinIndex = 0; SkinIndex < SkinCount; SkinIndex++)
    {
        FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
        if (!Skin)
        {
            continue;
        }

        const int32 ClusterCount = Skin->GetClusterCount();
        for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ClusterIndex++)
        {
            FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
            if (!Cluster || !Cluster->GetLink())
            {
                continue;
            }

            auto BoneIt = BoneNodeToIndex.find(Cluster->GetLink());
            if (BoneIt == BoneNodeToIndex.end())
            {
                continue;
            }

            const int32 BoneIndex = BoneIt->second;
            if (BoneIndex > 255)
            {
                SkippedInfluenceCountForUint8Limit += Cluster->GetControlPointIndicesCount();
                continue;
            }

            const int32 IndexCount = Cluster->GetControlPointIndicesCount();
            int* ControlPointIndices = Cluster->GetControlPointIndices();
            double* ControlPointWeights = Cluster->GetControlPointWeights();

            if (!ControlPointIndices || !ControlPointWeights)
            {
                continue;
            }

            for (int32 i = 0; i < IndexCount; i++)
            {
                const int32 CtrlPointIndex = ControlPointIndices[i];
                const float Weight = static_cast<float>(ControlPointWeights[i]);

                if (CtrlPointIndex < 0 || CtrlPointIndex >= ControlPointCount || Weight <= 0.0f)
                {
                    continue;
                }

                InfluencesByControlPoint[CtrlPointIndex].push_back({ BoneIndex, Weight });
            }
        }
    }

    // material mapping  
    FbxLayerElementArrayTemplate<int32>* MaterialIndices = nullptr;
    FbxGeometryElement::EMappingMode MaterialMappingMode = FbxGeometryElement::eByPolygon;

    if (Mesh->GetElementMaterial())
    {
        MaterialIndices = &Mesh->GetElementMaterial()->GetIndexArray();
        MaterialMappingMode = Mesh->GetElementMaterial()->GetMappingMode();
    }

    TArray<TArray<uint32>> SlotIndices;

    // polygon corner FSkeletalMeshVertex 
    const int32 PolygonCount = Mesh->GetPolygonCount();
    for (int32 PolyIdx = 0; PolyIdx < PolygonCount; PolyIdx++)
    {
        const int32 PolygonSize = Mesh->GetPolygonSize(PolyIdx);
        if (PolygonSize != 3)
        {
            continue;
        }

        FString MaterialName = "DefaultWhite";

        if (MaterialIndices && OwnerNode)
        {
            int32 MatIdx = 0;

            if (MaterialMappingMode == FbxGeometryElement::eByPolygon &&
                PolyIdx < MaterialIndices->GetCount())
            {
                MatIdx = MaterialIndices->GetAt(PolyIdx);
            }
            else if (MaterialMappingMode == FbxGeometryElement::eAllSame &&
                     MaterialIndices->GetCount() > 0)
            {
                MatIdx = MaterialIndices->GetAt(0);
            }

            if (MatIdx >= 0 && MatIdx < OwnerNode->GetMaterialCount())
            {
                if (FbxSurfaceMaterial* SurfMat = OwnerNode->GetMaterial(MatIdx))
                {
                    MaterialName = FString(SurfMat->GetName());
                }
            }
        }

        const int32 SlotIdx = GetOrAddMaterialSlot(InSkeletalMesh, MaterialName);
        if (SlotIdx >= static_cast<int32>(SlotIndices.size()))
        {
            SlotIndices.resize(SlotIdx + 1);
        }

        uint32 TriangleIndices[3] = {};
        int32 ValidCornerCount = 0;
        for (int32 Corner = 0; Corner < 3; Corner++)
        {
            const int32 CtrlPointIdx = Mesh->GetPolygonVertex(PolyIdx, Corner);
            if (CtrlPointIdx < 0 || CtrlPointIdx >= ControlPointCount)
            {
                continue;
            }

            FSkeletalMeshVertex Vertex = {};
            ResetVertexInfluences(Vertex);

            FbxVector4 Pos = ControlPoints[CtrlPointIdx];
            Pos = MeshBindGlobalWithGeometry.MultT(Pos);
            Vertex.Position = ToFVector(Pos);

            FbxVector4 Normal(0, 0, 1, 0);
            if (Mesh->GetPolygonVertexNormal(PolyIdx, Corner, Normal))
            {
                Normal[3] = 0.0;
                Normal = NormalBindGlobalWithGeometry.MultT(Normal);

                Vertex.Normal = ToFVector(Normal);
                Vertex.Normal.NormalizeSafe();
            }
            else
            {
                Vertex.Normal = FVector(0.0f, 0.0f, 1.0f);
            }

            Vertex.UVs = FVector2(0.0f, 0.0f);
            if (Mesh->GetElementUVCount() > 0)
            {
                FbxStringList UVNames;
                Mesh->GetUVSetNames(UVNames);

                if (const char* UVName = UVNames.GetStringAt(0))
                {
                    FbxVector2 UV;
                    bool bUnmapped = false;

                    if (Mesh->GetPolygonVertexUV(PolyIdx, Corner, UVName, UV, bUnmapped))
                    {
                        Vertex.UVs = ToFVector2(UV);
                    }
                }
            }

            Vertex.Color = FColor{ 1.0f, 1.0f, 1.0f, 1.0f };

            AssignTop4Influences(InfluencesByControlPoint[CtrlPointIdx], Vertex);

            const uint32 NewIndex = static_cast<uint32>(InSkeletalMesh->Vertices.size());
            InSkeletalMesh->Vertices.push_back(Vertex);
            TriangleIndices[ValidCornerCount] = NewIndex;
            ++ValidCornerCount;
        }

        if (ValidCornerCount == 3)
        {
            AppendTriangleIndices(
                SlotIndices[SlotIdx],
                TriangleIndices[0],
                TriangleIndices[1],
                TriangleIndices[2],
                bFlipWinding);
        }
    }

    for (int32 SlotIdx = 0; SlotIdx < static_cast<int32>(SlotIndices.size()); SlotIdx++)
    {
        TArray<uint32>& IndicesPerSlot = SlotIndices[SlotIdx];
        if (IndicesPerSlot.empty())
        {
            continue;
        }

        FStaticMeshSection NewSection;
        NewSection.StartIndex = static_cast<uint32>(InSkeletalMesh->Indices.size());
        NewSection.IndexCount = static_cast<uint32>(IndicesPerSlot.size());
        NewSection.MaterialSlotIndex = SlotIdx;

        InSkeletalMesh->Indices.insert(
            InSkeletalMesh->Indices.end(),
            IndicesPerSlot.begin(),
            IndicesPerSlot.end());

        InSkeletalMesh->Sections.push_back(NewSection);
    }

    if (SkippedInfluenceCountForUint8Limit > 0)
    {
        UE_LOG_WARNING(
            "[FbxImporter] Skipped skeletal influences because bone index exceeds uint8 limit | Node=%s | Count=%d",
            OwnerNode ? OwnerNode->GetName() : "<unknown>",
            SkippedInfluenceCountForUint8Limit);
    }

    UE_LOG(
        "[FbxImporter] Merged skinned mesh node: %s (Vertices: %zu -> %zu, Indices: %zu -> %zu, Sections: %zu -> %zu, Slots: %zu -> %zu)",
        OwnerNode ? OwnerNode->GetName() : "<unknown>",
        VertexCountBefore,
        InSkeletalMesh->Vertices.size(),
        IndexCountBefore,
        InSkeletalMesh->Indices.size(),
        SectionCountBefore,
        InSkeletalMesh->Sections.size(),
        SlotCountBefore,
        InSkeletalMesh->MaterialSlots.size());
}

void FFbxImporter::ProcessRigidAttachedMesh(
    FbxMesh* Mesh,
    FSkeletalMesh* InSkeletalMesh,
    TMap<FbxNode*, int32>& BoneNodeToIndex,
    bool bHasImportedSkinnedMesh)
{
    if (!Mesh || !InSkeletalMesh || Mesh->GetPolygonCount() <= 0)
    {
        return;
    }

    FbxNode* OwnerNode = Mesh->GetNode();
    if (!OwnerNode)
    {
        return;
    }

    if (!bHasImportedSkinnedMesh)
    {
        UE_LOG_WARNING("[FbxImporter] Skip rigid mesh before skinned mesh import | Node=%s", OwnerNode->GetName());
        return;
    }

    if (ShouldSkipRigidMeshByName(OwnerNode))
    {
        return;
    }

    const int32 AttachBoneIndex = FindNearestImportedBoneIndex(OwnerNode, BoneNodeToIndex);
    if (AttachBoneIndex < 0)
    {
        return;
    }

    if (AttachBoneIndex > 255)
    {
        UE_LOG_WARNING(
            "[FbxImporter] Skip rigid mesh because attach bone index exceeds uint8 limit | Node=%s | BoneIndex=%d",
            OwnerNode->GetName(),
            AttachBoneIndex);
        return;
    }

    const size_t VertexCountBefore = InSkeletalMesh->Vertices.size();
    const size_t IndexCountBefore = InSkeletalMesh->Indices.size();
    const size_t SectionCountBefore = InSkeletalMesh->Sections.size();
    const size_t SlotCountBefore = InSkeletalMesh->MaterialSlots.size();

    const FbxVector4* ControlPoints = Mesh->GetControlPoints();
    if (!ControlPoints)
    {
        return;
    }

    const int32 ControlPointCount = Mesh->GetControlPointsCount();

    const FbxAMatrix OwnerGlobalWithGeometry = GetGlobalTransformWithGeometry(OwnerNode);
    const FbxAMatrix OwnerNormalGlobalWithGeometry =
        GetNormalTransformFromPositionTransform(OwnerGlobalWithGeometry);
    const bool bFlipWinding = HasMirroredHandedness(OwnerGlobalWithGeometry);
    if (bFlipWinding)
    {
        UE_LOG(
            "[FbxImporter] Mirrored rigid attached mesh transform detected. Flipping winding | Node=%s",
            OwnerNode->GetName());
    }

    FbxLayerElementArrayTemplate<int32>* MaterialIndices = nullptr;
    FbxGeometryElement::EMappingMode MaterialMappingMode = FbxGeometryElement::eByPolygon;

    if (Mesh->GetElementMaterial())
    {
        MaterialIndices = &Mesh->GetElementMaterial()->GetIndexArray();
        MaterialMappingMode = Mesh->GetElementMaterial()->GetMappingMode();
    }

    TArray<TArray<uint32>> SlotIndices;

    const int32 PolygonCount = Mesh->GetPolygonCount();

    for (int32 PolyIdx = 0; PolyIdx < PolygonCount; PolyIdx++)
    {
        const int32 PolygonSize = Mesh->GetPolygonSize(PolyIdx);
        if (PolygonSize != 3)
        {
            continue;
        }

        FString MaterialName = "DefaultWhite";

        if (MaterialIndices && OwnerNode)
        {
            int32 MatIdx = 0;

            if (MaterialMappingMode == FbxGeometryElement::eByPolygon &&
                PolyIdx < MaterialIndices->GetCount())
            {
                MatIdx = MaterialIndices->GetAt(PolyIdx);
            }
            else if (MaterialMappingMode == FbxGeometryElement::eAllSame &&
                     MaterialIndices->GetCount() > 0)
            {
                MatIdx = MaterialIndices->GetAt(0);
            }

            if (MatIdx >= 0 && MatIdx < OwnerNode->GetMaterialCount())
            {
                if (FbxSurfaceMaterial* SurfMat = OwnerNode->GetMaterial(MatIdx))
                {
                    MaterialName = FString(SurfMat->GetName());
                }
            }
        }

        const int32 SlotIdx = GetOrAddMaterialSlot(InSkeletalMesh, MaterialName);
        if (SlotIdx >= static_cast<int32>(SlotIndices.size()))
        {
            SlotIndices.resize(SlotIdx + 1);
        }

        uint32 TriangleIndices[3] = {};
        int32 ValidCornerCount = 0;
        for (int32 Corner = 0; Corner < 3; Corner++)
        {
            const int32 CtrlPointIdx = Mesh->GetPolygonVertex(PolyIdx, Corner);
            if (CtrlPointIdx < 0 || CtrlPointIdx >= ControlPointCount)
            {
                continue;
            }

            FSkeletalMeshVertex Vertex = {};
            ResetVertexInfluences(Vertex);

            FbxVector4 Pos = ControlPoints[CtrlPointIdx];
            Pos = OwnerGlobalWithGeometry.MultT(Pos);
            Vertex.Position = ToFVector(Pos);

            FbxVector4 Normal(0, 0, 1, 0);
            if (Mesh->GetPolygonVertexNormal(PolyIdx, Corner, Normal))
            {
                Normal[3] = 0.0;
                Normal = OwnerNormalGlobalWithGeometry.MultT(Normal);

                Vertex.Normal = ToFVector(Normal);
                Vertex.Normal.NormalizeSafe();
            }
            else
            {
                Vertex.Normal = FVector(0.0f, 0.0f, 1.0f);
            }

            Vertex.UVs = FVector2(0.0f, 0.0f);
            if (Mesh->GetElementUVCount() > 0)
            {
                FbxStringList UVNames;
                Mesh->GetUVSetNames(UVNames);

                if (const char* UVName = UVNames.GetStringAt(0))
                {
                    FbxVector2 UV;
                    bool bUnmapped = false;

                    if (Mesh->GetPolygonVertexUV(PolyIdx, Corner, UVName, UV, bUnmapped))
                    {
                        Vertex.UVs = ToFVector2(UV);
                    }
                }
            }

            Vertex.Color = FColor{ 1.0f, 1.0f, 1.0f, 1.0f };

            // skin  rigid mesh parent bone  100% 
            AssignRigidInfluence(Vertex, AttachBoneIndex);

            const uint32 NewIndex = static_cast<uint32>(InSkeletalMesh->Vertices.size());
            InSkeletalMesh->Vertices.push_back(Vertex);
            TriangleIndices[ValidCornerCount] = NewIndex;
            ++ValidCornerCount;
        }

        if (ValidCornerCount == 3)
        {
            AppendTriangleIndices(
                SlotIndices[SlotIdx],
                TriangleIndices[0],
                TriangleIndices[1],
                TriangleIndices[2],
                bFlipWinding);
        }
    }

    for (int32 SlotIdx = 0; SlotIdx < static_cast<int32>(SlotIndices.size()); SlotIdx++)
    {
        TArray<uint32>& IndicesPerSlot = SlotIndices[SlotIdx];
        if (IndicesPerSlot.empty())
        {
            continue;
        }

        FStaticMeshSection NewSection;
        NewSection.StartIndex = static_cast<uint32>(InSkeletalMesh->Indices.size());
        NewSection.IndexCount = static_cast<uint32>(IndicesPerSlot.size());
        NewSection.MaterialSlotIndex = SlotIdx;

        InSkeletalMesh->Indices.insert(
            InSkeletalMesh->Indices.end(),
            IndicesPerSlot.begin(),
            IndicesPerSlot.end());

        InSkeletalMesh->Sections.push_back(NewSection);
    }

    UE_LOG(
        "[FbxImporter] Merged rigid attached mesh node: %s (AttachBone=%d, Vertices: %zu -> %zu, Indices: %zu -> %zu, Sections: %zu -> %zu, Slots: %zu -> %zu)",
        OwnerNode->GetName(),
        AttachBoneIndex,
        VertexCountBefore,
        InSkeletalMesh->Vertices.size(),
        IndexCountBefore,
        InSkeletalMesh->Indices.size(),
        SectionCountBefore,
        InSkeletalMesh->Sections.size(),
        SlotCountBefore,
        InSkeletalMesh->MaterialSlots.size());
}

int32 FFbxImporter::GetOrAddMaterialSlot(FSkeletalMesh* InSkeletalMesh, const FString& MaterialName)
{
    const FString SlotName = MaterialName.empty() ? FString("DefaultWhite") : MaterialName;

    for (int32 i = 0; i < static_cast<int32>(InSkeletalMesh->MaterialSlots.size()); i++)
    {
        if (InSkeletalMesh->MaterialSlots[i].SlotName == SlotName)
        {
            return i;
        }
    }

    FStaticMeshMaterialSlot NewSlot;
    NewSlot.SlotName = SlotName;
    NewSlot.Material = nullptr;
    InSkeletalMesh->MaterialSlots.push_back(NewSlot);
    return static_cast<int32>(InSkeletalMesh->MaterialSlots.size() - 1);
}

FAABB FFbxImporter::BuildLocalBounds(FSkeletalMesh* InSkeletalMesh) const
{
    FAABB Bounds;
    Bounds.Reset();

    if (!InSkeletalMesh)
    {
        return Bounds;
    }

    for (const FSkeletalMeshVertex& Vertex : InSkeletalMesh->Vertices)
    {
        Bounds.Expand(Vertex.Position);
    }

    return Bounds;
}

void FFbxImporter::ComputeTangents(FSkeletalMesh* InSkeletalMesh)
{
    if (!InSkeletalMesh)
    {
        return;
    }

    const uint64 VertexCount = InSkeletalMesh->Vertices.size();
    if (VertexCount == 0)
    {
        return;
    }

    TArray<FVector> TangentAcc(VertexCount, FVector(0.0f, 0.0f, 0.0f));
    TArray<FVector> BitangentAcc(VertexCount, FVector(0.0f, 0.0f, 0.0f));

    const TArray<uint32>& Idx = InSkeletalMesh->Indices;

    for (uint64 i = 0; i + 2 < Idx.size(); i += 3)
    {
        const uint32 I0 = Idx[i];
        const uint32 I1 = Idx[i + 1];
        const uint32 I2 = Idx[i + 2];

        if (I0 >= VertexCount || I1 >= VertexCount || I2 >= VertexCount)
        {
            continue;
        }

        const FSkeletalMeshVertex& V0 = InSkeletalMesh->Vertices[I0];
        const FSkeletalMeshVertex& V1 = InSkeletalMesh->Vertices[I1];
        const FSkeletalMeshVertex& V2 = InSkeletalMesh->Vertices[I2];

        FVector T;
        FVector B;

        GetTangentBitangent(
            T,
            B,
            V0.Position,
            V1.Position,
            V2.Position,
            V0.UVs,
            V1.UVs,
            V2.UVs);

        TangentAcc[I0] += T;
        TangentAcc[I1] += T;
        TangentAcc[I2] += T;

        BitangentAcc[I0] += B;
        BitangentAcc[I1] += B;
        BitangentAcc[I2] += B;
    }

    for (uint64 i = 0; i < VertexCount; i++)
    {
        const FVector& N = InSkeletalMesh->Vertices[i].Normal;
        FVector T = TangentAcc[i];

        T = T - N * FVector::DotProduct(N, T);

        const float Len = T.Size();
        T = (Len > 1e-6f) ? T / Len : FVector(1.0f, 0.0f, 0.0f);

        const FVector ExpectedB = FVector::CrossProduct(N, T);
        const float Sign =
            (FVector::DotProduct(ExpectedB, BitangentAcc[i]) < 0.0f)
                ? -1.0f
                : 1.0f;

        InSkeletalMesh->Vertices[i].Tangent = FVector4(T.X, T.Y, T.Z, Sign);
    }
}
