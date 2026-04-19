#pragma once

#include "Component/CameraComponent.h"
#include "EngineTypes.h"
#include "json.h"

struct EditorConfig
{
	float CameraMoveSpeed = 10.0f;
	float CameraRotationSpeed = 0.5f;
	float CameraFov = 90.0f;
	bool CameraOrthographic = false;
	float GridSize = 10.0f;
	uint32 ViewMode = static_cast<uint32>(EViewMode::Lit);
	uint32 ShowFlags = static_cast<uint32>(EShowFlag::All) & ~static_cast<uint32>(EShowFlag::Debug) & ~static_cast<uint32>(EShowFlag::UUID);

	std::string ToJson()
	{
		return ToJson(this);
	}

	static FString ToJson(EditorConfig* config)
	{
		json::JSON json;
		json["CameraMoveSpeed"] = config->CameraMoveSpeed;
		json["CameraRotationSpeed"] = config->CameraRotationSpeed;
		json["CameraFov"] = config->CameraFov;
		json["CameraOrthographic"] = config->CameraOrthographic;
		json["GridSize"] = config->GridSize;
		json["ViewMode"] = config->ViewMode;
		json["ShowFlags"] = config->ShowFlags;

		return json.dump();
	}

	static EditorConfig* FromJson(const FString& jsonStr)
	{
		json::JSON obj;
		obj = json::JSON::Load(jsonStr);
		
		EditorConfig* config = new EditorConfig();
		if (!obj["CameraMoveSpeed"].IsNull()) config->CameraMoveSpeed = obj["CameraMoveSpeed"].ToFloat();
		if (!obj["CameraRotationSpeed"].IsNull()) config->CameraRotationSpeed = obj["CameraRotationSpeed"].ToFloat();
		if (!obj["CameraFov"].IsNull()) config->CameraFov = obj["CameraFov"].ToFloat();
		if (!obj["CameraOrthographic"].IsNull()) config->CameraOrthographic = obj["CameraOrthographic"].ToBool();
		if (!obj["GridSize"].IsNull()) config->GridSize = obj["GridSize"].ToFloat();
		if (!obj["ViewMode"].IsNull()) config->ViewMode = obj["ViewMode"].ToInt();
		if (!obj["ShowFlags"].IsNull()) config->ShowFlags = obj["ShowFlags"].ToInt();

		return config;
	}
};