#include "Editor/Import/EditorFbxImporter.h"
#include "Platform/Paths.h"
#include "Core/Log.h"
#include "Mesh/MeshImportOptions.h"
#include "Math/MathUtils.h"
#include "Math/Transform.h"
#include "Materials/MaterialManager.h"
#include "SimpleJSON/json.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <utility>

TArray<FBone> FEditorFbxImporter::Bones;
TArray<FVertexPNCTBW> FEditorFbxImporter::Vertices;
TArray<uint32> FEditorFbxImporter::Indices;
TArray<FSkeletalMeshSection> FEditorFbxImporter::Sections;
TArray<FEditorFbxImporter::FImportedSkeletalMesh> FEditorFbxImporter::ImportedSkeletalMeshes;
TArray<FEditorFbxImporter::FMaterialInfo> FEditorFbxImporter::MtlInfos;
TArray<FVector> FEditorFbxImporter::TangentSums;
TArray<FVector> FEditorFbxImporter::BitangentSums;
TMap<FbxSurfaceMaterial*, int32> FEditorFbxImporter::MaterialToSlotIndex;
FString FEditorFbxImporter::CurrentSourcePath;

namespace
{
	FString NormalizeProjectPath(const FString& Path)
	{
		return FPaths::MakeProjectRelative(Path);
	}

	std::filesystem::path ResolveProjectPath(const FString& Path)
	{
		std::filesystem::path FullPath(FPaths::ToWide(Path));
		if (!FullPath.is_absolute())
		{
			FullPath = std::filesystem::path(FPaths::RootDir()) / FullPath;
		}
		return FullPath.lexically_normal();
	}

	FString SanitizeFileStem(const FString& Name)
	{
		FString Result = Name.empty() ? "None" : Name;
		for (char& Ch : Result)
		{
			const unsigned char U = static_cast<unsigned char>(Ch);
			if (U < 32 || Ch == '<' || Ch == '>' || Ch == ':' || Ch == '"' ||
				Ch == '/' || Ch == '\\' || Ch == '|' || Ch == '?' || Ch == '*')
			{
				Ch = '_';
			}
		}
		return Result.empty() ? FString("None") : Result;
	}

	FString BuildAdjacentMaterialPath(const FString& FbxFilePath, const FString& MaterialSlotName)
	{
		const std::filesystem::path FbxPath = ResolveProjectPath(FbxFilePath);
		const FString MeshStem = FPaths::ToUtf8(FbxPath.stem().wstring());
		const FString SlotStem = SanitizeFileStem(MaterialSlotName);
		const std::filesystem::path MatPath = FbxPath.parent_path() / FPaths::ToWide(MeshStem + "_" + SlotStem + ".mat");
		return NormalizeProjectPath(FPaths::ToUtf8(MatPath.generic_wstring()));
	}

	FString MakeUniqueMaterialSlotName(const FString& InName, TMap<FString, int32>& NameCounts)
	{
		const FString BaseName = InName.empty() ? FString("None") : InName;
		int32& Count = NameCounts[BaseName];
		++Count;
		if (Count == 1)
		{
			return BaseName;
		}

		return BaseName + " Slot #" + std::to_string(Count);
	}

	std::wstring ToLowerWide(std::wstring Value)
	{
		std::transform(Value.begin(), Value.end(), Value.begin(), ::towlower);
		return Value;
	}

	bool IsSupportedTextureExtension(const std::filesystem::path& Path)
	{
		const std::wstring Ext = ToLowerWide(Path.extension().wstring());
		return Ext == L".png" || Ext == L".jpg" || Ext == L".jpeg" || Ext == L".tga" || Ext == L".bmp";
	}

	std::wstring CanonicalTextureStem(const std::filesystem::path& Path)
	{
		std::wstring Stem = ToLowerWide(Path.stem().wstring());
		std::wstring Result;
		for (wchar_t Ch : Stem)
		{
			if (std::iswalnum(Ch))
			{
				Result.push_back(Ch);
			}
		}

		const std::wstring Tokens[] = { L"texture", L"tex", L"color", L"basecolor", L"diffuse", L"bc" };
		for (const std::wstring& Token : Tokens)
		{
			size_t Pos = std::wstring::npos;
			while ((Pos = Result.find(Token)) != std::wstring::npos)
			{
				Result.erase(Pos, Token.length());
			}
		}
		return Result;
	}

	FString ResolveImportedTexturePath(const FString& FbxFilePath, const FString& RawTexturePath)
	{
		if (RawTexturePath.empty())
		{
			return FString();
		}

		const std::filesystem::path RawPath(FPaths::ToWide(RawTexturePath));
		const std::filesystem::path DirectPath = ResolveProjectPath(RawTexturePath);
		if (std::filesystem::exists(DirectPath) && std::filesystem::is_regular_file(DirectPath))
		{
			return NormalizeProjectPath(FPaths::ToUtf8(DirectPath.generic_wstring()));
		}

		const std::filesystem::path FbxDirectory = ResolveProjectPath(FbxFilePath).parent_path();
		if (!std::filesystem::exists(FbxDirectory) || !std::filesystem::is_directory(FbxDirectory))
		{
			return NormalizeProjectPath(RawTexturePath);
		}

		const std::wstring RawFileName = ToLowerWide(RawPath.filename().wstring());
		const std::wstring RawStem = ToLowerWide(RawPath.stem().wstring());
		const std::wstring RawCanonicalStem = CanonicalTextureStem(RawPath);
		std::filesystem::path FallbackMatch;

		for (const std::filesystem::directory_entry& Entry : std::filesystem::recursive_directory_iterator(FbxDirectory))
		{
			if (!Entry.is_regular_file() || !IsSupportedTextureExtension(Entry.path()))
			{
				continue;
			}

			const std::filesystem::path CandidatePath = Entry.path();
			const std::wstring CandidateFileName = ToLowerWide(CandidatePath.filename().wstring());
			const std::wstring CandidateStem = ToLowerWide(CandidatePath.stem().wstring());
			if (CandidateFileName == RawFileName || CandidateStem == RawStem)
			{
				return NormalizeProjectPath(FPaths::ToUtf8(CandidatePath.generic_wstring()));
			}

			if (FallbackMatch.empty() && !RawCanonicalStem.empty() && CanonicalTextureStem(CandidatePath) == RawCanonicalStem)
			{
				FallbackMatch = CandidatePath;
			}
		}

		if (!FallbackMatch.empty())
		{
			return NormalizeProjectPath(FPaths::ToUtf8(FallbackMatch.generic_wstring()));
		}

		return NormalizeProjectPath(RawTexturePath);
	}

	void CollectFbxNodes(FbxNode* Node, TArray<FbxNode*>& OutNodes)
	{
		if (!Node)
		{
			return;
		}

		OutNodes.push_back(Node);
		for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
		{
			CollectFbxNodes(Node->GetChild(ChildIndex), OutNodes);
		}
	}

	bool ContainsIndex(const TArray<int32>* Indices, int32 Value)
	{
		if (!Indices)
		{
			return true;
		}

		return std::find(Indices->begin(), Indices->end(), Value) != Indices->end();
	}

	bool LoadConvertedFbxScene(const FString& FilePath, const char* SceneName, FbxManager*& OutSdkManager, FbxScene*& OutScene)
	{
		OutSdkManager = nullptr;
		OutScene = nullptr;

		FbxManager* SdkManager = FbxManager::Create();
		if (!SdkManager)
		{
			return false;
		}

		FbxIOSettings* ios = FbxIOSettings::Create(SdkManager, IOSROOT);
		if (!ios)
		{
			SdkManager->Destroy();
			return false;
		}
		SdkManager->SetIOSettings(ios);

		FbxScene* Scene = FbxScene::Create(SdkManager, SceneName);
		FbxImporter* Importer = FbxImporter::Create(SdkManager, "");
		if (!Scene || !Importer)
		{
			if (Importer) Importer->Destroy();
			SdkManager->Destroy();
			return false;
		}

		const FString FullPath = FPaths::ToUtf8(FPaths::Combine(FPaths::RootDir(), FPaths::ToWide(FilePath)));
		if (!Importer->Initialize(FullPath.c_str(), -1, SdkManager->GetIOSettings()))
		{
			Importer->Destroy();
			SdkManager->Destroy();
			return false;
		}

		const bool bImported = Importer->Import(Scene);
		Importer->Destroy();
		if (!bImported)
		{
			SdkManager->Destroy();
			return false;
		}

		FbxSystemUnit::m.ConvertScene(Scene);

		FbxAxisSystem UnrealAxisSystem(FbxAxisSystem::eZAxis, FbxAxisSystem::eParityEven, FbxAxisSystem::eLeftHanded);
		UnrealAxisSystem.DeepConvertScene(Scene);

		OutSdkManager = SdkManager;
		OutScene = Scene;
		return true;
	}

	float DotQuat(const FQuat& A, const FQuat& B)
	{
		return A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W;
	}

	bool AreVectorsEqual(const FVector& A, const FVector& B, float Tolerance = 0.0001f)
	{
		return std::abs(A.X - B.X) <= Tolerance
			&& std::abs(A.Y - B.Y) <= Tolerance
			&& std::abs(A.Z - B.Z) <= Tolerance;
	}

	bool AreQuatsEqual(const FQuat& A, const FQuat& B, float Tolerance = 0.0001f)
	{
		return std::abs(DotQuat(A.GetNormalized(), B.GetNormalized())) >= (1.0f - Tolerance);
	}

	void ReduceConstantVectorKeys(TArray<FVector>& Keys)
	{
		if (Keys.size() <= 1)
		{
			return;
		}

		const FVector First = Keys.front();
		for (const FVector& Key : Keys)
		{
			if (!AreVectorsEqual(First, Key))
			{
				return;
			}
		}

		Keys.clear();
		Keys.push_back(First);
	}

	void ReduceConstantQuatKeys(TArray<FQuat>& Keys)
	{
		if (Keys.size() <= 1)
		{
			return;
		}

		const FQuat First = Keys.front().GetNormalized();
		for (const FQuat& Key : Keys)
		{
			if (!AreQuatsEqual(First, Key))
			{
				return;
			}
		}

		Keys.clear();
		Keys.push_back(First);
	}

	int32 GetAnimationCurveRate(FbxAnimCurve* Curve)
	{
		if (!Curve || Curve->KeyGetCount() < 2)
		{
			return 0;
		}

		double MinDeltaSeconds = 0.0;
		for (int32 KeyIndex = 1; KeyIndex < Curve->KeyGetCount(); ++KeyIndex)
		{
			const double Prev = Curve->KeyGetTime(KeyIndex - 1).GetSecondDouble();
			const double Curr = Curve->KeyGetTime(KeyIndex).GetSecondDouble();
			const double Delta = Curr - Prev;
			if (Delta <= 0.0)
			{
				continue;
			}

			if (MinDeltaSeconds <= 0.0 || Delta < MinDeltaSeconds)
			{
				MinDeltaSeconds = Delta;
			}
		}

		if (MinDeltaSeconds <= 0.0)
		{
			return 0;
		}

		return static_cast<int32>(std::round(1.0 / MinDeltaSeconds));
	}

