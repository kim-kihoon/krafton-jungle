#include "Mesh/Importer/Fbx/FbxSceneLoader.h"
#include "Platform/Paths.h"
#include "Core/Logging/Log.h"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <system_error>

namespace
{
	namespace fs = std::filesystem;

	using FFbxClock = std::chrono::steady_clock;

	static double GetElapsedSeconds(const FFbxClock::time_point& Start)
	{
		return std::chrono::duration<double>(FFbxClock::now() - Start).count();
	}

	struct FFbxSharedSdkContext
	{
		FbxManager* Manager = nullptr;
		FbxIOSettings* IoSettings = nullptr;

		FFbxSharedSdkContext()
		{
			Manager = FbxManager::Create();
			if (!Manager)
			{
				return;
			}

			IoSettings = FbxIOSettings::Create(Manager, IOSROOT);
			if (!IoSettings)
			{
				Manager->Destroy();
				Manager = nullptr;
				return;
			}

			Manager->SetIOSettings(IoSettings);
		}

		~FFbxSharedSdkContext()
		{
			if (Manager)
			{
				Manager->Destroy();
				Manager = nullptr;
				IoSettings = nullptr;
			}
		}

		bool IsValid() const
		{
			return Manager != nullptr && IoSettings != nullptr;
		}
	};

	static FFbxSharedSdkContext& GetSharedSdkContext()
	{
		static FFbxSharedSdkContext Context;
		return Context;
	}
}

FFbxSceneHandle::~FFbxSceneHandle()
{
	Reset();
}

void FFbxSceneHandle::Reset()
{
	if (Scene)
	{
		const auto Start = FFbxClock::now();
		Scene->Destroy(true);
		Scene = nullptr;

		UE_LOG("FBX scene cleanup timing: SceneDestroy=%.3fs", GetElapsedSeconds(Start));
	}

	// Manager is a shared process-lifetime SDK context owned by FbxSceneLoader.cpp.
	Manager = nullptr;
}

namespace
{
	static void ApplyFbxImportOptions(FbxIOSettings* IoSettings, const FFbxSceneLoadOptions& Options)
	{
		if (!IoSettings)
		{
			return;
		}

		IoSettings->SetBoolProp(IMP_FBX_MATERIAL, Options.bImportMaterials);
		IoSettings->SetBoolProp(IMP_FBX_TEXTURE, Options.bImportTextures);
		IoSettings->SetBoolProp(IMP_FBX_LINK, Options.bImportLinks);
		IoSettings->SetBoolProp(IMP_FBX_SHAPE, Options.bImportShapes);
		IoSettings->SetBoolProp(IMP_FBX_GOBO, Options.bImportGobos);
		IoSettings->SetBoolProp(IMP_FBX_ANIMATION, Options.bImportAnimations);
		IoSettings->SetBoolProp(IMP_FBX_GLOBAL_SETTINGS, Options.bImportGlobalSettings);
		IoSettings->SetBoolProp(IMP_FBX_EXTRACT_EMBEDDED_DATA, false);
		IoSettings->SetStringProp(IMP_EXTRACT_FOLDER, FbxString(""));
	}

	static fs::path ToProjectAbsolutePath(const FString& Path)
	{
		fs::path Result(FPaths::ToWide(Path));
		if (!Result.empty() && !Result.is_absolute())
		{
			Result = fs::path(FPaths::RootDir()) / Result;
		}
		return Result.lexically_normal();
	}

	static bool IsPathStrictlyInsideDirectory(const fs::path& Path, const fs::path& Directory)
	{
		const fs::path NormalPath = Path.lexically_normal();
		const fs::path NormalDirectory = Directory.lexically_normal();
		if (NormalPath == NormalDirectory)
		{
			return false;
		}

		const fs::path Relative = NormalPath.lexically_relative(NormalDirectory);
		return !Relative.empty() && !Relative.is_absolute() && Relative.native().rfind(L"..", 0) != 0;
	}

