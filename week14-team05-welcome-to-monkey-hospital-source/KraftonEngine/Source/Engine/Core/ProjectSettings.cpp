#include "Core/ProjectSettings.h"
#include "SimpleJSON/json.hpp"

#include <algorithm>
#include <fstream>
#include <filesystem>
#include <cmath>

namespace PSKey
{
	constexpr const char* Shadow = "Shadow";
	constexpr const char* bShadows = "bShadows";
	constexpr const char* CSMResolution = "CSMResolution";
	constexpr const char* SpotAtlasResolution = "SpotAtlasResolution";
	constexpr const char* PointAtlasResolution = "PointAtlasResolution";
	constexpr const char* MaxSpotAtlasPages = "MaxSpotAtlasPages";
	constexpr const char* MaxPointAtlasPages = "MaxPointAtlasPages";

	constexpr const char* PhysicsSection             = "Physics";
    constexpr const char* FixedTimeStep              = "FixedTimeStep";
    constexpr const char* MaxSimulationSubstepDeltaTime = "MaxSimulationSubstepDeltaTime";
    constexpr const char* MaxFrameDeltaTime          = "MaxFrameDeltaTime";
    constexpr const char* MaxSubsteps                = "MaxSubsteps";
    constexpr const char* WorkerThreadCount          = "WorkerThreadCount";
    constexpr const char* bEnableCCD                 = "bEnableCCD";
    constexpr const char* bEnablePCM                 = "bEnablePCM";
    constexpr const char* bEnableActiveActors        = "bEnableActiveActors";
    constexpr const char* bRequireSceneReadWriteLock = "bRequireSceneReadWriteLock";
    constexpr const char* bAsyncPhysics              = "bAsyncPhysics";
    constexpr const char* bDispatchCollisionEvents   = "bDispatchCollisionEvents";
    constexpr const char* bDispatchTriggerEvents     = "bDispatchTriggerEvents";
    constexpr const char* bBuildDebugSnapshot        = "bBuildDebugSnapshot";

	constexpr const char* GameSection = "Game";
	constexpr const char* StartLevelName = "StartLevelName";
	constexpr const char* GameModeClassName = "GameModeClassName";

	constexpr const char* InputSection = "Input";
	constexpr const char* Actions = "Actions";
	constexpr const char* Axes = "Axes";
	constexpr const char* Name = "Name";
	constexpr const char* Key = "Key";
	constexpr const char* Scale = "Scale";
}

namespace
{
	void AddActionMapping(FInputProjectOption& Input, const FString& Name, const FString& Key)
	{
		Input.ActionMappings.push_back(FInputBindingSetting{ Name, Key, 1.0f });
	}

	void AddAxisMapping(FInputProjectOption& Input, const FString& Name, const FString& Key, float Scale)
	{
		Input.AxisMappings.push_back(FInputBindingSetting{ Name, Key, Scale });
	}

	json::JSON WriteInputBinding(const FInputBindingSetting& Binding, bool bWriteScale)
	{
		json::JSON Obj = json::Object();
		Obj[PSKey::Name] = Binding.Name;
		Obj[PSKey::Key] = Binding.Key;
		if (bWriteScale)
		{
			Obj[PSKey::Scale] = Binding.Scale;
		}
		return Obj;
	}

	void ReadInputBindings(json::JSON& Array, TArray<FInputBindingSetting>& OutBindings, bool bReadScale)
	{
		if (Array.JSONType() != json::JSON::Class::Array)
		{
			return;
		}

		OutBindings.clear();
		for (auto& Entry : Array.ArrayRange())
		{
			if (Entry.JSONType() != json::JSON::Class::Object || !Entry.hasKey(PSKey::Name) || !Entry.hasKey(PSKey::Key))
			{
				continue;
			}

			FInputBindingSetting Binding;
			Binding.Name = Entry[PSKey::Name].ToString();
			Binding.Key = Entry[PSKey::Key].ToString();
			Binding.Scale = bReadScale && Entry.hasKey(PSKey::Scale) ? static_cast<float>(Entry[PSKey::Scale].ToFloat()) : 1.0f;
			if (!Binding.Name.empty() && !Binding.Key.empty())
			{
				OutBindings.push_back(Binding);
			}
		}
	}
}