	void CollectNodeSampleRates(FbxNode* Node, FbxAnimLayer* AnimLayer, TArray<int32>& OutRates)
	{
		if (!Node || !AnimLayer)
		{
			return;
		}

		FbxAnimCurve* Curves[9] =
		{
			Node->LclTranslation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_X, false),
			Node->LclTranslation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, false),
			Node->LclTranslation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, false),
			Node->LclRotation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_X, false),
			Node->LclRotation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, false),
			Node->LclRotation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, false),
			Node->LclScaling.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_X, false),
			Node->LclScaling.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, false),
			Node->LclScaling.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, false),
		};

		for (FbxAnimCurve* Curve : Curves)
		{
			const int32 Rate = GetAnimationCurveRate(Curve);
			if (Rate > 0)
			{
				OutRates.push_back(Rate);
			}
		}
	}

	int32 CalculateAnimStackSampleRate(FbxScene* Scene, FbxAnimStack* Stack)
	{
		constexpr int32 DefaultSampleRate = 30;
		constexpr int32 MaxSampleRate = 120;

		if (!Scene || !Stack)
		{
			return DefaultSampleRate;
		}

		TArray<FbxNode*> Nodes;
		if (Scene->GetRootNode())
		{
			CollectFbxNodes(Scene->GetRootNode(), Nodes);
		}

		TArray<int32> Rates;
		const int32 LayerCount = Stack->GetMemberCount();
		for (int32 LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
		{
			FbxAnimLayer* AnimLayer = static_cast<FbxAnimLayer*>(Stack->GetMember(LayerIndex));
			for (FbxNode* Node : Nodes)
			{
				CollectNodeSampleRates(Node, AnimLayer, Rates);
			}
		}

		int32 Result = DefaultSampleRate;
		for (int32 Rate : Rates)
		{
			Result = std::max(Result, Rate);
		}

		return std::max(1, std::min(Result, MaxSampleRate));
	}
}


struct FFbxSkeletalVertexKey
{
	const FbxNode* MeshNode = nullptr;
	const FbxMesh* Mesh = nullptr;
	int32 ControlPointIndex = -1;
	float NormalX = 0.0f;
	float NormalY = 0.0f;
	float NormalZ = 0.0f;
	float UVX = 0.0f;
	float UVY = 0.0f;

	bool operator==(const FFbxSkeletalVertexKey& Other) const
	{
		return MeshNode == Other.MeshNode
			&& Mesh == Other.Mesh
			&& ControlPointIndex == Other.ControlPointIndex
			&& NormalX == Other.NormalX
			&& NormalY == Other.NormalY
			&& NormalZ == Other.NormalZ
			&& UVX == Other.UVX
			&& UVY == Other.UVY;
	}
};

namespace std
{
template<>
struct hash<FFbxSkeletalVertexKey>
{
	size_t operator()(const FFbxSkeletalVertexKey& Key) const noexcept
	{
		size_t Result = std::hash<const FbxNode*>()(Key.MeshNode);
		auto Combine = [&Result](size_t Value)
			{
				Result ^= Value + 0x9e3779b9 + (Result << 6) + (Result >> 2);
			};

		Combine(std::hash<const FbxMesh*>()(Key.Mesh));
		Combine(std::hash<int32>()(Key.ControlPointIndex));
		Combine(std::hash<float>()(Key.NormalX));
		Combine(std::hash<float>()(Key.NormalY));
		Combine(std::hash<float>()(Key.NormalZ));
		Combine(std::hash<float>()(Key.UVX));
		Combine(std::hash<float>()(Key.UVY));
		return Result;
	}
};
}

struct FFbxStaticVertexKey
{
	int32 ControlPointIndex = -1;
	float NormalX = 0.0f;
	float NormalY = 0.0f;
	float NormalZ = 0.0f;
	float UVX = 0.0f;
	float UVY = 0.0f;

	bool operator==(const FFbxStaticVertexKey& Other) const
	{
		return ControlPointIndex == Other.ControlPointIndex
			&& NormalX == Other.NormalX
			&& NormalY == Other.NormalY
			&& NormalZ == Other.NormalZ
			&& UVX == Other.UVX
			&& UVY == Other.UVY;
	}
};

namespace std
{
template<>
struct hash<FFbxStaticVertexKey>
{
	size_t operator()(const FFbxStaticVertexKey& Key) const noexcept
	{
		size_t Result = std::hash<int32>()(Key.ControlPointIndex);
		auto Combine = [&Result](size_t Value)
			{
				Result ^= Value + 0x9e3779b9 + (Result << 6) + (Result >> 2);
			};

		Combine(std::hash<float>()(Key.NormalX));
		Combine(std::hash<float>()(Key.NormalY));
		Combine(std::hash<float>()(Key.NormalZ));
		Combine(std::hash<float>()(Key.UVX));
		Combine(std::hash<float>()(Key.UVY));
		return Result;
	}
};
}

static FMatrix ConvertFbxMatrix(const FbxMatrix& FbxMat);
static FbxAMatrix GetGeometryTransform(FbxNode* Node);
static bool IsSkeletonNode(FbxNode* Node);

static bool IsValidControlPointIndex(const FbxMesh* Mesh, int32 ControlPointIndex)
{
	return Mesh && ControlPointIndex >= 0 && ControlPointIndex < Mesh->GetControlPointsCount();
}

struct FFbxSkinClusterRef
{
	FbxNode* MeshNode = nullptr;
	FbxMesh* Mesh = nullptr;
	FbxSkin* Skin = nullptr;
	FbxCluster* Cluster = nullptr;
};

struct FFbxBindPoseMatrix
{
	FbxMatrix Matrix;
	bool bLocalMatrix = false;
};

struct FFbxWeightData
{
	int32 BoneIndex = -1;
	float Weight = 0.0f;
};

static void CollectMeshSkins(FbxMesh* Mesh, TArray<FbxSkin*>& OutSkins)
{
	OutSkins.clear();
	if (!Mesh)
	{
		return;
	}

	const int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
	for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
	{
		FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
		if (Skin && Skin->GetClusterCount() > 0)
		{
			OutSkins.push_back(Skin);
		}
	}
}

static void CollectSkinClusters(const TArray<FbxNode*>& Nodes, TArray<FFbxSkinClusterRef>& OutClusters)
{
	OutClusters.clear();
	for (FbxNode* Node : Nodes)
	{
		if (!Node)
		{
			continue;
		}

		FbxMesh* Mesh = Node->GetMesh();
		if (!Mesh)
		{
			continue;
		}

		TArray<FbxSkin*> Skins;
		CollectMeshSkins(Mesh, Skins);
		for (FbxSkin* Skin : Skins)
		{
			const int32 ClusterCount = Skin ? Skin->GetClusterCount() : 0;
			for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
			{
				FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
				if (!Cluster)
				{
					continue;
				}

				FFbxSkinClusterRef Ref;
				Ref.MeshNode = Node;
				Ref.Mesh = Mesh;
				Ref.Skin = Skin;
				Ref.Cluster = Cluster;
				OutClusters.push_back(Ref);
			}
		}
	}
}

static bool TryGetBindPoseMatrix(FbxScene* Scene, FbxNode* Node, FFbxBindPoseMatrix& OutMatrix)
{
	if (!Scene || !Node)
	{
		return false;
	}

	const int32 PoseCount = Scene->GetPoseCount();
	for (int32 PoseIndex = 0; PoseIndex < PoseCount; ++PoseIndex)
	{
		FbxPose* Pose = Scene->GetPose(PoseIndex);
		if (!Pose || !Pose->IsBindPose())
		{
			continue;
		}

		const int32 NodePoseIndex = Pose->Find(Node);
		if (NodePoseIndex < 0)
		{
			continue;
		}

		OutMatrix.Matrix = Pose->GetMatrix(NodePoseIndex);
		OutMatrix.bLocalMatrix = Pose->IsLocalMatrix(NodePoseIndex);
		return true;
	}

	return false;
}

static void AddBoneCandidateWithAncestors(FbxNode* Node, FbxNode* SceneRoot, TSet<FbxNode*>& OutCandidates)
{
	while (Node && Node != SceneRoot)
	{
		OutCandidates.insert(Node);
		Node = Node->GetParent();
	}
}

static bool HasInfluencingLinkDescendant(FbxNode* Node, const TSet<FbxNode*>& LinkNodes)
{
	if (!Node)
	{
		return false;
	}

	if (LinkNodes.find(Node) != LinkNodes.end())
	{
		return true;
	}

	for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
	{
		if (HasInfluencingLinkDescendant(Node->GetChild(ChildIndex), LinkNodes))
		{
			return true;
		}
	}

	return false;
}

static int32 CountInfluencingLinkChildBranches(FbxNode* Node, const TSet<FbxNode*>& LinkNodes)
{
	if (!Node)
	{
		return 0;
	}

	int32 BranchCount = 0;
	for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
	{
		if (HasInfluencingLinkDescendant(Node->GetChild(ChildIndex), LinkNodes))
		{
			++BranchCount;
		}
	}

	return BranchCount;
}

static FbxNode* FindTopmostBoneRoot(FbxNode* BoneNode, const TSet<FbxNode*>& BoneNodes, const TSet<FbxNode*>& LinkNodes, FbxNode* SceneRoot)
{
	FbxNode* Result = BoneNode;
	FbxNode* Parent = BoneNode ? BoneNode->GetParent() : nullptr;
	while (Parent && Parent != SceneRoot && BoneNodes.find(Parent) != BoneNodes.end())
	{
		const bool bParentIsInfluencingLink = LinkNodes.find(Parent) != LinkNodes.end();
		const bool bParentIsSkeletonNode = IsSkeletonNode(Parent);
		const int32 InfluencingChildBranches = CountInfluencingLinkChildBranches(Parent, LinkNodes);
		if (!bParentIsInfluencingLink && !bParentIsSkeletonNode && InfluencingChildBranches > 1)
		{
			break;
		}

		Result = Parent;
		Parent = Parent->GetParent();
	}

	return Result;
}

static FbxNode* FindFirstRootBoneNode(const TArray<FbxNode*>& Nodes, const TMap<FbxNode*, int32>& NodeToIndex, const TArray<FBone>& InBones)
{
	for (FbxNode* Node : Nodes)
	{
		auto It = NodeToIndex.find(Node);
		if (It == NodeToIndex.end())
		{
			continue;
		}

		const int32 BoneIndex = It->second;
		if (BoneIndex >= 0 && BoneIndex < static_cast<int32>(InBones.size()) && InBones[BoneIndex].ParentIndex < 0)
		{
			return Node;
		}
	}

	return nullptr;
}

static FMatrix ResolveMeshBindGlobal(FbxNode* Node, const TArray<FbxSkin*>& Skins)
{
	FbxAMatrix NodeGeometryTransform = GetGeometryTransform(Node);
	FMatrix MeshBindGlobal = ConvertFbxMatrix(Node->EvaluateGlobalTransform() * NodeGeometryTransform);

	for (FbxSkin* Skin : Skins)
	{
		if (!Skin)
		{
			continue;
		}

		const int32 ClusterCount = Skin->GetClusterCount();
		for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
		{
			FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
			if (!Cluster)
			{
				continue;
			}

			FbxAMatrix MeshBindMatrix;
			Cluster->GetTransformMatrix(MeshBindMatrix);
			return ConvertFbxMatrix(MeshBindMatrix);
		}
	}

	return MeshBindGlobal;
}