	static bool PrepareEmbeddedTextureExtractionFolder(const FFbxSceneLoadOptions& Options, FString& OutAbsoluteFolder)
	{
		OutAbsoluteFolder.clear();

		if (!Options.bExtractEmbeddedTextures || !Options.bImportTextures || Options.EmbeddedTextureScratchDirectory.empty())
		{
			return false;
		}

		const fs::path Folder = ToProjectAbsolutePath(Options.EmbeddedTextureScratchDirectory);
		if (Folder.empty())
		{
			return false;
		}

		const fs::path ScratchRoot = ToProjectAbsolutePath("Intermediate/FbxEmbeddedTextures");
		if (!IsPathStrictlyInsideDirectory(Folder, ScratchRoot))
		{
			UE_LOG(
				"FBX embedded texture extraction disabled: scratch folder must be inside '%s' but was '%s'",
				FPaths::ToUtf8(ScratchRoot.generic_wstring()).c_str(),
				FPaths::ToUtf8(Folder.generic_wstring()).c_str()
			);
			return false;
		}

		std::error_code Ec;
		fs::remove_all(Folder, Ec);
		if (Ec)
		{
			UE_LOG(
				"FBX embedded texture extraction disabled: failed to clean scratch folder '%s' (%s)",
				FPaths::ToUtf8(Folder.generic_wstring()).c_str(),
				Ec.message().c_str()
			);
			return false;
		}

		fs::create_directories(Folder, Ec);
		if (Ec)
		{
			UE_LOG(
				"FBX embedded texture extraction disabled: failed to create scratch folder '%s' (%s)",
				FPaths::ToUtf8(Folder.generic_wstring()).c_str(),
				Ec.message().c_str()
			);
			return false;
		}

		OutAbsoluteFolder = FPaths::ToUtf8(Folder.generic_wstring());
		return true;
	}

	static void ApplyEmbeddedTextureExtractionOptions(FbxIOSettings* IoSettings, const FString& AbsoluteFolder)
	{
		if (!IoSettings || AbsoluteFolder.empty())
		{
			return;
		}

		IoSettings->SetBoolProp(IMP_FBX_EXTRACT_EMBEDDED_DATA, true);
		IoSettings->SetStringProp(IMP_EXTRACT_FOLDER, FbxString(AbsoluteFolder.c_str()));
	}

	static void ApplyFbxAnimationStackSelection(FbxImporter* Importer, const FFbxSceneLoadOptions& Options)
	{
		if (!Importer || !Options.bImportAnimations || Options.SelectedAnimationStackIndices.empty())
		{
			return;
		}

		const int32 AnimStackCount = Importer->GetAnimStackCount();
		for (int32 StackIndex = 0; StackIndex < AnimStackCount; ++StackIndex)
		{
			if (FbxTakeInfo* TakeInfo = Importer->GetTakeInfo(StackIndex))
			{
				TakeInfo->mSelect = Options.SelectedAnimationStackIndices.find(StackIndex) != Options.
				SelectedAnimationStackIndices.end();
			}
		}
	}


	struct FFbxSceneBounds
	{
		FbxVector4 Min = FbxVector4(
			(std::numeric_limits<double>::max)(),
			(std::numeric_limits<double>::max)(),
			(std::numeric_limits<double>::max)()
		);
		FbxVector4 Max = FbxVector4(
			-(std::numeric_limits<double>::max)(),
			-(std::numeric_limits<double>::max)(),
			-(std::numeric_limits<double>::max)()
		);
		bool bValid = false;

		void Include(const FbxVector4& Point)
		{
			for (int Axis = 0; Axis < 3; ++Axis)
			{
				if (Point[Axis] < Min[Axis]) Min[Axis] = Point[Axis];
				if (Point[Axis] > Max[Axis]) Max[Axis] = Point[Axis];
			}
			bValid = true;
		}

		double GetMaxExtent() const
		{
			if (!bValid)
			{
				return 0.0;
			}

			const double ExtX = Max[0] - Min[0];
			const double ExtY = Max[1] - Min[1];
			const double ExtZ = Max[2] - Min[2];
			return (std::max)(ExtX, (std::max)(ExtY, ExtZ));
		}
	};

	static FbxAMatrix GetNodeGeometryTransform(FbxNode* Node)
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

	static void AccumulateRawMeshBounds(FbxNode* Node, FFbxSceneBounds& Bounds)
	{
		if (!Node)
		{
			return;
		}

		if (FbxMesh* Mesh = Node->GetMesh())
		{
			const FbxAMatrix MeshToWorld = Node->EvaluateGlobalTransform() * GetNodeGeometryTransform(Node);
			const int32 ControlPointCount = Mesh->GetControlPointsCount();
			for (int32 ControlPointIndex = 0; ControlPointIndex < ControlPointCount; ++ControlPointIndex)
			{
				Bounds.Include(MeshToWorld.MultT(Mesh->GetControlPointAt(ControlPointIndex)));
			}
		}

		for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
		{
			AccumulateRawMeshBounds(Node->GetChild(ChildIndex), Bounds);
		}
	}

	static FFbxSceneBounds ComputeRawSceneBounds(FbxScene* Scene)
	{
		FFbxSceneBounds Bounds;
		if (FbxNode* RootNode = Scene ? Scene->GetRootNode() : nullptr)
		{
			AccumulateRawMeshBounds(RootNode, Bounds);
		}
		return Bounds;
	}

	static bool IsCentimeterSystem(const FbxSystemUnit& SystemUnit)
	{
		return std::abs(SystemUnit.GetScaleFactor() - FbxSystemUnit::cm.GetScaleFactor()) <= 0.0001;
	}