void FProjectSettings::SaveToFile(const FString& Path) const
{
	using namespace json;

	JSON Root = Object();

	JSON ShadowObj = Object();
	ShadowObj[PSKey::bShadows] = Shadow.bEnabled;
	ShadowObj[PSKey::CSMResolution] = static_cast<int>(Shadow.CSMResolution);
	ShadowObj[PSKey::SpotAtlasResolution] = static_cast<int>(Shadow.SpotAtlasResolution);
	ShadowObj[PSKey::PointAtlasResolution] = static_cast<int>(Shadow.PointAtlasResolution);
	ShadowObj[PSKey::MaxSpotAtlasPages] = static_cast<int>(Shadow.MaxSpotAtlasPages);
	ShadowObj[PSKey::MaxPointAtlasPages] = static_cast<int>(Shadow.MaxPointAtlasPages);
	Root[PSKey::Shadow] = ShadowObj;

	JSON PhysObj                               = Object();
    PhysObj[PSKey::FixedTimeStep]              = Physics.FixedTimeStep;
    PhysObj[PSKey::MaxSimulationSubstepDeltaTime] = Physics.MaxSimulationSubstepDeltaTime;
    PhysObj[PSKey::MaxFrameDeltaTime]          = Physics.MaxFrameDeltaTime;
    PhysObj[PSKey::MaxSubsteps]                = Physics.MaxSubsteps;
    PhysObj[PSKey::WorkerThreadCount]          = Physics.WorkerThreadCount;
    PhysObj[PSKey::bEnableCCD]                 = Physics.bEnableCCD;
    PhysObj[PSKey::bEnablePCM]                 = Physics.bEnablePCM;
    PhysObj[PSKey::bEnableActiveActors]        = Physics.bEnableActiveActors;
    PhysObj[PSKey::bRequireSceneReadWriteLock] = Physics.bRequireSceneReadWriteLock;
    PhysObj[PSKey::bAsyncPhysics]              = Physics.bAsyncPhysics;
    PhysObj[PSKey::bDispatchCollisionEvents]   = Physics.bDispatchCollisionEvents;
    PhysObj[PSKey::bDispatchTriggerEvents]     = Physics.bDispatchTriggerEvents;
    PhysObj[PSKey::bBuildDebugSnapshot]        = Physics.bBuildDebugSnapshot;
	Root[PSKey::PhysicsSection]                = PhysObj;

	JSON GameObj = Object();
	GameObj[PSKey::StartLevelName] = Game.StartLevelName;
	GameObj[PSKey::GameModeClassName] = Game.GameModeClassName;
	Root[PSKey::GameSection] = GameObj;

	JSON InputObj = Object();
	JSON Actions = Array();
	JSON Axes = Array();
	for (const FInputBindingSetting& Binding : Input.ActionMappings)
	{
		Actions.append(WriteInputBinding(Binding, false));
	}
	for (const FInputBindingSetting& Binding : Input.AxisMappings)
	{
		Axes.append(WriteInputBinding(Binding, true));
	}
	InputObj[PSKey::Actions] = Actions;
	InputObj[PSKey::Axes] = Axes;
	Root[PSKey::InputSection] = InputObj;

	std::filesystem::path FilePath(FPaths::ToWide(Path));
	if (FilePath.has_parent_path())
		std::filesystem::create_directories(FilePath.parent_path());

	std::ofstream File(FilePath);
	if (File.is_open())
		File << Root;
}