static void AccumulateSkeletalTangents(
	const TArray<FVertexPNCTBW>& InVertices,
	TArray<FVector>& InOutTangentSums,
	TArray<FVector>& InOutBitangentSums,
	const uint32 TriIndices[3])
{
	if (TriIndices[0] >= InVertices.size() || TriIndices[1] >= InVertices.size() || TriIndices[2] >= InVertices.size())
	{
		return;
	}

	const FVertexPNCTBW& V0 = InVertices[TriIndices[0]];
	const FVertexPNCTBW& V1 = InVertices[TriIndices[1]];
	const FVertexPNCTBW& V2 = InVertices[TriIndices[2]];

	FVector Edge1 = V1.Position - V0.Position;
	FVector Edge2 = V2.Position - V0.Position;

	FVector2 DeltaUV1 = V1.UV - V0.UV;
	FVector2 DeltaUV2 = V2.UV - V0.UV;

	const float Det = DeltaUV1.X * DeltaUV2.Y - DeltaUV1.Y * DeltaUV2.X;
	if (std::abs(Det) < 1e-8f)
	{
		return;
	}

	const float InvDet = 1.0f / Det;
	const FVector Tangent = (Edge1 * DeltaUV2.Y - Edge2 * DeltaUV1.Y) * InvDet;
	const FVector Bitangent = (Edge2 * DeltaUV1.X - Edge1 * DeltaUV2.X) * InvDet;

	for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
	{
		const uint32 VertexIndex = TriIndices[CornerIndex];
		if (VertexIndex < InOutTangentSums.size())
		{
			InOutTangentSums[VertexIndex] += Tangent;
		}
		if (VertexIndex < InOutBitangentSums.size())
		{
			InOutBitangentSums[VertexIndex] += Bitangent;
		}
	}
}

static void BuildSkeletalTangents(TArray<FVertexPNCTBW>& InOutVertices, const TArray<FVector>& InTangentSums, const TArray<FVector>& InBitangentSums)
{
	for (uint32 VertexIndex = 0; VertexIndex < static_cast<uint32>(InOutVertices.size()); ++VertexIndex)
	{
		FVector N = InOutVertices[VertexIndex].Normal;
		FVector T = VertexIndex < InTangentSums.size() ? InTangentSums[VertexIndex] : FVector::ZeroVector;

		T = T - N * N.Dot(T);
		if (T.Length() < FMath::Epsilon)
		{
			FVector Axis = std::abs(N.Z) < 0.999f ? FVector(0.0f, 0.0f, 1.0f) : FVector(0.0f, 1.0f, 0.0f);
			T = Axis.Cross(N).Normalized();
		}
		else
		{
			T.Normalize();
		}

		FVector B = VertexIndex < InBitangentSums.size() ? InBitangentSums[VertexIndex] : FVector::ZeroVector;
		const float Handedness = N.Cross(T).Dot(B) < 0.0f ? -1.0f : 1.0f;
		InOutVertices[VertexIndex].Tangent = FVector4(T, Handedness);
	}
}

static bool CompareMaterialSectionKey(int32 A, int32 B)
{
	if ((A < 0) != (B < 0))
	{
		return A >= 0;
	}

	return A < B;
}

bool FEditorFbxImporter::Import(const FString& FilePath)
{
	// FBX import 결과가 바로 엔진 전용 cooked binary cache에 저장되므로,
	// 이전 import의 static 임시 데이터가 섞이지 않게 시작 지점에서 모두 정리
	Vertices.clear();
	Indices.clear();
	Bones.clear();
	Sections.clear();
	ImportedSkeletalMeshes.clear();

	MtlInfos.clear();
	MaterialToSlotIndex.clear();
	CurrentSourcePath = NormalizeProjectPath(FilePath);

	TangentSums.clear();
	BitangentSums.clear();

	FbxManager* SdkManager = FbxManager::Create();
	if (!SdkManager)
	{
		return false;
	}

	FbxIOSettings* ios = FbxIOSettings::Create(SdkManager, IOSROOT);
	if (!ios)
	{
		SdkManager->Destroy();
		return false;
	}
	SdkManager->SetIOSettings(ios);

	FbxScene* Scene = FbxScene::Create(SdkManager, "My Scene");
	if (!Scene)
	{
		SdkManager->Destroy();
		return false;
	}

	FbxImporter* Importer = FbxImporter::Create(SdkManager, "");
	if (!Importer)
	{
		SdkManager->Destroy();
		return false;
	}

	FString FullPath = FPaths::ToUtf8(FPaths::Combine(FPaths::RootDir(), FPaths::ToWide(FilePath)));

	if (!Importer->Initialize(FullPath.c_str(), -1, SdkManager->GetIOSettings()))
	{
		Importer->Destroy();
		SdkManager->Destroy();
		return false;
	}

	if (!Importer->Import(Scene))
	{
		Importer->Destroy();
		SdkManager->Destroy();
		return false;
	}
	Importer->Destroy();

	// 임의로 m 변환. UE는 cm 단위
	FbxSystemUnit::m.ConvertScene(Scene);

	FbxAxisSystem UnrealAxisSystem(FbxAxisSystem::eZAxis, FbxAxisSystem::eParityEven, FbxAxisSystem::eLeftHanded);
	UnrealAxisSystem.DeepConvertScene(Scene);

	TriangulateScene(Scene);

	if (!Parse(Scene))
	{
		SdkManager->Destroy();
		return false;
	}

	if (!Convert())
	{
		SdkManager->Destroy();
		return false;
	}

	if (ImportedSkeletalMeshes.empty())
	{
		SdkManager->Destroy();
		return false;
	}

	SdkManager->Destroy();
	return true;
}

bool FEditorFbxImporter::DiscoverMeshNames(const FString& FilePath, TArray<FString>& OutMeshNames)
{
	OutMeshNames.clear();

	FbxManager* SdkManager = FbxManager::Create();
	if (!SdkManager)
	{
		return false;
	}

	FbxIOSettings* ios = FbxIOSettings::Create(SdkManager, IOSROOT);
	if (!ios)
	{
		SdkManager->Destroy();
		return false;
	}
	SdkManager->SetIOSettings(ios);

	FbxScene* Scene = FbxScene::Create(SdkManager, "FBX Mesh Discovery Scene");
	FbxImporter* Importer = FbxImporter::Create(SdkManager, "");
	if (!Scene || !Importer)
	{
		if (Importer) Importer->Destroy();
		SdkManager->Destroy();
		return false;
	}

	if (!Importer->Initialize(FilePath.c_str(), -1, SdkManager->GetIOSettings()))
	{
		Importer->Destroy();
		SdkManager->Destroy();
		return false;
	}

	const bool bImported = Importer->Import(Scene);
	Importer->Destroy();
	if (!bImported || !Scene->GetRootNode())
	{
		SdkManager->Destroy();
		return false;
	}

	TArray<FbxNode*> Nodes;
	CollectNodes(Scene->GetRootNode(), 0, Nodes);
	for (FbxNode* Node : Nodes)
	{
		if (Node && Node->GetMesh())
		{
			OutMeshNames.push_back(Node->GetName());
		}
	}

	SdkManager->Destroy();
	return !OutMeshNames.empty();
}

bool FEditorFbxImporter::DiscoverAnimStackNames(const FString& FilePath, TArray<FString>& OutStackNames)
{
	OutStackNames.clear();

	FbxManager* SdkManager = nullptr;
	FbxScene* Scene = nullptr;
	if (!LoadConvertedFbxScene(FilePath, "FBX Anim Discovery Scene", SdkManager, Scene))
	{
		return false;
	}

	const int32 StackCount = Scene->GetSrcObjectCount<FbxAnimStack>();
	for (int32 StackIndex = 0; StackIndex < StackCount; ++StackIndex)
	{
		FbxAnimStack* Stack = Scene->GetSrcObject<FbxAnimStack>(StackIndex);
		if (!Stack)
		{
			continue;
		}

		const char* StackName = Stack->GetName();
		OutStackNames.push_back(StackName && StackName[0] ? FString(StackName) : FString("Take_") + std::to_string(StackIndex + 1));
	}

	SdkManager->Destroy();
	return true;
}

bool FEditorFbxImporter::ImportAnimations(const FString& FilePath, const FSkeletonAsset* TargetSkeleton, FSkeletonAsset& OutParsedSkeleton, TArray<FImportedAnimSequence>& OutSequences)
{
	OutParsedSkeleton = FSkeletonAsset();
	OutSequences.clear();

	Vertices.clear();
	Indices.clear();
	Bones.clear();
	Sections.clear();
	ImportedSkeletalMeshes.clear();
	MtlInfos.clear();
	MaterialToSlotIndex.clear();
	TangentSums.clear();
	BitangentSums.clear();
	CurrentSourcePath = NormalizeProjectPath(FilePath);

	FbxManager* SdkManager = nullptr;
	FbxScene* Scene = nullptr;
	if (!LoadConvertedFbxScene(FilePath, "Animation FBX Scene", SdkManager, Scene))
	{
		return false;
	}

	FbxNode* RootNode = Scene->GetRootNode();
	if (!RootNode)
	{
		SdkManager->Destroy();
		return false;
	}

	TArray<FbxNode*> Nodes;
	TMap<FbxNode*, int32> NodeToIndex;
	CollectNodes(RootNode, 0, Nodes);
	ParseBone(Scene, Nodes, NodeToIndex);

	OutParsedSkeleton.PathFileName = CurrentSourcePath;
	OutParsedSkeleton.Bones = Bones;

	const FSkeletonAsset* SkeletonForTracks = TargetSkeleton && !TargetSkeleton->Bones.empty()
		? TargetSkeleton
		: &OutParsedSkeleton;

	ParseAnimations(Scene, *SkeletonForTracks, OutSequences);

	SdkManager->Destroy();
	return true;
}