	static bool ShouldSkipCentimeterToMeterConversion(FbxScene* Scene, const FFbxSceneNormalizeOptions& Options)
	{
		if (!Scene || !Options.bAutoDetectMeterAuthoredCentimeterScene)
		{
			return false;
		}

		const FbxSystemUnit SourceUnit = Scene->GetGlobalSettings().GetSystemUnit();
		if (!IsCentimeterSystem(SourceUnit))
		{
			return false;
		}

		const FFbxSceneBounds RawBounds = ComputeRawSceneBounds(Scene);
		const double MaxExtent = RawBounds.GetMaxExtent();

		return RawBounds.bValid && MaxExtent >= 2.0 && MaxExtent <= 20.0;
	}
}

bool FFbxSceneLoader::Load(const FString& SourcePath, FFbxSceneHandle& OutScene, FString* OutMessage)
{
	return Load(SourcePath, FFbxSceneLoadOptions(), OutScene, OutMessage);
}

bool FFbxSceneLoader::Load(
	const FString&              SourcePath,
	const FFbxSceneLoadOptions& Options,
	FFbxSceneHandle&            OutScene,
	FString*                    OutMessage
	)
{
	OutScene.Reset();

	FFbxSharedSdkContext& SdkContext = GetSharedSdkContext();
	if (!SdkContext.IsValid())
	{
		if (OutMessage) *OutMessage = "FBX SDK manager creation failed.";
		return false;
	}

	FbxManager* SdkManager = SdkContext.Manager;
	FbxIOSettings* IoSettings = SdkContext.IoSettings;
	ApplyFbxImportOptions(IoSettings, Options);

	FString EmbeddedTextureExtractionFolder;
	const bool bUseEmbeddedTextureExtraction = PrepareEmbeddedTextureExtractionFolder(Options, EmbeddedTextureExtractionFolder);
	if (bUseEmbeddedTextureExtraction)
	{
		ApplyEmbeddedTextureExtractionOptions(IoSettings, EmbeddedTextureExtractionFolder);
	}

	FbxScene* Scene = FbxScene::Create(SdkManager, "FBX Scene");
	FbxImporter* Importer = FbxImporter::Create(SdkManager, "");
	if (!Scene || !Importer)
	{
		if (Importer) Importer->Destroy();
		if (Scene) Scene->Destroy(true);
		if (OutMessage) *OutMessage = "FBX scene/importer creation failed.";
		return false;
	}

	const FString FullPath = FPaths::ToUtf8(FPaths::Combine(FPaths::RootDir(), FPaths::ToWide(SourcePath)));
	if (bUseEmbeddedTextureExtraction)
	{
		Importer->SetEmbeddingExtractionFolder(EmbeddedTextureExtractionFolder.c_str());
	}

	if (!Importer->Initialize(FullPath.c_str(), -1, SdkManager->GetIOSettings()))
	{
		Importer->Destroy();
		Scene->Destroy(true);
		if (OutMessage) *OutMessage = FString("FBX importer initialize failed: ") + SourcePath;
		return false;
	}

	ApplyFbxAnimationStackSelection(Importer, Options);

	if (!Importer->Import(Scene))
	{
		Importer->Destroy();
		Scene->Destroy(true);
		if (OutMessage) *OutMessage = FString("FBX scene import failed: ") + SourcePath;
		return false;
	}

	Importer->Destroy();
	OutScene.Manager = SdkManager;
	OutScene.Scene = Scene;
	return true;
}

void FFbxSceneLoader::NormalizeScene(FbxScene* Scene)
{
	NormalizeScene(Scene, FFbxSceneNormalizeOptions());
}

void FFbxSceneLoader::NormalizeScene(FbxScene* Scene, const FFbxSceneNormalizeOptions& Options)
{
	if (!Scene)
	{
		return;
	}

	if (ShouldSkipCentimeterToMeterConversion(Scene, Options))
	{
		const FFbxSceneBounds RawBounds = ComputeRawSceneBounds(Scene);
		UE_LOG(
			"FBX unit normalization: skipped cm->m conversion because source bounds look meter-authored. MaxExtent=%.3f",
			RawBounds.GetMaxExtent()
		);
	}
	else
	{
		FbxSystemUnit::m.ConvertScene(Scene);
	}

	FbxAxisSystem UnrealAxisSystem(
		FbxAxisSystem::eZAxis,
		FbxAxisSystem::eParityEven,
		FbxAxisSystem::eLeftHanded
	);
	UnrealAxisSystem.DeepConvertScene(Scene);
}

void FFbxSceneLoader::Triangulate(FbxManager* Manager, FbxScene* Scene)
{
	if (!Manager || !Scene)
	{
		return;
	}

	FbxGeometryConverter Converter(Manager);
	Converter.Triangulate(Scene, true);
}
