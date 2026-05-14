#include "Editor/Importer/Fbx/FbxSceneDocument.h"

#include "Core/Logger.h"

#include <fbxsdk.h>

FFbxSceneDocument::~FFbxSceneDocument()
{
	Reset();
}

bool FFbxSceneDocument::Load(const FString& InSourcePath)
{
	Reset();
	SourcePath = InSourcePath;

	if (!CreateFbxSdkContext())
	{
		UE_LOG("[FbxSceneDocument] Failed to create FBX SDK context: %s", SourcePath.c_str());
		return false;
	}

	if (!ImportFbxScene())
	{
		Reset();
		return false;
	}

	ConvertSceneToEngineSpace();

	if (!TriangulateFbxScene())
	{
		Reset();
		return false;
	}

	return true;
}

void FFbxSceneDocument::Reset()
{
	if (Manager != nullptr)
	{
		Manager->Destroy();
	}

	SourcePath.clear();
	Manager = nullptr;
	IOSettings = nullptr;
	Scene = nullptr;
}

FbxNode* FFbxSceneDocument::GetRootNode() const
{
	return Scene ? Scene->GetRootNode() : nullptr;
}

bool FFbxSceneDocument::CreateFbxSdkContext()
{
	Manager = FbxManager::Create();
	if (Manager == nullptr)
	{
		return false;
	}

	IOSettings = FbxIOSettings::Create(Manager, IOSROOT);
	if (IOSettings == nullptr)
	{
		Reset();
		return false;
	}

	Manager->SetIOSettings(IOSettings);
	Scene = FbxScene::Create(Manager, "FbxImportScene");
	if (Scene == nullptr)
	{
		Reset();
		return false;
	}

	return true;
}

bool FFbxSceneDocument::ImportFbxScene()
{
	if (Manager == nullptr || Scene == nullptr)
	{
		return false;
	}

	FbxImporter* Importer = FbxImporter::Create(Manager, "FbxTempImporter");
	if (Importer == nullptr)
	{
		return false;
	}

	const bool bInitialized = Importer->Initialize(SourcePath.c_str(), -1, Manager->GetIOSettings());
	if (!bInitialized)
	{
		const char* ErrorString = Importer->GetStatus().GetErrorString();
		UE_LOG("[FbxSceneDocument] FBX initialize failed for %s: %s",
			SourcePath.c_str(),
			ErrorString ? ErrorString : "Unknown FBX error");
		Importer->Destroy();
		return false;
	}

	const bool bImported = Importer->Import(Scene);
	if (!bImported)
	{
		const char* ErrorString = Importer->GetStatus().GetErrorString();
		UE_LOG("[FbxSceneDocument] FBX import failed for %s: %s",
			SourcePath.c_str(),
			ErrorString ? ErrorString : "Unknown FBX error");
		Importer->Destroy();
		return false;
	}

	Importer->Destroy();
	return true;
}

void FFbxSceneDocument::ConvertSceneToEngineSpace()
{
	if (Scene == nullptr)
	{
		return;
	}

	FbxAxisSystem EngineAxisSystem;
	if (!FbxAxisSystem::ParseAxisSystem("yzx", EngineAxisSystem))
	{
		UE_LOG("[FbxSceneDocument] FBX engine axis system parse failed. Path: %s", SourcePath.c_str());
		return;
	}

	const FbxAxisSystem CurrentAxisSystem = Scene->GetGlobalSettings().GetAxisSystem();
	if (CurrentAxisSystem != EngineAxisSystem)
	{
		EngineAxisSystem.DeepConvertScene(Scene);
		UE_LOG("[FbxSceneDocument] FBX axis system converted. Path: %s", SourcePath.c_str());
	}

	const FbxSystemUnit EngineUnitSystem = FbxSystemUnit::m;
	const FbxSystemUnit CurrentUnitSystem = Scene->GetGlobalSettings().GetSystemUnit();
	if (CurrentUnitSystem != EngineUnitSystem)
	{
		EngineUnitSystem.ConvertScene(Scene);
		UE_LOG("[FbxSceneDocument] FBX unit system converted to meter. Path: %s", SourcePath.c_str());
	}
}

bool FFbxSceneDocument::TriangulateFbxScene()
{
	if (Manager == nullptr || Scene == nullptr)
	{
		return false;
	}

	FbxGeometryConverter GeometryConverter(Manager);
	const bool bConverted = GeometryConverter.Triangulate(Scene, true);
	if (!bConverted)
	{
		UE_LOG("[FbxSceneDocument] FBX triangulation failed. Path: %s", SourcePath.c_str());
		return false;
	}

	return true;
}