bool FEditorFbxImporter::ImportStatic(const FString& FilePath, const FImportOptions* Options, FStaticMesh& OutMesh, TArray<FStaticMaterial>& OutMaterials, const TArray<int32>* MeshNodeIndexFilter)
{
	OutMesh = FStaticMesh();
	OutMaterials.clear();

	MtlInfos.clear();
	MaterialToSlotIndex.clear();
	CurrentSourcePath = NormalizeProjectPath(FilePath);

	FbxManager* SdkManager = FbxManager::Create();
	if (!SdkManager) return false;

	FbxIOSettings* ios = FbxIOSettings::Create(SdkManager, IOSROOT);
	if (!ios)
	{
		SdkManager->Destroy();
		return false;
	}
	SdkManager->SetIOSettings(ios);

	FbxScene* Scene = FbxScene::Create(SdkManager, "Static FBX Scene");
	FbxImporter* Importer = FbxImporter::Create(SdkManager, "");
	if (!Scene || !Importer)
	{
		if (Importer) Importer->Destroy();
		SdkManager->Destroy();
		return false;
	}

	FString FullPath = FPaths::ToUtf8(FPaths::Combine(FPaths::RootDir(), FPaths::ToWide(FilePath)));
	if (!Importer->Initialize(FullPath.c_str(), -1, SdkManager->GetIOSettings()))
	{
		SdkManager->Destroy();
		return false;
	}

	if (!Importer->Import(Scene))
	{
		Importer->Destroy();
		SdkManager->Destroy();
		return false;
	}
	Importer->Destroy();

	FbxSystemUnit::m.ConvertScene(Scene);

	FbxAxisSystem UnrealAxisSystem(FbxAxisSystem::eZAxis, FbxAxisSystem::eParityEven, FbxAxisSystem::eLeftHanded);
	UnrealAxisSystem.DeepConvertScene(Scene);

	FbxNode* RootNode = Scene->GetRootNode();
	if (!RootNode)
	{
		SdkManager->Destroy();
		return false;
	}

	TArray<FbxNode*> Nodes;
	CollectNodes(RootNode, 0, Nodes);
	CollectMaterials(Scene);

	OutMaterials.reserve(MtlInfos.size());
	for (const FMaterialInfo& MatInfo : MtlInfos)
	{
		FStaticMaterial NewMaterial;
		NewMaterial.MaterialSlotName = MatInfo.Name;
		NewMaterial.MaterialInterface = FMaterialManager::Get().GetOrCreateMaterial(ConvertToMat(&MatInfo));
		OutMaterials.push_back(NewMaterial);
	}

	TArray<FVector> StaticTangentSums;
	TArray<FVector> StaticBitangentSums;
	bool bNeedsNoneSlot = OutMaterials.empty();

	int32 MeshNodeIndex = 0;
	for (FbxNode* Node : Nodes)
	{
		if (!Node) continue;

		FbxMesh* Mesh = Node->GetMesh();
		if (!Mesh) continue;
		const int32 CurrentMeshNodeIndex = MeshNodeIndex++;
		if (!ContainsIndex(MeshNodeIndexFilter, CurrentMeshNodeIndex))
		{
			continue;
		}

		const int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
		FbxSkin* Skin = SkinCount > 0 ? static_cast<FbxSkin*>(Mesh->GetDeformer(0, FbxDeformer::eSkin)) : nullptr;
		const bool bHasSkin = Skin && Skin->GetClusterCount() > 0;
		const EStaticFbxSkinnedMeshPolicy SkinnedMeshPolicy = Options
			? Options->StaticFbxSkinnedMeshPolicy
			: EStaticFbxSkinnedMeshPolicy::Skip;

		FbxAMatrix NodeGeometryTransform = GetGeometryTransform(Node);
		FMatrix MeshToWorld = ConvertFbxMatrix(Node->EvaluateGlobalTransform() * NodeGeometryTransform);

		if (bHasSkin)
		{
			switch (SkinnedMeshPolicy)
			{
			case EStaticFbxSkinnedMeshPolicy::Skip:
				continue;
			case EStaticFbxSkinnedMeshPolicy::ImportBindPoseAsStatic:
			{
				FbxAMatrix MeshBindMatrix;
				Skin->GetCluster(0)->GetTransformMatrix(MeshBindMatrix);
				MeshToWorld = ConvertFbxMatrix(MeshBindMatrix);
				break;
			}
			}
		}


		FbxStringList UVSetNames;
		Mesh->GetUVSetNames(UVSetNames);
		const char* UVName = (UVSetNames.GetCount() > 0) ? UVSetNames.GetStringAt(0) : nullptr;

		TArray<int32> LocalToGlobalMaterialIndex;
		LocalToGlobalMaterialIndex.resize(Node->GetMaterialCount());
		for (int32 LocalIndex = 0; LocalIndex < Node->GetMaterialCount(); ++LocalIndex)
		{
			FbxSurfaceMaterial* Material = Node->GetMaterial(LocalIndex);
			auto It = MaterialToSlotIndex.find(Material);
			LocalToGlobalMaterialIndex[LocalIndex] = (It != MaterialToSlotIndex.end()) ? It->second : -1;
		}

		TMap<int32, TArray<uint32>> SectionIndicesMap;
		TMap<FFbxStaticVertexKey, uint32> VertexMap;

		for (int32 PolygonIndex = 0; PolygonIndex < Mesh->GetPolygonCount(); ++PolygonIndex)
		{
			const int32 PolygonSize = Mesh->GetPolygonSize(PolygonIndex);
			if (PolygonSize < 3)
			{
				continue;
			}

			const int32 LocalMaterialIndex = GetMaterialIndex(Mesh, PolygonIndex);
			int32 GlobalMaterialIndex = -1;
			if (LocalMaterialIndex >= 0 && LocalMaterialIndex < static_cast<int32>(LocalToGlobalMaterialIndex.size()))
			{
				GlobalMaterialIndex = LocalToGlobalMaterialIndex[LocalMaterialIndex];
			}

			auto BuildStaticVertex = [&](int32 CornerIndex, uint32& OutVertexIndex) -> bool
			{
				FNormalVertex Vertex;
				const int32 CPIndex = Mesh->GetPolygonVertex(PolygonIndex, CornerIndex);
				if (!IsValidControlPointIndex(Mesh, CPIndex))
				{
					return false;
				}

				FbxVector4 CP = Mesh->GetControlPointAt(CPIndex);
				Vertex.pos = MeshToWorld.TransformPositionWithW(FVector((float)CP[0], (float)CP[1], (float)CP[2]));

				FbxVector4 Normal;
				Mesh->GetPolygonVertexNormal(PolygonIndex, CornerIndex, Normal);
				Normal.Normalize();
				Vertex.normal = MeshToWorld.TransformVector(FVector((float)Normal[0], (float)Normal[1], (float)Normal[2]));
				if (!Vertex.normal.IsNearlyZero())
				{
					Vertex.normal.Normalize();
				}

				Vertex.color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
				Vertex.tex = FVector2(0.0f, 0.0f);
				if (UVName)
				{
					FbxVector2 UV;
					bool bUnmappedUV = false;
					const bool bSuccess = Mesh->GetPolygonVertexUV(PolygonIndex, CornerIndex, UVName, UV, bUnmappedUV);
					if (bSuccess && !bUnmappedUV)
					{
						Vertex.tex = FVector2((float)UV[0], 1.0f - (float)UV[1]);
					}
				}

				FFbxStaticVertexKey Key;
				Key.ControlPointIndex = CPIndex;
				Key.NormalX = Vertex.normal.X;
				Key.NormalY = Vertex.normal.Y;
				Key.NormalZ = Vertex.normal.Z;
				Key.UVX = Vertex.tex.X;
				Key.UVY = Vertex.tex.Y;

				auto It = VertexMap.find(Key);
				if (It != VertexMap.end())
				{
					OutVertexIndex = It->second;
				}
				else
				{
					OutVertexIndex = static_cast<uint32>(OutMesh.Vertices.size());
					OutMesh.Vertices.push_back(Vertex);
					StaticTangentSums.push_back(FVector::ZeroVector);
					StaticBitangentSums.push_back(FVector::ZeroVector);
					VertexMap[Key] = OutVertexIndex;
				}

				return true;
			};

			for (int32 FanIndex = 1; FanIndex + 1 < PolygonSize; ++FanIndex)
			{
				const int32 CornerIndices[3] = { 0, FanIndex, FanIndex + 1 };
				uint32 TriIndices[3] = {};
				bool bValidTriangle = true;
				for (int32 TriangleCornerIndex = 0; TriangleCornerIndex < 3; ++TriangleCornerIndex)
				{
					if (!BuildStaticVertex(CornerIndices[TriangleCornerIndex], TriIndices[TriangleCornerIndex]))
					{
						bValidTriangle = false;
						break;
					}
				}

				if (!bValidTriangle)
				{
					continue;
				}

				for (uint32 VertexIndex : TriIndices)
				{
					SectionIndicesMap[GlobalMaterialIndex].push_back(VertexIndex);
				}

				const FNormalVertex& V0 = OutMesh.Vertices[TriIndices[0]];
				const FNormalVertex& V1 = OutMesh.Vertices[TriIndices[1]];
				const FNormalVertex& V2 = OutMesh.Vertices[TriIndices[2]];

				FVector Edge1 = V1.pos - V0.pos;
				FVector Edge2 = V2.pos - V0.pos;
				FVector2 DeltaUV1 = V1.tex - V0.tex;
				FVector2 DeltaUV2 = V2.tex - V0.tex;

				float Det = DeltaUV1.X * DeltaUV2.Y - DeltaUV1.Y * DeltaUV2.X;
				if (std::abs(Det) >= 1e-8f)
				{
					float InvDet = 1.0f / Det;
					FVector Tangent = (Edge1 * DeltaUV2.Y - Edge2 * DeltaUV1.Y) * InvDet;
					FVector Bitangent = (Edge2 * DeltaUV1.X - Edge1 * DeltaUV2.X) * InvDet;

					for (uint32 TriIndex : TriIndices)
					{
						StaticTangentSums[TriIndex] += Tangent;
						StaticBitangentSums[TriIndex] += Bitangent;
					}
				}
			}
		}

		uint32 CurrentBaseIndex = static_cast<uint32>(OutMesh.Indices.size());
		for (auto& Pair : SectionIndicesMap)
		{
			FStaticMeshSection Section;
			const int32 MatIndex = Pair.first;
			if (MatIndex >= 0 && MatIndex < static_cast<int32>(MtlInfos.size()))
			{
				Section.MaterialSlotName = MtlInfos[MatIndex].Name;
				Section.MaterialIndex = MatIndex;
			}
			else
			{
				Section.MaterialSlotName = "None";
				Section.MaterialIndex = -1;
				bNeedsNoneSlot = true;
			}

			Section.FirstIndex = CurrentBaseIndex;
			Section.NumTriangles = static_cast<uint32>(Pair.second.size() / 3);
			CurrentBaseIndex += static_cast<uint32>(Pair.second.size());
			OutMesh.Indices.insert(OutMesh.Indices.end(), Pair.second.begin(), Pair.second.end());
			OutMesh.Sections.push_back(Section);
		}
	}

	if (bNeedsNoneSlot)
	{
		FStaticMaterial DefaultMaterial;
		DefaultMaterial.MaterialSlotName = "None";
		DefaultMaterial.MaterialInterface = FMaterialManager::Get().GetOrCreateMaterial("None");
		OutMaterials.push_back(DefaultMaterial);
		const int32 NoneMaterialIndex = static_cast<int32>(OutMaterials.size()) - 1;
		for (FStaticMeshSection& Section : OutMesh.Sections)
		{
			if (Section.MaterialSlotName == "None")
			{
				Section.MaterialIndex = NoneMaterialIndex;
			}
		}
	}

	for (uint32 VertexIndex = 0; VertexIndex < static_cast<uint32>(OutMesh.Vertices.size()); ++VertexIndex)
	{
		FNormalVertex& Vertex = OutMesh.Vertices[VertexIndex];
		FVector N = Vertex.normal.Normalized();
		FVector T = StaticTangentSums[VertexIndex];
		T = T - N * N.Dot(T);

		if (T.Length() < 1e-8f)
		{
			FVector Axis = std::abs(N.Z) < 0.999f ? FVector(0.0f, 0.0f, 1.0f) : FVector(0.0f, 1.0f, 0.0f);
			T = Axis.Cross(N).Normalized();
		}
		else
		{
			T.Normalize();
		}

		FVector B = StaticBitangentSums[VertexIndex];
		float Handedness = N.Cross(T).Dot(B) < 0.0f ? -1.0f : 1.0f;
		Vertex.tangent = FVector4(T, Handedness);
	}

	OutMesh.PathFileName = FilePath;
	SdkManager->Destroy();
	return !OutMesh.Vertices.empty() && !OutMesh.Indices.empty();
}