void FProjectSettings::LoadFromFile(const FString& Path)
{
	using namespace json;

	EnsureDefaultInputMappings();

	std::ifstream File(std::filesystem::path(FPaths::ToWide(Path)));
	if (!File.is_open())
		return;

	FString Content((std::istreambuf_iterator<char>(File)),
		std::istreambuf_iterator<char>());

	JSON Root = JSON::Load(Content);

	if (Root.hasKey(PSKey::PhysicsSection))
	{
		JSON P = Root[PSKey::PhysicsSection];
        if (P.hasKey(PSKey::FixedTimeStep))
        {
            float v               = static_cast<float>(P[PSKey::FixedTimeStep].ToFloat());
            Physics.FixedTimeStep = (std::max)(1.0f / 240.0f, (std::min)(v, 1.0f / 15.0f));
        }
        if (P.hasKey(PSKey::MaxSimulationSubstepDeltaTime))
        {
            float v = static_cast<float>(P[PSKey::MaxSimulationSubstepDeltaTime].ToFloat());
            Physics.MaxSimulationSubstepDeltaTime = (std::max)(1.0f / 240.0f, (std::min)(v, Physics.FixedTimeStep));
        }
        else
        {
            Physics.MaxSimulationSubstepDeltaTime = (std::min)(Physics.MaxSimulationSubstepDeltaTime, Physics.FixedTimeStep);
        }
        if (P.hasKey(PSKey::MaxFrameDeltaTime))
        {
            float v                   = static_cast<float>(P[PSKey::MaxFrameDeltaTime].ToFloat());
            Physics.MaxFrameDeltaTime = (std::max)(Physics.FixedTimeStep, (std::min)(v, 1.0f));
        }
        if (P.hasKey(PSKey::MaxSubsteps))
        {
            int v               = P[PSKey::MaxSubsteps].ToInt();
            Physics.MaxSubsteps = (std::max)(1, (std::min)(v, 32));
        }
        if (P.hasKey(PSKey::WorkerThreadCount))
        {
            int v                     = P[PSKey::WorkerThreadCount].ToInt();
            Physics.WorkerThreadCount = (std::max)(0, (std::min)(v, 32));
        }
        if (P.hasKey(PSKey::bEnableCCD)) Physics.bEnableCCD = P[PSKey::bEnableCCD].ToBool();
        if (P.hasKey(PSKey::bEnablePCM)) Physics.bEnablePCM = P[PSKey::bEnablePCM].ToBool();
        if (P.hasKey(PSKey::bEnableActiveActors)) Physics.bEnableActiveActors = P[PSKey::bEnableActiveActors].ToBool();
        if (P.hasKey(PSKey::bRequireSceneReadWriteLock)) Physics.bRequireSceneReadWriteLock = P[PSKey::bRequireSceneReadWriteLock].ToBool();
        if (P.hasKey(PSKey::bAsyncPhysics)) Physics.bAsyncPhysics = P[PSKey::bAsyncPhysics].ToBool();
        if (P.hasKey(PSKey::bDispatchCollisionEvents)) Physics.bDispatchCollisionEvents = P[PSKey::bDispatchCollisionEvents].ToBool();
        if (P.hasKey(PSKey::bDispatchTriggerEvents)) Physics.bDispatchTriggerEvents = P[PSKey::bDispatchTriggerEvents].ToBool();
        if (P.hasKey(PSKey::bBuildDebugSnapshot)) Physics.bBuildDebugSnapshot = P[PSKey::bBuildDebugSnapshot].ToBool();

        const int RequiredSubsteps = (Physics.MaxSimulationSubstepDeltaTime > 0.0f)
            ? static_cast<int>(std::ceil(Physics.FixedTimeStep / Physics.MaxSimulationSubstepDeltaTime - 1.e-6f))
            : 1;
        Physics.MaxSubsteps = (std::max)(Physics.MaxSubsteps, (std::max)(1, RequiredSubsteps));
	}

	if (Root.hasKey(PSKey::GameSection))
	{
		JSON G = Root[PSKey::GameSection];
		if (G.hasKey(PSKey::StartLevelName))
			Game.StartLevelName = G[PSKey::StartLevelName].ToString();
		if (G.hasKey(PSKey::GameModeClassName))
			Game.GameModeClassName = G[PSKey::GameModeClassName].ToString();
	}

	if (Root.hasKey(PSKey::InputSection))
	{
		JSON I = Root[PSKey::InputSection];
		if (I.hasKey(PSKey::Actions))
		{
			JSON Actions = I[PSKey::Actions];
			ReadInputBindings(Actions, Input.ActionMappings, false);
		}
		if (I.hasKey(PSKey::Axes))
		{
			JSON Axes = I[PSKey::Axes];
			ReadInputBindings(Axes, Input.AxisMappings, true);
		}
		EnsureDefaultInputMappings();
	}

	if (Root.hasKey(PSKey::Shadow))
	{
		JSON S = Root[PSKey::Shadow];
		if (S.hasKey(PSKey::bShadows))
			Shadow.bEnabled = S[PSKey::bShadows].ToBool();
		if (S.hasKey(PSKey::CSMResolution))
		{
			int v = S[PSKey::CSMResolution].ToInt();
			Shadow.CSMResolution = static_cast<uint32>((std::max)(64, (std::min)(v, 8192)));
		}
		if (S.hasKey(PSKey::SpotAtlasResolution))
		{
			int v = S[PSKey::SpotAtlasResolution].ToInt();
			Shadow.SpotAtlasResolution = static_cast<uint32>((std::max)(64, (std::min)(v, 8192)));
		}
		if (S.hasKey(PSKey::PointAtlasResolution))
		{
			int v = S[PSKey::PointAtlasResolution].ToInt();
			Shadow.PointAtlasResolution = static_cast<uint32>((std::max)(64, (std::min)(v, 8192)));
		}
		if (S.hasKey(PSKey::MaxSpotAtlasPages))
		{
			int v = S[PSKey::MaxSpotAtlasPages].ToInt();
			Shadow.MaxSpotAtlasPages = static_cast<uint32>(v > 1 ? v : 1);
		}
		if (S.hasKey(PSKey::MaxPointAtlasPages))
		{
			int v = S[PSKey::MaxPointAtlasPages].ToInt();
			Shadow.MaxPointAtlasPages = static_cast<uint32>(v > 1 ? v : 1);
		}
	}
}

