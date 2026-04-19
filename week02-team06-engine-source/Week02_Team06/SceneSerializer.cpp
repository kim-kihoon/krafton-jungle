#include "pch.h"
#include "SceneSerializer.h"
#include "json.hpp"
#include <fstream>
#include "World.h"
#include "PrimitiveComponent.h"
#include "Actor.h"

using ordered_json = nlohmann::ordered_json;
using json = nlohmann::json;


static json VectorToJson(const FVector& v)
{
	return json::array({ v.X, v.Y, v.Z });
}

static FVector JsonToVector(const json& j)
{
	return FVector(j[0], j[1], j[2]);
}

FString USceneSerializer::Serialize(SceneSaveData& sceneInfo)
{
	ordered_json root;

	root["Version"] = sceneInfo.Version;
	root["NextUUID"] = sceneInfo.NextUUID;

	json primitive = json::object();

	for (size_t i = 0; i < sceneInfo.Primitives.Size(); ++i)
	{
		UPrimitiveComponent* RootComponent = nullptr;

		if (sceneInfo.Primitives[i]->RootComponent->IsA(UPrimitiveComponent::StaticClass()))
		{
			RootComponent = static_cast<UPrimitiveComponent*>(sceneInfo.Primitives[i]->RootComponent);
		}
	
		if (!RootComponent)
			continue;

		json objData = json::object();

		objData["Location"] = VectorToJson(RootComponent->GetPosition());
		objData["Rotation"] = VectorToJson(RootComponent->GetRotation());
		objData["Scale"] = VectorToJson(RootComponent->GetScale());
		objData["Type"] = RootComponent->GetType();
		
		primitive[std::to_string(sceneInfo.Primitives[i]->UUID)] = objData;
	}

	root["Primitives"] = primitive;

	return root.dump(4);
}

bool USceneSerializer::SaveScene(const FString& sceneName, UWorld* World)
{
	SceneSaveData sceneData;

	sceneData.Name = sceneName;
	sceneData.NextUUID = UEngineStatics::NextUUID;

	sceneData.Primitives = World->GetSerializableActors();

	FString fullPath = GetSaveDirectory() + sceneName + ".Scene";
	std::filesystem::create_directories(GetSaveDirectory());

	FString root = Serialize(sceneData);

	std::ofstream file(fullPath);
	if (!file.is_open()) return false;
	
	file << root;
	file.close();
	return true;
}

bool USceneSerializer::LoadScene(const FString& sceneName, UWorld* World)
{
	UEngineStatics::bIsLoading = true;

	FString fullPath = GetSaveDirectory() + sceneName + ".Scene";

	std::ifstream file(fullPath);
	if (!file.is_open()) return false;

	std::stringstream buffer;
	buffer << file.rdbuf();

	json root = json::parse(buffer.str());
	World->ClearScene();

	UEngineStatics::NextUUID = root["NextUUID"];

	for (auto& item : root["Primitives"].items())
	{
		json objData = item.value();

		FSpawnParameters params;
		params.bOverrideUUID = true;
		params.Count = 1;
		params.UUID = std::stoul(item.key());
		params.Location = JsonToVector(objData["Location"]);
		params.Rotation = JsonToVector(objData["Rotation"]);
		params.Scale = JsonToVector(objData["Scale"]);
		params.PrimitiveType = objData["Type"];

		World->SpawnActorFromEditor(params);
	}

	UEngineStatics::bIsLoading = false;
	return true;
}

bool USceneSerializer::SaveEditorConfig()
{
	json config;
	config["NextUUID"] = UEngineStatics::NextUUID;

	std::ofstream file("Content/Editor.config");
	if (!file.is_open()) return false;

	file << config.dump(4);
	file.close();
	return true;
}

bool USceneSerializer::LoadEditorConfig()
{
	std::ifstream file("Content/Editor.config");
	if (!file.is_open()) return false;

	std::stringstream buffer;
	buffer << file.rdbuf();

	json config = json::parse(buffer.str());
	UEngineStatics::NextUUID = config["NextUUID"];
	return true;
}