bool FEditorFbxImporter::Parse(FbxScene* Scene)
{
	FbxNode* RootNode = Scene->GetRootNode();

	if (!RootNode)
	{
		return false;
	}

	TArray<FbxNode*> Nodes;
	TMap<FbxNode*, int32> NodeToIndex;

	CollectNodes(RootNode, 0, Nodes);
	CollectMaterials(Scene);

	ParseBone(Scene, Nodes, NodeToIndex);
	ParseSkin(Nodes, NodeToIndex);
	
	return true;
}

bool FEditorFbxImporter::Convert()
{
	TArray<FSkeletalMaterial> BaseMaterials;

	for (const FMaterialInfo& MatInfo : MtlInfos)
	{
		FString MaterialPath = ConvertToMat(&MatInfo);
		UMaterial* MaterialObject = FMaterialManager::Get().GetOrCreateMaterial(MaterialPath);

		FSkeletalMaterial NewMaterial;
		NewMaterial.MaterialInterface = MaterialObject;
		NewMaterial.MaterialSlotName = MatInfo.Name;
		NewMaterial.MaterialPath = MaterialPath;		// *.mat 파일 Path
		BaseMaterials.push_back(NewMaterial);
	}

	// Default Material 경우 추가
	for (FImportedSkeletalMesh& ImportedMesh : ImportedSkeletalMeshes)
	{
		ImportedMesh.Materials = BaseMaterials;
		bool bNeedsNoneSlot = ImportedMesh.Materials.empty();

	for (const FSkeletalMeshSection& Section : ImportedMesh.Sections)
	{
		if (Section.MaterialSlotName == "None")
		{
			bNeedsNoneSlot = true;
			break;
		}
	}

	if (bNeedsNoneSlot)
	{
		FSkeletalMaterial DefaultMaterial;
		DefaultMaterial.MaterialInterface = FMaterialManager::Get().GetOrCreateMaterial("None");
		DefaultMaterial.MaterialSlotName = "None";
		DefaultMaterial.MaterialPath = DefaultMaterial.MaterialInterface
			? DefaultMaterial.MaterialInterface->GetAssetPathFileName()
			: FString(); // GetOrCreateMaterial("None");이 성공하면 해당 Default Material의 PathFileName을 MaterialPath로 사용.
		ImportedMesh.Materials.push_back(DefaultMaterial);

		const int32 NoneMaterialIndex = static_cast<int32>(ImportedMesh.Materials.size()) - 1;
		for (FSkeletalMeshSection& Section : ImportedMesh.Sections)
		{
			if (Section.MaterialSlotName == "None")
			{
				Section.MaterialIndex = NoneMaterialIndex;
			}
		}
	}

	}

	return true;
}

void FEditorFbxImporter::CollectNodes(FbxNode* Node, int32 depth, TArray<FbxNode*>& OutNodes)
{
	OutNodes.push_back(Node);

	for (int i = 0; i < Node->GetChildCount(); ++i)
	{
		CollectNodes(Node->GetChild(i), depth + 1, OutNodes);
	}
}

void FEditorFbxImporter::CollectMaterials(FbxScene* Scene)
{
	MtlInfos.clear();
	MaterialToSlotIndex.clear();

	int32 MaterialCount = Scene->GetMaterialCount();
	TMap<FString, int32> MaterialNameCounts;

	for (int32 i = 0; i < MaterialCount; ++i)
	{
		FbxSurfaceMaterial* Material = Scene->GetMaterial(i);
		if (!Material) continue;

		FMaterialInfo MatInfo;
		MatInfo.Name = MakeUniqueMaterialSlotName(Material->GetName(), MaterialNameCounts);
		MatInfo.DiffuseColor = { 1.0f, 1.0f, 1.0f };

		FbxProperty DiffuseProp = Material->FindProperty(FbxSurfaceMaterial::sDiffuse);
		if (DiffuseProp.IsValid())
		{
			FbxDouble3 Color = DiffuseProp.Get<FbxDouble3>();
			MatInfo.DiffuseColor = { (float)Color[0], (float)Color[1], (float)Color[2] };

			int32 TextureCount = DiffuseProp.GetSrcObjectCount<FbxTexture>();
			if (TextureCount > 0)
			{
				FbxFileTexture* Texture = DiffuseProp.GetSrcObject<FbxFileTexture>(0);
				if (Texture)
				{
					FString RawTexturePath = Texture->GetFileName();
					MatInfo.TexturePath = ResolveImportedTexturePath(CurrentSourcePath, RawTexturePath);
				}
			}
		}

		auto ReadTexturePath = [](const FbxProperty& Property) -> FString
			{
				if (!Property.IsValid()) return "";

				int32 TextureCount = Property.GetSrcObjectCount<FbxTexture>();
				for (int32 TextureIndex = 0; TextureIndex < TextureCount; ++TextureIndex)
				{
					FbxFileTexture* Texture = Property.GetSrcObject<FbxFileTexture>(TextureIndex);
					if (Texture)
					{
						return ResolveImportedTexturePath(CurrentSourcePath, Texture->GetFileName());
					}
				}

				return "";
			};

		FbxProperty NormalProp = Material->FindProperty(FbxSurfaceMaterial::sNormalMap);
		MatInfo.NormalTexturePath = ReadTexturePath(NormalProp);

		if (MatInfo.NormalTexturePath.empty())
		{
			FbxProperty BumpProp = Material->FindProperty(FbxSurfaceMaterial::sBump);
			MatInfo.NormalTexturePath = ReadTexturePath(BumpProp);
		}

		int32 GlobalIndex = (int32)MtlInfos.size();
		MtlInfos.push_back(MatInfo);
		MaterialToSlotIndex[Material] = GlobalIndex;
	}
}

static FMatrix ConvertFbxMatrix(const FbxMatrix& FbxMat)
{
	FMatrix Mat;
	for (int i = 0; i < 4; ++i)
	{
		for (int j = 0; j < 4; ++j)
		{
			Mat.M[i][j] = (float)FbxMat.Get(i, j);
		}
	}
	return Mat;
}

static bool IsSkeletonNode(FbxNode* Node)
{
	FbxNodeAttribute* Attr = Node ? Node->GetNodeAttribute() : nullptr;
	return Attr && Attr->GetAttributeType() == FbxNodeAttribute::eSkeleton;
}

static FbxAMatrix GetGeometryTransform(FbxNode* Node)
{
	FbxAMatrix GeometryTransform;
	if (!Node)
	{
		return GeometryTransform;
	}

	GeometryTransform.SetT(Node->GetGeometricTranslation(FbxNode::eSourcePivot));
	GeometryTransform.SetR(Node->GetGeometricRotation(FbxNode::eSourcePivot));
	GeometryTransform.SetS(Node->GetGeometricScaling(FbxNode::eSourcePivot));
	return GeometryTransform;
}

static int32 AddSyntheticRootBoneIfNeeded(FbxNode* Node, TArray<FBone>& Bones, TMap<FbxNode*, int32>& OutNodeToIndex)
{
	if (!Node || !Node->GetParent() || IsSkeletonNode(Node))
	{
		return -1;
	}

	auto Existing = OutNodeToIndex.find(Node);
	if (Existing != OutNodeToIndex.end())
	{
		return Existing->second;
	}

	FBone Bone;
	Bone.Name = Node->GetName();
	Bone.ParentIndex = -1;

	FbxNode* Parent = Node->GetParent();
	while (Parent)
	{
		auto It = OutNodeToIndex.find(Parent);
		if (It != OutNodeToIndex.end())
		{
			Bone.ParentIndex = It->second;
			break;
		}

		Parent = Parent->GetParent();
	}

	FMatrix GlobalMatrix = ConvertFbxMatrix(Node->EvaluateGlobalTransform());

	Bone.LocalMatrix = ConvertFbxMatrix(Node->EvaluateLocalTransform());
	Bone.GlobalMatrix = GlobalMatrix;
	Bone.InverseBindPoseMatrix = GlobalMatrix.GetInverse();

	const int32 NewBoneIndex = (int32)Bones.size();
	Bones.push_back(Bone);
	OutNodeToIndex[Node] = NewBoneIndex;
	return NewBoneIndex;
}

void FEditorFbxImporter::ParseBone(FbxScene* Scene, TArray<FbxNode*>& Nodes, TMap<FbxNode*, int32>& OutNodeToIndex)
{
	Bones.clear();
	OutNodeToIndex.clear();

	FbxNode* SceneRoot = Scene ? Scene->GetRootNode() : nullptr;

	TArray<FFbxSkinClusterRef> ClusterRefs;
	CollectSkinClusters(Nodes, ClusterRefs);

	TSet<FbxNode*> CandidateNodes;
	TMap<FbxNode*, FbxAMatrix> ClusterLinkBindMatrices;
	for (const FFbxSkinClusterRef& Ref : ClusterRefs)
	{
		FbxNode* LinkNode = Ref.Cluster ? Ref.Cluster->GetLink() : nullptr;
		if (!LinkNode)
		{
			continue;
		}

		AddBoneCandidateWithAncestors(LinkNode, SceneRoot, CandidateNodes);

		if (ClusterLinkBindMatrices.find(LinkNode) == ClusterLinkBindMatrices.end())
		{
			FbxAMatrix LinkBindMatrix;
			Ref.Cluster->GetTransformLinkMatrix(LinkBindMatrix);
			ClusterLinkBindMatrices.emplace(LinkNode, LinkBindMatrix);
		}
	}

	if (CandidateNodes.empty())
	{
		for (FbxNode* Node : Nodes)
		{
			if (IsSkeletonNode(Node))
			{
				CandidateNodes.insert(Node);
			}
		}
	}

	for (FbxNode* Node : Nodes)
	{
		if (!Node || CandidateNodes.find(Node) == CandidateNodes.end())
		{
			continue;
		}

		FBone Bone;
		Bone.Name = Node->GetName();
		Bone.ParentIndex = FindNearestParentBoneIndex(Node, OutNodeToIndex);

		FMatrix GlobalBind = FMatrix::Identity;
		auto ClusterBindIt = ClusterLinkBindMatrices.find(Node);
		if (ClusterBindIt != ClusterLinkBindMatrices.end())
		{
			GlobalBind = ConvertFbxMatrix(ClusterBindIt->second);
		}
		else
		{
			FFbxBindPoseMatrix BindPoseMatrix;
			if (TryGetBindPoseMatrix(Scene, Node, BindPoseMatrix))
			{
				const FMatrix PoseMatrix = ConvertFbxMatrix(BindPoseMatrix.Matrix);
				if (BindPoseMatrix.bLocalMatrix && Bone.ParentIndex >= 0 && Bone.ParentIndex < static_cast<int32>(Bones.size()))
				{
					GlobalBind = PoseMatrix * Bones[Bone.ParentIndex].GlobalMatrix;
				}
				else
				{
					GlobalBind = PoseMatrix;
				}
			}
			else
			{
				GlobalBind = ConvertFbxMatrix(Node->EvaluateGlobalTransform());
			}
		}

		Bone.GlobalMatrix = GlobalBind;
		Bone.LocalMatrix = (Bone.ParentIndex >= 0 && Bone.ParentIndex < static_cast<int32>(Bones.size()))
			? GlobalBind * Bones[Bone.ParentIndex].GlobalMatrix.GetInverse()
			: GlobalBind;
		Bone.InverseBindPoseMatrix = GlobalBind.GetInverse();

		const int32 NewBoneIndex = static_cast<int32>(Bones.size());
		Bones.push_back(Bone);
		OutNodeToIndex[Node] = NewBoneIndex;
	}
}