void FProjectSettings::EnsureDefaultInputMappings()
{
	if (!Input.ActionMappings.empty() || !Input.AxisMappings.empty())
	{
		return;
	}

	AddAxisMapping(Input, "MoveForward", "W", 1.0f);
	AddAxisMapping(Input, "MoveForward", "S", -1.0f);
	AddAxisMapping(Input, "MoveForward", "Gamepad_LeftStickY", -1.0f);
	AddAxisMapping(Input, "MoveRight", "D", 1.0f);
	AddAxisMapping(Input, "MoveRight", "A", -1.0f);
	AddAxisMapping(Input, "MoveRight", "Gamepad_LeftStickX", 1.0f);
	AddAxisMapping(Input, "Turn", "MouseX", 0.1f);
	AddAxisMapping(Input, "Turn", "Gamepad_RightStickX", 2.0f);
	AddAxisMapping(Input, "LookUp", "MouseY", 0.1f);
	AddAxisMapping(Input, "LookUp", "Gamepad_RightStickY", 2.0f);
	AddAxisMapping(Input, "VehicleThrottle", "W", 1.0f);
	AddAxisMapping(Input, "VehicleThrottle", "Gamepad_RightTrigger", 1.0f);
	AddAxisMapping(Input, "VehicleBrake", "S", 1.0f);
	AddAxisMapping(Input, "VehicleBrake", "Gamepad_LeftTrigger", 1.0f);
	AddAxisMapping(Input, "VehicleSteering", "A", -1.0f);
	AddAxisMapping(Input, "VehicleSteering", "D", 1.0f);
	AddAxisMapping(Input, "VehicleSteering", "Gamepad_LeftStickX", 1.0f);

	AddActionMapping(Input, "Jump", "Space");
	AddActionMapping(Input, "Jump", "Gamepad_FaceDown");
	AddActionMapping(Input, "Interact", "E");
	AddActionMapping(Input, "Interact", "Gamepad_FaceRight");
	AddActionMapping(Input, "Aim", "RightMouseButton");
	AddActionMapping(Input, "Aim", "Gamepad_LeftShoulder");
	AddActionMapping(Input, "DebugAnomalyOutline", "L");
	AddActionMapping(Input, "DebugAnomalyOutline", "Gamepad_LeftTrigger");
	AddActionMapping(Input, "Fire", "LeftMouseButton");
	AddActionMapping(Input, "Fire", "Gamepad_RightShoulder");
	AddActionMapping(Input, "VehicleHandbrake", "Space");
	AddActionMapping(Input, "VehicleHandbrake", "Gamepad_FaceDown");
}