static void NormalizeWeights(float* Weights, int32 Count)
{
	float TotalWeight = 0.0f;
	for (int32 i = 0; i < Count; ++i)
	{
		TotalWeight += Weights[i];
	}

	if (TotalWeight > 0.0f)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			Weights[i] /= TotalWeight;
		}
	}
}

static bool AssignTopNormalizedWeights(const TArray<FFbxWeightData>& SourceWeights, int32 FallbackBoneIndex, FVertexPNCTBW& Vertex)
{
	for (int32 WeightIndex = 0; WeightIndex < 4; ++WeightIndex)
	{
		Vertex.BoneIndices[WeightIndex] = -1;
		Vertex.BoneWeights[WeightIndex] = 0.0f;
	}

	TArray<FFbxWeightData> MergedWeights;
	for (const FFbxWeightData& SourceWeight : SourceWeights)
	{
		if (SourceWeight.BoneIndex < 0 || SourceWeight.Weight <= 0.0f)
		{
			continue;
		}

		bool bMerged = false;
		for (FFbxWeightData& MergedWeight : MergedWeights)
		{
			if (MergedWeight.BoneIndex == SourceWeight.BoneIndex)
			{
				MergedWeight.Weight += SourceWeight.Weight;
				bMerged = true;
				break;
			}
		}

		if (!bMerged)
		{
			MergedWeights.push_back(SourceWeight);
		}
	}

	const bool bUsedFallback = MergedWeights.empty() && FallbackBoneIndex >= 0;
	if (bUsedFallback)
	{
		MergedWeights.push_back({ FallbackBoneIndex, 1.0f });
	}

	std::sort(MergedWeights.begin(), MergedWeights.end(), [](const FFbxWeightData& A, const FFbxWeightData& B)
	{
		if (A.Weight == B.Weight)
		{
			return A.BoneIndex < B.BoneIndex;
		}
		return A.Weight > B.Weight;
	});

	for (int32 WeightIndex = 0; WeightIndex < static_cast<int32>(MergedWeights.size()) && WeightIndex < 4; ++WeightIndex)
	{
		Vertex.BoneIndices[WeightIndex] = MergedWeights[WeightIndex].BoneIndex;
		Vertex.BoneWeights[WeightIndex] = MergedWeights[WeightIndex].Weight;
	}

	NormalizeWeights(Vertex.BoneWeights, 4);
	return bUsedFallback;
}

void FEditorFbxImporter::ParseSkin(TArray<FbxNode*>& Nodes, TMap<FbxNode*, int32>& NodeToIndex)
{
	Vertices.clear();
	Indices.clear();
	Sections.clear();
	ImportedSkeletalMeshes.clear();
	TangentSums.clear();
	BitangentSums.clear();

	if (Bones.empty() || NodeToIndex.empty())
	{
		return;
	}

	FbxNode* SceneRoot = Nodes.empty() ? nullptr : Nodes.front();

	TMap<FbxNode*, int32> NodeOrder;
	for (int32 NodeIndex = 0; NodeIndex < static_cast<int32>(Nodes.size()); ++NodeIndex)
	{
		NodeOrder[Nodes[NodeIndex]] = NodeIndex;
	}

	TSet<FbxNode*> BoneNodeSet;
	TArray<FbxNode*> BoneIndexToNode;
	BoneIndexToNode.resize(Bones.size(), nullptr);
	for (const auto& Pair : NodeToIndex)
	{
		FbxNode* BoneNode = Pair.first;
		const int32 BoneIndex = Pair.second;
		BoneNodeSet.insert(BoneNode);
		if (BoneIndex >= 0 && BoneIndex < static_cast<int32>(BoneIndexToNode.size()))
		{
			BoneIndexToNode[BoneIndex] = BoneNode;
		}
	}

	TArray<FFbxSkinClusterRef> ClusterRefs;
	CollectSkinClusters(Nodes, ClusterRefs);

	TSet<FbxNode*> LinkNodeSet;
	for (const FFbxSkinClusterRef& Ref : ClusterRefs)
	{
		FbxNode* LinkNode = Ref.Cluster ? Ref.Cluster->GetLink() : nullptr;
		if (LinkNode && NodeToIndex.find(LinkNode) != NodeToIndex.end())
		{
			LinkNodeSet.insert(LinkNode);
		}
	}

	FbxNode* FirstRootBoneNode = FindFirstRootBoneNode(Nodes, NodeToIndex, Bones);

	struct FSkeletalMeshBuildGroup
	{
		FbxNode* RootNode = nullptr;
		FString MeshName;
		TArray<FVertexPNCTBW> Vertices;
		TArray<uint32> Indices;
		TArray<FSkeletalMeshSection> Sections;
		TArray<FVector> TangentSums;
		TArray<FVector> BitangentSums;
		TMap<int32, TArray<uint32>> SectionIndicesMap;
		TMap<FFbxSkeletalVertexKey, uint32> VertexMap;
	};

	TArray<FSkeletalMeshBuildGroup> Groups;

	auto GetNodeOrder = [&NodeOrder](FbxNode* Node) -> int32
		{
			auto It = NodeOrder.find(Node);
			return It != NodeOrder.end() ? It->second : INT_MAX;
		};

	auto AddUniqueRoot = [](TArray<FbxNode*>& Roots, FbxNode* Root)
		{
			if (!Root)
			{
				return;
			}

			for (FbxNode* ExistingRoot : Roots)
			{
				if (ExistingRoot == Root)
				{
					return;
				}
			}

			Roots.push_back(Root);
		};

	auto GetOrCreateGroup = [&Groups](FbxNode* RootNode) -> FSkeletalMeshBuildGroup*
		{
			if (!RootNode)
			{
				return nullptr;
			}

			for (FSkeletalMeshBuildGroup& Group : Groups)
			{
				if (Group.RootNode == RootNode)
				{
					return &Group;
				}
			}

			FSkeletalMeshBuildGroup NewGroup;
			NewGroup.RootNode = RootNode;
			NewGroup.MeshName = RootNode->GetName();
			Groups.push_back(std::move(NewGroup));
			return &Groups.back();
		};

	for (FbxNode* Node : Nodes)
	{
		if (!Node)
		{
			continue;
		}

		FbxMesh* Mesh = Node->GetMesh();
		if (!Mesh) continue;

		TArray<FbxSkin*> Skins;
		CollectMeshSkins(Mesh, Skins);

		TArray<FbxNode*> InfluenceRoots;
		for (FbxSkin* Skin : Skins)
		{
			const int32 ClusterCount = Skin ? Skin->GetClusterCount() : 0;
			for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
			{
				FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
				FbxNode* LinkNode = Cluster ? Cluster->GetLink() : nullptr;
				if (!LinkNode || NodeToIndex.find(LinkNode) == NodeToIndex.end())
				{
					continue;
				}

				AddUniqueRoot(InfluenceRoots, FindTopmostBoneRoot(LinkNode, BoneNodeSet, LinkNodeSet, SceneRoot));
			}
		}

		if (InfluenceRoots.empty())
		{
			const int32 NearestBoneIndex = FindNearestParentBoneIndex(Node, NodeToIndex);
			FbxNode* NearestBoneNode = (NearestBoneIndex >= 0 && NearestBoneIndex < static_cast<int32>(BoneIndexToNode.size()))
				? BoneIndexToNode[NearestBoneIndex]
				: nullptr;
			AddUniqueRoot(InfluenceRoots, FindTopmostBoneRoot(NearestBoneNode, BoneNodeSet, LinkNodeSet, SceneRoot));
		}

		if (InfluenceRoots.empty())
		{
			UE_LOG("Warning: FBX skeletal import skipped mesh with no skeleton root. Mesh=%s", Node->GetName());
			continue;
		}

		std::sort(InfluenceRoots.begin(), InfluenceRoots.end(), [&GetNodeOrder](FbxNode* A, FbxNode* B)
		{
			return GetNodeOrder(A) < GetNodeOrder(B);
		});

		FbxNode* GroupRootNode = InfluenceRoots.front();
		if (InfluenceRoots.size() > 1)
		{
			UE_LOG("Warning: FBX skeletal mesh is influenced by multiple skeleton roots. Mesh=%s Root=%s", Node->GetName(), GroupRootNode ? GroupRootNode->GetName() : "None");
		}

		FSkeletalMeshBuildGroup* Group = GetOrCreateGroup(GroupRootNode);
		if (!Group)
		{
			continue;
		}

		const FMatrix MeshBindGlobal = ResolveMeshBindGlobal(Node, Skins);
		const FMatrix NodeToGroupBind = MeshBindGlobal;

		int32 FallbackBoneIndex = FindNearestParentBoneIndex(Node, NodeToIndex);
		if (FallbackBoneIndex < 0 && GroupRootNode)
		{
			auto RootBoneIt = NodeToIndex.find(GroupRootNode);
			if (RootBoneIt != NodeToIndex.end())
			{
				FallbackBoneIndex = RootBoneIt->second;
			}
		}
		if (FallbackBoneIndex < 0 && FirstRootBoneNode)
		{
			auto FirstRootIt = NodeToIndex.find(FirstRootBoneNode);
			if (FirstRootIt != NodeToIndex.end())
			{
				FallbackBoneIndex = FirstRootIt->second;
			}
		}

		TArray<TArray<FFbxWeightData>> TempWeights(Mesh->GetControlPointsCount());

		for (FbxSkin* Skin : Skins)
		{
			const int32 ClusterCount = Skin ? Skin->GetClusterCount() : 0;
			for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
			{
				FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
				if (!Cluster)
				{
					continue;
				}

				FbxNode* LinkNode = Cluster->GetLink();
				auto BoneIt = NodeToIndex.find(LinkNode);
				if (BoneIt == NodeToIndex.end())
				{
					continue;
				}

				int32* ControlPointIndices = Cluster->GetControlPointIndices();
				double* ControlPointWeights = Cluster->GetControlPointWeights();
				const int32 NumIndices = Cluster->GetControlPointIndicesCount();
				if (!ControlPointIndices || !ControlPointWeights || NumIndices <= 0)
				{
					continue;
				}

				for (int32 WeightIndex = 0; WeightIndex < NumIndices; ++WeightIndex)
				{
					const int32 CPIndex = ControlPointIndices[WeightIndex];
					if (!IsValidControlPointIndex(Mesh, CPIndex))
					{
						continue;
					}

					const float Weight = static_cast<float>(ControlPointWeights[WeightIndex]);
					if (Weight <= 0.0f)
					{
						continue;
					}

					TempWeights[CPIndex].push_back({ BoneIt->second, Weight });
				}
			}
		}

		TArray<uint8> FallbackWeightAssigned;
		FallbackWeightAssigned.resize(Mesh->GetControlPointsCount(), 0);
		int32 FallbackWeightCount = 0;
		int32 MissingWeightCount = 0;

		FbxStringList UVSetNames;
		Mesh->GetUVSetNames(UVSetNames);
		const char* UVName = (UVSetNames.GetCount() > 0) ? UVSetNames.GetStringAt(0) : nullptr;

		TArray<int32> LocalToGlobalMaterialIndex;
		LocalToGlobalMaterialIndex.resize(Node->GetMaterialCount());

		for (int32 LocalIndex = 0; LocalIndex < Node->GetMaterialCount(); ++LocalIndex)
		{
			FbxSurfaceMaterial* Material = Node->GetMaterial(LocalIndex);

			auto It = MaterialToSlotIndex.find(Material);
			LocalToGlobalMaterialIndex[LocalIndex] = (It != MaterialToSlotIndex.end()) ? It->second : -1;
		}

		for (int32 i = 0; i < Mesh->GetPolygonCount(); ++i)
		{
			if (Mesh->GetPolygonSize(i) != 3)
			{
				continue;
			}

			int32 LocalMaterialIndex = GetMaterialIndex(Mesh, i);
			int32 GlobalMaterialIndex = -1;

			if (LocalMaterialIndex >= 0 && LocalMaterialIndex < (int32)LocalToGlobalMaterialIndex.size())
			{
				GlobalMaterialIndex = LocalToGlobalMaterialIndex[LocalMaterialIndex];
			}
			uint32 TriIndices[3] = {};
			uint32 PendingSectionIndices[3] = {};
			bool bValidTriangle = true;
			for (int32 j = 0; j < 3; ++j)
			{
				FVertexPNCTBW Vertex;
				int32 CPIndex = Mesh->GetPolygonVertex(i, j);
				if (!IsValidControlPointIndex(Mesh, CPIndex))
				{
					bValidTriangle = false;
					break;
				}

				FbxVector4 CP = Mesh->GetControlPointAt(CPIndex);
				Vertex.Position = NodeToGroupBind.TransformPositionWithW(FVector((float)CP[0], (float)CP[1], (float)CP[2]));

				const bool bHasAuthoredWeights = !TempWeights[CPIndex].empty();
				const bool bUsedFallback = AssignTopNormalizedWeights(TempWeights[CPIndex], FallbackBoneIndex, Vertex);
				if (!bHasAuthoredWeights && CPIndex < static_cast<int32>(FallbackWeightAssigned.size()) && !FallbackWeightAssigned[CPIndex])
				{
					FallbackWeightAssigned[CPIndex] = 1;
					if (bUsedFallback)
					{
						++FallbackWeightCount;
					}
					else
					{
						++MissingWeightCount;
					}

				}

				FbxVector4 Normal;
				Mesh->GetPolygonVertexNormal(i, j, Normal);
				Normal.Normalize();
				FVector N = FVector((float)Normal[0], (float)Normal[1], (float)Normal[2]);
				N = NodeToGroupBind.TransformVector(N);
				N.Normalize();

				Vertex.Normal = N;

				Vertex.Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
				Vertex.UV = FVector2(0.0f, 0.0f);

				if (UVName)
				{
					FbxVector2 UV;
					bool bUnmappedUV = false;
					const bool bSuccess = Mesh->GetPolygonVertexUV(i, j, UVName, UV, bUnmappedUV);
					if (bSuccess && !bUnmappedUV)
					{
						Vertex.UV = FVector2((float)UV[0], 1.0f - (float)UV[1]);
					}
				}

				FFbxSkeletalVertexKey Key;
				Key.MeshNode = Node;
				Key.Mesh = Mesh;
				Key.ControlPointIndex = CPIndex;
				Key.NormalX = Vertex.Normal.X;
				Key.NormalY = Vertex.Normal.Y;
				Key.NormalZ = Vertex.Normal.Z;
				Key.UVX = Vertex.UV.X;
				Key.UVY = Vertex.UV.Y;

				uint32 VertexIndex = 0;
				auto It = Group->VertexMap.find(Key);
				if (It != Group->VertexMap.end())
				{
					VertexIndex = It->second;
				}
				else
				{
					VertexIndex = static_cast<uint32>(Group->Vertices.size());
					Group->Vertices.push_back(Vertex);
					Group->TangentSums.push_back(FVector::ZeroVector);
					Group->BitangentSums.push_back(FVector::ZeroVector);
					Group->VertexMap[Key] = VertexIndex;
				}
				TriIndices[j] = VertexIndex;
				PendingSectionIndices[j] = VertexIndex;
			}

			if (!bValidTriangle)
			{
				continue;
			}

			for (uint32 VertexIndex : PendingSectionIndices)
			{
				Group->SectionIndicesMap[GlobalMaterialIndex].push_back(VertexIndex);
			}

			AccumulateSkeletalTangents(Group->Vertices, Group->TangentSums, Group->BitangentSums, TriIndices);
		}

		if (FallbackWeightCount > 0)
		{
			const FString FallbackBoneName = (FallbackBoneIndex >= 0 && FallbackBoneIndex < static_cast<int32>(Bones.size()))
				? Bones[FallbackBoneIndex].Name
				: FString("None");
			UE_LOG("Warning: FBX skeletal import assigned rigid fallback weights. Mesh=%s ControlPoints=%d Bone=%s", Node->GetName(), FallbackWeightCount, FallbackBoneName.c_str());
		}
		if (MissingWeightCount > 0)
		{
			UE_LOG("Warning: FBX skeletal import found unweighted vertices with no fallback bone. Mesh=%s ControlPoints=%d", Node->GetName(), MissingWeightCount);
		}
	}

	for (FSkeletalMeshBuildGroup& Group : Groups)
	{
		if (Group.Vertices.empty() || Group.SectionIndicesMap.empty())
		{
			continue;
		}

		BuildSkeletalTangents(Group.Vertices, Group.TangentSums, Group.BitangentSums);

		TArray<int32> SectionKeys;
		SectionKeys.reserve(Group.SectionIndicesMap.size());
		for (const auto& Pair : Group.SectionIndicesMap)
		{
			if (!Pair.second.empty())
			{
				SectionKeys.push_back(Pair.first);
			}
		}

		std::sort(SectionKeys.begin(), SectionKeys.end(), CompareMaterialSectionKey);

		uint32 CurrentBaseIndex = 0;
		for (int32 MatIndex : SectionKeys)
		{
			TArray<uint32>& SectionIndices = Group.SectionIndicesMap[MatIndex];
			if (SectionIndices.empty())
			{
				continue;
			}

			FSkeletalMeshSection Section;
			if (MatIndex >= 0 && MatIndex < static_cast<int32>(MtlInfos.size()))
			{
				Section.MaterialSlotName = MtlInfos[MatIndex].Name;
				Section.MaterialIndex = MatIndex;
			}
			else
			{
				if (MatIndex >= 0)
				{
					UE_LOG("Warning: Material index %d out of range. Assigning to Default slot.", MatIndex);
				}
				Section.MaterialSlotName = "None";
				Section.MaterialIndex = -1;
			}

			Section.FirstIndex = CurrentBaseIndex;
			Section.IndexCount = static_cast<uint32>(SectionIndices.size());
			CurrentBaseIndex += Section.IndexCount;

			Group.Indices.insert(Group.Indices.end(), SectionIndices.begin(), SectionIndices.end());
			Group.Sections.push_back(Section);
		}

		if (Group.Vertices.empty() || Group.Indices.empty())
		{
			continue;
		}

		FImportedSkeletalMesh ImportedMesh;
		ImportedMesh.MeshName = Group.MeshName.empty()
			? FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(CurrentSourcePath)).stem().wstring())
			: Group.MeshName;
		ImportedMesh.MeshBindGlobal = FMatrix::Identity;
		ImportedMesh.Vertices = std::move(Group.Vertices);
		ImportedMesh.Indices = std::move(Group.Indices);
		ImportedMesh.Sections = std::move(Group.Sections);
		ImportedSkeletalMeshes.push_back(std::move(ImportedMesh));
	}
}

void FEditorFbxImporter::ParseAnimations(FbxScene* Scene, const FSkeletonAsset& TargetSkeleton, TArray<FImportedAnimSequence>& OutSequences)
{
	OutSequences.clear();
	if (!Scene || !Scene->GetRootNode() || TargetSkeleton.Bones.empty())
	{
		return;
	}

	TArray<FbxNode*> Nodes;
	CollectNodes(Scene->GetRootNode(), 0, Nodes);

	TMap<FString, FbxNode*> NodeNameToNode;
	for (FbxNode* Node : Nodes)
	{
		if (!Node)
		{
			continue;
		}

		const FString NodeName = Node->GetName();
		if (NodeNameToNode.find(NodeName) == NodeNameToNode.end())
		{
			NodeNameToNode.emplace(NodeName, Node);
		}
	}

	TArray<FbxNode*> BoneNodes;
	BoneNodes.resize(TargetSkeleton.Bones.size(), nullptr);
	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(TargetSkeleton.Bones.size()); ++BoneIndex)
	{
		auto It = NodeNameToNode.find(TargetSkeleton.Bones[BoneIndex].Name);
		if (It != NodeNameToNode.end())
		{
			BoneNodes[BoneIndex] = It->second;
		}
	}

	const int32 StackCount = Scene->GetSrcObjectCount<FbxAnimStack>();
	for (int32 StackIndex = 0; StackIndex < StackCount; ++StackIndex)
	{
		FbxAnimStack* Stack = Scene->GetSrcObject<FbxAnimStack>(StackIndex);
		if (!Stack)
		{
			continue;
		}

		Scene->SetCurrentAnimationStack(Stack);

		FbxTimeSpan TimeSpan = Stack->GetLocalTimeSpan();
		const double StartSeconds = TimeSpan.GetStart().GetSecondDouble();
		const double StopSeconds = TimeSpan.GetStop().GetSecondDouble();
		const double DurationSeconds = std::max(0.0, StopSeconds - StartSeconds);
		const int32 SampleRate = CalculateAnimStackSampleRate(Scene, Stack);
		const int32 NumberOfKeys = std::max(1, static_cast<int32>(std::round(DurationSeconds * static_cast<double>(SampleRate))) + 1);

		FImportedAnimSequence ImportedSequence;
		const char* StackName = Stack->GetName();
		ImportedSequence.StackName = StackName && StackName[0] ? FString(StackName) : FString("Take_") + std::to_string(StackIndex + 1);
		ImportedSequence.PlayLength = static_cast<float>(DurationSeconds);
		ImportedSequence.FrameRate = static_cast<float>(SampleRate);
		ImportedSequence.NumberOfFrames = std::max(0, NumberOfKeys - 1);
		ImportedSequence.NumberOfKeys = NumberOfKeys;
		ImportedSequence.Tracks.reserve(TargetSkeleton.Bones.size());

		for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(TargetSkeleton.Bones.size()); ++BoneIndex)
		{
			const FBone& Bone = TargetSkeleton.Bones[BoneIndex];

			FBoneAnimationTrack Track;
			Track.Name = FName(Bone.Name);
			Track.BoneTreeIndex = BoneIndex;
			Track.InternalTrackData.PosKeys.reserve(NumberOfKeys);
			Track.InternalTrackData.RotKeys.reserve(NumberOfKeys);
			Track.InternalTrackData.ScaleKeys.reserve(NumberOfKeys);

			FbxNode* BoneNode = BoneIndex < static_cast<int32>(BoneNodes.size()) ? BoneNodes[BoneIndex] : nullptr;
			FbxNode* ParentBoneNode = (Bone.ParentIndex >= 0 && Bone.ParentIndex < static_cast<int32>(BoneNodes.size()))
				? BoneNodes[Bone.ParentIndex]
				: nullptr;

			for (int32 FrameIndex = 0; FrameIndex < NumberOfKeys; ++FrameIndex)
			{
				const double FrameSeconds = (FrameIndex == NumberOfKeys - 1)
					? StopSeconds
					: StartSeconds + static_cast<double>(FrameIndex) / static_cast<double>(SampleRate);

				FbxTime SampleTime;
				SampleTime.SetSecondDouble(FrameSeconds);

				FMatrix LocalMatrix = Bone.LocalMatrix;
				if (BoneNode)
				{
					const FMatrix GlobalMatrix = ConvertFbxMatrix(BoneNode->EvaluateGlobalTransform(SampleTime));
					if (Bone.ParentIndex >= 0 && Bone.ParentIndex < static_cast<int32>(TargetSkeleton.Bones.size()))
					{
						// Animation-only FBX files can omit synthetic mesh/root ancestors that exist in
						// the target skeleton. In that case raw local transform keeps the scene scale
						// on the first real bone, so derive local from the target parent's global bind.
						FMatrix ParentGlobalMatrix = TargetSkeleton.Bones[Bone.ParentIndex].GlobalMatrix;
						if (ParentBoneNode)
						{
							ParentGlobalMatrix = ConvertFbxMatrix(ParentBoneNode->EvaluateGlobalTransform(SampleTime));
						}
						LocalMatrix = GlobalMatrix * ParentGlobalMatrix.GetInverse();
					}
					else
					{
						LocalMatrix = GlobalMatrix;
					}
				}

				FTransform LocalTransform(LocalMatrix);
				FQuat Rotation = LocalTransform.Rotation.GetNormalized();
				if (!Track.InternalTrackData.RotKeys.empty() && DotQuat(Track.InternalTrackData.RotKeys.back(), Rotation) < 0.0f)
				{
					Rotation = FQuat(-Rotation.X, -Rotation.Y, -Rotation.Z, -Rotation.W);
				}

				Track.InternalTrackData.PosKeys.push_back(LocalTransform.Location);
				Track.InternalTrackData.RotKeys.push_back(Rotation);
				Track.InternalTrackData.ScaleKeys.push_back(LocalTransform.Scale);
			}

			ReduceConstantVectorKeys(Track.InternalTrackData.PosKeys);
			ReduceConstantQuatKeys(Track.InternalTrackData.RotKeys);
			ReduceConstantVectorKeys(Track.InternalTrackData.ScaleKeys);
			ImportedSequence.Tracks.push_back(std::move(Track));
		}

		OutSequences.push_back(std::move(ImportedSequence));
	}
}

void FEditorFbxImporter::GenerateTangents(uint32 TriIndices[])
{
	const FVertexPNCTBW& V0 = Vertices[TriIndices[0]];
	const FVertexPNCTBW& V1 = Vertices[TriIndices[1]];
	const FVertexPNCTBW& V2 = Vertices[TriIndices[2]];

	FVector Edge1 = V1.Position - V0.Position;
	FVector Edge2 = V2.Position - V0.Position;

	FVector2 DeltaUV1 = V1.UV - V0.UV;
	FVector2 DeltaUV2 = V2.UV - V0.UV;

	float Det = DeltaUV1.X * DeltaUV2.Y - DeltaUV1.Y * DeltaUV2.X;
	if (std::abs(Det) >= 1e-8f)
	{
		float InvDet = 1.0f / Det;

		FVector Tangent = (Edge1 * DeltaUV2.Y - Edge2 * DeltaUV1.Y) * InvDet;
		FVector Bitangent = (Edge2 * DeltaUV1.X - Edge1 * DeltaUV2.X) * InvDet;

		TangentSums[TriIndices[0]] += Tangent;
		TangentSums[TriIndices[1]] += Tangent;
		TangentSums[TriIndices[2]] += Tangent;

		BitangentSums[TriIndices[0]] += Bitangent;
		BitangentSums[TriIndices[1]] += Bitangent;
		BitangentSums[TriIndices[2]] += Bitangent;
	}
}

void FEditorFbxImporter::BuildTangentsForVertexRange(const uint32 VertexStart)
{
	for (uint32 i = VertexStart; i < (uint32)Vertices.size(); ++i)
	{
		FVector N = Vertices[i].Normal;
		FVector T = TangentSums[i];

		T = T - N * N.Dot(T);
		if (T.Length() < FMath::Epsilon)
		{
			FVector Axis = std::abs(N.Z) < 0.999f ? FVector(0.0f, 0.0f, 1.0f) : FVector(0.0f, 1.0f, 0.0f);
			T = Axis.Cross(N).Normalized();
		}
		else
		{
			T.Normalize();
		}


		FVector B = BitangentSums[i];
		float Handedness = N.Cross(T).Dot(B) < 0.0f ? -1.0f : 1.0f;

		Vertices[i].Tangent = FVector4(T, Handedness);
	}
}

int32 FEditorFbxImporter::GetMaterialIndex(FbxMesh* Mesh, int32 PolygonIndex)
{
	FbxLayerElementMaterial* LayerElementMaterial = Mesh->GetElementMaterial();
	if (!LayerElementMaterial) return -1;

	FbxLayerElementArrayTemplate<int32>& MaterialIndices = LayerElementMaterial->GetIndexArray();

	switch (LayerElementMaterial->GetMappingMode())
	{
	case FbxLayerElement::eAllSame: return MaterialIndices[0];
	case FbxLayerElement::eByPolygon: return MaterialIndices[PolygonIndex];
	}

	return 0;
}

int32 FEditorFbxImporter::FindNearestParentBoneIndex(FbxNode* Node, const TMap<FbxNode*, int32>& NodeToIndex)
{
	FbxNode* Parent = Node ? Node->GetParent() : nullptr;

	while (Parent)
	{
		auto It = NodeToIndex.find(Parent);
		if (It != NodeToIndex.end())
		{
			return It->second;
		}

		Parent = Parent->GetParent();
	}

	return -1;
}

int32 FEditorFbxImporter::FindBoneIndexByName(const FSkeletonAsset& SkeletonAsset, const FString& BoneName)
{
	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(SkeletonAsset.Bones.size()); ++BoneIndex)
	{
		if (SkeletonAsset.Bones[BoneIndex].Name == BoneName)
		{
			return BoneIndex;
		}
	}

	return -1;
}

void FEditorFbxImporter::TriangulateScene(FbxScene* Scene)
{
	if (!Scene)
	{
		return;
	}

	FbxGeometryConverter Converter(Scene->GetFbxManager());
	Converter.RemoveBadPolygonsFromMeshes(Scene);

	FbxNode* RootNode = Scene->GetRootNode();
	if (!RootNode)
	{
		return;
	}

	TArray<FbxNode*> Nodes;
	CollectNodes(RootNode, 0, Nodes);

	TArray<FbxNodeAttribute*> MeshAttributes;
	for (FbxNode* Node : Nodes)
	{
		if (!Node)
		{
			continue;
		}

		const int32 AttributeCount = Node->GetNodeAttributeCount();
		for (int32 AttributeIndex = 0; AttributeIndex < AttributeCount; ++AttributeIndex)
		{
			FbxNodeAttribute* Attribute = Node->GetNodeAttributeByIndex(AttributeIndex);
			if (!Attribute || Attribute->GetAttributeType() != FbxNodeAttribute::eMesh)
			{
				continue;   
			}

			if (std::find(MeshAttributes.begin(), MeshAttributes.end(), Attribute) == MeshAttributes.end())
			{
				MeshAttributes.push_back(Attribute);
			}
		}
	}

	for (FbxNodeAttribute* Attribute : MeshAttributes)
	{
		FbxMesh* Mesh = static_cast<FbxMesh*>(Attribute);
		if (Mesh->IsTriangleMesh())
		{
			continue;
		}

		if (!Converter.Triangulate(Attribute, true))
		{
			UE_LOG("Warning: FBX triangulation skipped invalid mesh attribute. Name=%s", Attribute->GetName());
		}
	}
}

FString FEditorFbxImporter::ConvertToMat(const FMaterialInfo* MaterialInfo)
{
	const FString SourcePath = CurrentSourcePath.empty() ? FString("Asset") : CurrentSourcePath;
	FString MatPath = BuildAdjacentMaterialPath(SourcePath, MaterialInfo->Name);

	std::filesystem::create_directories(ResolveProjectPath(MatPath).parent_path());

	json::JSON JsonData;
	JsonData["PathFileName"] = MatPath;
	JsonData["Origin"] = "FbxImport";
	JsonData["ShaderPath"] = "Shaders/Geometry/UberLit.hlsl";
	JsonData["RenderPass"] = "Opaque";

	if (!MaterialInfo->TexturePath.empty())
	{
		FString TexturePath = ResolveImportedTexturePath(SourcePath, MaterialInfo->TexturePath);
		JsonData["Textures"]["DiffuseTexture"] = TexturePath;

		JsonData["Parameters"]["SectionColor"][0] = 1.0f;
		JsonData["Parameters"]["SectionColor"][1] = 1.0f;
		JsonData["Parameters"]["SectionColor"][2] = 1.0f;
		JsonData["Parameters"]["SectionColor"][3] = 1.0f;
	}
	else
	{
		JsonData["Parameters"]["SectionColor"][0] = MaterialInfo->DiffuseColor.X;
		JsonData["Parameters"]["SectionColor"][1] = MaterialInfo->DiffuseColor.Y;
		JsonData["Parameters"]["SectionColor"][2] = MaterialInfo->DiffuseColor.Z;
		JsonData["Parameters"]["SectionColor"][3] = 1.0f;
	}

	if (!MaterialInfo->NormalTexturePath.empty())
	{
		JsonData["Textures"]["NormalTexture"] = ResolveImportedTexturePath(SourcePath, MaterialInfo->NormalTexturePath);
		JsonData["Parameters"]["HasNormalMap"] = 1.0f;
	}
	else
	{
		JsonData["Parameters"]["HasNormalMap"] = 0.0f;
	}

	std::ofstream File(ResolveProjectPath(MatPath));
	File << JsonData.dump();

	return MatPath;
}
