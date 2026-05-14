#include "Engine/Camera/Modifier/LuaCameraModifier.h"

#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Engine/Camera/Modifier/CameraShakeModifier.h"
#include "Engine/Camera/PlayerCameraManager.h"
#include "Engine/Math/Utils.h"
#include "Object/ObjectFactory.h"
#include "Scripting/LuaBindings.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <string>

DEFINE_CLASS(ULuaCameraModifier, UCameraModifier)
REGISTER_FACTORY(ULuaCameraModifier)

namespace
{
#if WITH_LUA
bool LoadLuaSourceFromFile(const FString& ScriptPath, FString& OutSource, FString& OutError)
{
	const std::filesystem::path WidePath(FPaths::ToAbsolute(FPaths::ToWide(ScriptPath)));
	std::ifstream File(WidePath, std::ios::binary);
	if (!File.is_open())
	{
		OutError = "failed to open script file.";
		return false;
	}

	OutSource.assign(std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>());
	if (File.bad())
	{
		OutError = "failed to read script file.";
		return false;
	}

	return true;
}

template <typename TSettings>
bool CallLuaModifierFunction(sol::state* LuaState, const FString& ScriptPath, const char* FunctionName, float DeltaTime, TSettings& InOutSettings, FString& OutError)
{
	if (LuaState == nullptr)
	{
		return false;
	}

	sol::protected_function Function = (*LuaState)[FunctionName];
	if (!Function.valid())
	{
		return false;
	}

	sol::protected_function_result Result = Function(std::ref(InOutSettings), DeltaTime);
	if (!Result.valid())
	{
		sol::error Error = Result;
		OutError = Error.what();
		UE_LOG("LuaCameraModifier: failed to call %s in '%s': %s", FunctionName, ScriptPath.c_str(), OutError.c_str());
		return false;
	}

	if (Result.return_count() > 0)
	{
		sol::object ReturnValue = Result.get<sol::object>(0);
		if (ReturnValue.is<bool>())
		{
			return ReturnValue.as<bool>();
		}
	}

	return true;
}

bool IsNilOrInvalid(const sol::object& Object)
{
	return !Object.valid() || Object == sol::nil;
}

bool ReadBoolField(const sol::table& Table, const char* Key, bool DefaultValue)
{
	const sol::object Value = Table[Key];
	return !IsNilOrInvalid(Value) && Value.is<bool>() ? Value.as<bool>() : DefaultValue;
}

FString ReadStringField(const sol::table& Table, const char* Key, const FString& DefaultValue)
{
	const sol::object Value = Table[Key];
	return !IsNilOrInvalid(Value) && Value.is<FString>() ? Value.as<FString>() : DefaultValue;
}

float ReadRawFloatField(const sol::table& Table, const char* Key, float DefaultValue)
{
	const sol::object Value = Table[Key];
	return !IsNilOrInvalid(Value) && Value.is<float>() ? Value.as<float>() : DefaultValue;
}

float EvaluateFloatObject(const sol::object& Value, float DefaultValue, float TimeSeconds)
{
	if (IsNilOrInvalid(Value))
	{
		return DefaultValue;
	}

	if (Value.is<float>())
	{
		return Value.as<float>();
	}

	if (!Value.is<sol::table>())
	{
		return DefaultValue;
	}

	const sol::table Table = Value.as<sol::table>();
	const float DirectValue = ReadRawFloatField(Table, "Value", DefaultValue);
	const float Base = ReadRawFloatField(Table, "Base", DirectValue);
	const float Amplitude = ReadRawFloatField(Table, "Amplitude", 0.0f);
	const float Frequency = ReadRawFloatField(Table, "Frequency", 0.0f);

	if (Amplitude != 0.0f && Frequency != 0.0f)
	{
		return Base + std::sin(TimeSeconds * Frequency) * Amplitude;
	}

	const sol::object ToObject = Table["To"];
	if (!IsNilOrInvalid(ToObject) && ToObject.is<float>())
	{
		const float From = ReadRawFloatField(Table, "From", DefaultValue);
		const float To = ToObject.as<float>();
		const float Duration = std::max(ReadRawFloatField(Table, "Duration", 0.0f), 0.0f);
		if (Duration <= 0.0f)
		{
			return To;
		}

		const bool bLoop = ReadBoolField(Table, "Loop", false);
		const bool bPingPong = ReadBoolField(Table, "PingPong", false);
		float LocalTime = TimeSeconds;
		if (bLoop)
		{
			const float Period = bPingPong ? Duration * 2.0f : Duration;
			if (Period > 0.0f)
			{
				LocalTime = std::fmod(TimeSeconds, Period);
				if (bPingPong && LocalTime > Duration)
				{
					LocalTime = Period - LocalTime;
				}
			}
		}

		const float Alpha = MathUtil::Clamp(LocalTime / Duration, 0.0f, 1.0f);
		return MathUtil::Lerp(From, To, Alpha);
	}

	return DirectValue;
}

float EvaluateFloatField(const sol::table& Table, const char* Key, float DefaultValue, float TimeSeconds)
{
	return EvaluateFloatObject(Table[Key], DefaultValue, TimeSeconds);
}

void ReadFloatArrayField(const sol::table& Table, const char* Key, float* Values, int32 Count, float TimeSeconds)
{
	const sol::object Value = Table[Key];
	if (IsNilOrInvalid(Value) || !Value.is<sol::table>() || Values == nullptr)
	{
		return;
	}

	const sol::table ArrayTable = Value.as<sol::table>();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Values[Index] = EvaluateFloatObject(ArrayTable[Index + 1], Values[Index], TimeSeconds);
	}
}

FVector4 ReadVector4Field(const sol::table& Table, const char* Key, const FVector4& DefaultValue, float TimeSeconds)
{
	const sol::object Value = Table[Key];
	if (IsNilOrInvalid(Value) || !Value.is<sol::table>())
	{
		return DefaultValue;
	}

	const sol::table VectorTable = Value.as<sol::table>();
	FVector4 Result = DefaultValue;
	Result.X = EvaluateFloatObject(VectorTable[1], EvaluateFloatField(VectorTable, "R", EvaluateFloatField(VectorTable, "X", Result.X, TimeSeconds), TimeSeconds), TimeSeconds);
	Result.Y = EvaluateFloatObject(VectorTable[2], EvaluateFloatField(VectorTable, "G", EvaluateFloatField(VectorTable, "Y", Result.Y, TimeSeconds), TimeSeconds), TimeSeconds);
	Result.Z = EvaluateFloatObject(VectorTable[3], EvaluateFloatField(VectorTable, "B", EvaluateFloatField(VectorTable, "Z", Result.Z, TimeSeconds), TimeSeconds), TimeSeconds);
	Result.W = EvaluateFloatObject(VectorTable[4], EvaluateFloatField(VectorTable, "A", EvaluateFloatField(VectorTable, "W", Result.W, TimeSeconds), TimeSeconds), TimeSeconds);
	return Result;
}

FVector ReadVectorField(const sol::table& Table, const char* Key, const FVector& DefaultValue, float TimeSeconds)
{
	const sol::object Value = Table[Key];
	if (IsNilOrInvalid(Value) || !Value.is<sol::table>())
	{
		return DefaultValue;
	}

	const sol::table VectorTable = Value.as<sol::table>();
	FVector Result = DefaultValue;
	Result.X = EvaluateFloatObject(VectorTable[1], EvaluateFloatField(VectorTable, "X", Result.X, TimeSeconds), TimeSeconds);
	Result.Y = EvaluateFloatObject(VectorTable[2], EvaluateFloatField(VectorTable, "Y", Result.Y, TimeSeconds), TimeSeconds);
	Result.Z = EvaluateFloatObject(VectorTable[3], EvaluateFloatField(VectorTable, "Z", Result.Z, TimeSeconds), TimeSeconds);
	return Result;
}

bool TryGetSection(const sol::table& Root, const char* Name, sol::table& OutSection)
{
	const sol::object Section = Root[Name];
	if (IsNilOrInvalid(Section) || !Section.is<sol::table>())
	{
		return false;
	}

	OutSection = Section.as<sol::table>();
	return true;
}

bool IsModifierType(const sol::table& Table, const char* ExpectedType)
{
	const FString Type = ReadStringField(Table, "Type", "");
	return Type == ExpectedType;
}

bool ForEachModifierEntry(const sol::table& Root, const std::function<bool(const sol::table&, int32)>& Visitor)
{
	sol::table Modifiers;
	if (!TryGetSection(Root, "Modifiers", Modifiers))
	{
		return false;
	}

	bool bAnyVisited = false;
	int32 Index = 1;
	while (true)
	{
		const sol::object EntryObject = Modifiers[Index];
		if (IsNilOrInvalid(EntryObject))
		{
			break;
		}

		if (EntryObject.is<sol::table>())
		{
			bAnyVisited = true;
			if (!Visitor(EntryObject.as<sol::table>(), Index))
			{
				break;
			}
		}

		++Index;
	}

	return bAnyVisited;
}
#endif
}

ULuaCameraModifier::~ULuaCameraModifier()
{
	UnloadScript();
}

void ULuaCameraModifier::SetScriptPath(const FString& InScriptPath)
{
	if (ScriptPath == InScriptPath)
	{
		return;
	}

	ScriptPath = InScriptPath;
	bScriptLoaded = false;
	bHasModifierDataTable = false;
	ElapsedTime = 0.0f;
	TriggeredActionKeys.clear();
	SetLastScriptError("");
}

bool ULuaCameraModifier::ReloadScript()
{
	UnloadScript();

	if (ScriptPath.empty())
	{
		SetLastScriptError("No Lua camera modifier script assigned.");
		return false;
	}

#if WITH_LUA
	const std::wstring AbsolutePath = FPaths::ToAbsolute(FPaths::ToWide(ScriptPath));
	if (!std::filesystem::exists(AbsolutePath))
	{
		SetLastScriptError("Lua camera modifier script does not exist: " + ScriptPath);
		UE_LOG("LuaCameraModifier: %s", LastScriptError.c_str());
		return false;
	}

	LuaState = std::make_unique<sol::state>();
	LuaState->open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);
	RegisterLuaBindings(*LuaState);
	LuaState->set("CameraManager", CameraOwner);

	FString ScriptSource;
	FString ReadError;
	if (!LoadLuaSourceFromFile(ScriptPath, ScriptSource, ReadError))
	{
		SetLastScriptError(ReadError);
		UE_LOG("LuaCameraModifier: failed to load '%s': %s", ScriptPath.c_str(), LastScriptError.c_str());
		LuaState.reset();
		return false;
	}

	sol::protected_function_result Result = LuaState->safe_script(ScriptSource, sol::script_pass_on_error);
	if (!Result.valid())
	{
		sol::error Error = Result;
		SetLastScriptError(Error.what());
		UE_LOG("LuaCameraModifier: failed to load '%s': %s", ScriptPath.c_str(), LastScriptError.c_str());
		LuaState.reset();
		return false;
	}
	
	// OnLoaded가 있다면 한 번 호출
	sol::protected_function OnLoaded = (*LuaState)["OnLoaded"];
	if (OnLoaded.valid())
	{
		sol::protected_function_result OnLoadedResult = OnLoaded();
		if (!OnLoadedResult.valid())
		{
			sol::error Error = OnLoadedResult;
			SetLastScriptError(Error.what());
			UE_LOG("LuaCameraModifier: failed to call OnLoaded in '%s': %s", ScriptPath.c_str(), LastScriptError.c_str());
			LuaState.reset();
			return false;
		}
	}

	bHasModifierDataTable = false;
	ElapsedTime = 0.0f;
	TriggeredActionKeys.clear();
	if (Result.return_count() > 0)
	{
		sol::object ReturnValue = Result.get<sol::object>(0);
		if (!IsNilOrInvalid(ReturnValue) && ReturnValue.is<sol::table>())
		{
			ModifierDataTable = ReturnValue.as<sol::table>();
			bHasModifierDataTable = true;
		}
	}

	if (!bHasModifierDataTable)
	{
		sol::object GlobalData = (*LuaState)["CameraModifier"];
		if (!IsNilOrInvalid(GlobalData) && GlobalData.is<sol::table>())
		{
			ModifierDataTable = GlobalData.as<sol::table>();
			bHasModifierDataTable = true;
		}
	}

	bScriptLoaded = true;
	SetLastScriptError("");
	return true;
#else
	SetLastScriptError("Lua runtime is disabled. Build with WITH_LUA=1.");
	return false;
#endif
}

void ULuaCameraModifier::UnloadScript()
{
#if WITH_LUA
	ModifierDataTable = sol::nil;
	LuaState.reset();
#endif
	bScriptLoaded = false;
	bHasModifierDataTable = false;
	ElapsedTime = 0.0f;
	TriggeredActionKeys.clear();
}

void ULuaCameraModifier::AddedToCamera(APlayerCameraManager* Camera)
{
	CameraOwner = Camera;
	if (!ScriptPath.empty())
	{
		ReloadScript();
	}
}

void ULuaCameraModifier::RemovedFromCamera(APlayerCameraManager* Camera)
{
	if (CameraOwner == Camera)
	{
		CameraOwner = nullptr;
	}
	UnloadScript();
}

bool ULuaCameraModifier::ModifyCamera(float DeltaTime, FCameraViewInfo& InOutView)
{
	ElapsedTime += DeltaTime;
	ProcessDataDrivenActions();
	const bool bAppliedData = ApplyDataDrivenCamera(DeltaTime, InOutView);
	const bool bCalledFunction = CallModifierFunction("ModifyCamera", DeltaTime, InOutView);
	return bAppliedData || bCalledFunction;
}

bool ULuaCameraModifier::ModifyPostProcess(float DeltaTime, FPostProcessSettings& InOutSettings)
{
	const bool bAppliedData = ApplyDataDrivenPostProcess(DeltaTime, InOutSettings);
	const bool bCalledFunction = CallModifierFunction("ModifyPostProcess", DeltaTime, InOutSettings);
	return bAppliedData || bCalledFunction;
}

bool ULuaCameraModifier::ModifyOverlay(float DeltaTime, FCameraOverlaySettings& InOutOverlay)
{
	const bool bAppliedData = ApplyDataDrivenOverlay(DeltaTime, InOutOverlay);
	const bool bCalledFunction = CallModifierFunction("ModifyOverlay", DeltaTime, InOutOverlay);
	return bAppliedData || bCalledFunction;
}

bool ULuaCameraModifier::ApplyDataDrivenCamera(float DeltaTime, FCameraViewInfo& InOutView)
{
#if WITH_LUA
	(void)DeltaTime;
	if (!bScriptLoaded || !bHasModifierDataTable)
	{
		return false;
	}

	sol::table CameraSection;
	bool bApplied = false;
	if (TryGetSection(ModifierDataTable, "Camera", CameraSection))
	{
		InOutView.FOV = EvaluateFloatField(CameraSection, "FOV", InOutView.FOV, ElapsedTime);
		InOutView.FOV += EvaluateFloatField(CameraSection, "FOVOffset", 0.0f, ElapsedTime);
		InOutView.Location += ReadVectorField(CameraSection, "LocationOffset", FVector::ZeroVector, ElapsedTime);
		bApplied = true;
	}

	ForEachModifierEntry(
		ModifierDataTable,
		[this, &InOutView, &bApplied](const sol::table& Entry, int32 Index)
		{
			(void)Index;
			if (!IsModifierType(Entry, "Camera"))
			{
				return true;
			}

			InOutView.FOV = EvaluateFloatField(Entry, "FOV", InOutView.FOV, ElapsedTime);
			InOutView.FOV += EvaluateFloatField(Entry, "FOVOffset", 0.0f, ElapsedTime);
			InOutView.Location += ReadVectorField(Entry, "LocationOffset", FVector::ZeroVector, ElapsedTime);
			bApplied = true;
			return true;
		});
	return bApplied;
#else
	(void)DeltaTime;
	(void)InOutView;
	return false;
#endif
}

bool ULuaCameraModifier::ApplyDataDrivenPostProcess(float DeltaTime, FPostProcessSettings& InOutSettings)
{
#if WITH_LUA
	(void)DeltaTime;
	if (!bScriptLoaded || !bHasModifierDataTable)
	{
		return false;
	}

	sol::table PostProcessSection;
	bool bApplied = false;
	if (TryGetSection(ModifierDataTable, "PostProcess", PostProcessSection))
	{
		InOutSettings.Gamma = EvaluateFloatField(PostProcessSection, "Gamma", InOutSettings.Gamma, ElapsedTime);
		InOutSettings.VignetteIntensity = EvaluateFloatField(PostProcessSection, "VignetteIntensity", InOutSettings.VignetteIntensity, ElapsedTime);
		InOutSettings.VignetteRadius = EvaluateFloatField(PostProcessSection, "VignetteRadius", InOutSettings.VignetteRadius, ElapsedTime);
		InOutSettings.VignetteSoftness = EvaluateFloatField(PostProcessSection, "VignetteSoftness", InOutSettings.VignetteSoftness, ElapsedTime);
		bApplied = true;
	}

	ForEachModifierEntry(
		ModifierDataTable,
		[this, &InOutSettings, &bApplied](const sol::table& Entry, int32 Index)
		{
			(void)Index;
			if (!IsModifierType(Entry, "PostProcess"))
			{
				return true;
			}

			InOutSettings.Gamma = EvaluateFloatField(Entry, "Gamma", InOutSettings.Gamma, ElapsedTime);
			InOutSettings.VignetteIntensity = EvaluateFloatField(Entry, "VignetteIntensity", InOutSettings.VignetteIntensity, ElapsedTime);
			InOutSettings.VignetteRadius = EvaluateFloatField(Entry, "VignetteRadius", InOutSettings.VignetteRadius, ElapsedTime);
			InOutSettings.VignetteSoftness = EvaluateFloatField(Entry, "VignetteSoftness", InOutSettings.VignetteSoftness, ElapsedTime);
			bApplied = true;
			return true;
		});
	return bApplied;
#else
	(void)DeltaTime;
	(void)InOutSettings;
	return false;
#endif
}

bool ULuaCameraModifier::ApplyDataDrivenOverlay(float DeltaTime, FCameraOverlaySettings& InOutOverlay)
{
#if WITH_LUA
	(void)DeltaTime;
	if (!bScriptLoaded || !bHasModifierDataTable)
	{
		return false;
	}

	sol::table OverlaySection;
	bool bApplied = false;
	if (TryGetSection(ModifierDataTable, "Overlay", OverlaySection))
	{
		InOutOverlay.LetterBoxRatio = EvaluateFloatField(OverlaySection, "LetterBoxRatio", InOutOverlay.LetterBoxRatio, ElapsedTime);
		InOutOverlay.FadeColor = ReadVector4Field(OverlaySection, "FadeColor", InOutOverlay.FadeColor, ElapsedTime);
		InOutOverlay.FadeColor.W = EvaluateFloatField(OverlaySection, "FadeAlpha", InOutOverlay.FadeColor.W, ElapsedTime);
		bApplied = true;
	}

	ForEachModifierEntry(
		ModifierDataTable,
		[this, &InOutOverlay, &bApplied](const sol::table& Entry, int32 Index)
		{
			(void)Index;
			if (!IsModifierType(Entry, "Overlay"))
			{
				return true;
			}

			InOutOverlay.LetterBoxRatio = EvaluateFloatField(Entry, "LetterBoxRatio", InOutOverlay.LetterBoxRatio, ElapsedTime);
			InOutOverlay.FadeColor = ReadVector4Field(Entry, "FadeColor", InOutOverlay.FadeColor, ElapsedTime);
			InOutOverlay.FadeColor.W = EvaluateFloatField(Entry, "FadeAlpha", InOutOverlay.FadeColor.W, ElapsedTime);
			bApplied = true;
			return true;
		});
	return bApplied;
#else
	(void)DeltaTime;
	(void)InOutOverlay;
	return false;
#endif
}

bool ULuaCameraModifier::ProcessDataDrivenActions()
{
#if WITH_LUA
	if (!bScriptLoaded || !bHasModifierDataTable || CameraOwner == nullptr)
	{
		return false;
	}

	bool bTriggeredAny = false;
	ForEachModifierEntry(
		ModifierDataTable,
		[this, &bTriggeredAny](const sol::table& Entry, int32 Index)
		{
			const FString Type = ReadStringField(Entry, "Type", "");
			if (Type != "Fade" && Type != "LetterBox" && Type != "Shake")
			{
				return true;
			}

			const FString ExplicitId = ReadStringField(Entry, "Id", "");
			const FString TriggerKey = ExplicitId.empty() ? (Type + "#" + std::to_string(Index)) : (Type + "#" + ExplicitId);
			if (std::find(TriggeredActionKeys.begin(), TriggeredActionKeys.end(), TriggerKey) != TriggeredActionKeys.end())
			{
				return true;
			}

			const FString Trigger = ReadStringField(Entry, "Trigger", "OnActivate");
			if (Trigger != "OnActivate")
			{
				return true;
			}

			if (Type == "Fade")
			{
				const FVector4 FadeColor = ReadVector4Field(Entry, "Color", ReadVector4Field(Entry, "FadeColor", FVector4(0.0f, 0.0f, 0.0f, 1.0f), ElapsedTime), ElapsedTime);
				const float FromAlpha = EvaluateFloatField(Entry, "FromAlpha", 0.0f, ElapsedTime);
				const float ToAlpha = EvaluateFloatField(Entry, "ToAlpha", EvaluateFloatField(Entry, "Alpha", FadeColor.W, ElapsedTime), ElapsedTime);
				const float Duration = EvaluateFloatField(Entry, "Duration", 0.0f, ElapsedTime);
				const bool bHold = ReadBoolField(Entry, "Hold", ReadBoolField(Entry, "bHoldWhenFinished", false));
				CameraOwner->StartCameraFade(FVector(FadeColor.X, FadeColor.Y, FadeColor.Z), FromAlpha, ToAlpha, Duration, bHold);
			}
			else if (Type == "LetterBox")
			{
				const float TargetRatio = EvaluateFloatField(Entry, "TargetRatio", EvaluateFloatField(Entry, "LetterBoxRatio", 0.0f, ElapsedTime), ElapsedTime);
				const float Duration = EvaluateFloatField(Entry, "Duration", 0.0f, ElapsedTime);
				CameraOwner->StartLetterBox(TargetRatio, Duration);
			}
			else if (Type == "Shake")
			{
				FCameraShakeParams Params;
				Params.Duration = EvaluateFloatField(Entry, "Duration", Params.Duration, ElapsedTime);
				ReadFloatArrayField(Entry, "RotAmplitude", Params.RotAmplitude, 3, ElapsedTime);
				ReadFloatArrayField(Entry, "RotFrequency", Params.RotFrequency, 3, ElapsedTime);
				ReadFloatArrayField(Entry, "LocAmplitude", Params.LocAmplitude, 3, ElapsedTime);
				ReadFloatArrayField(Entry, "LocFrequency", Params.LocFrequency, 3, ElapsedTime);
				ReadFloatArrayField(Entry, "RotBezierCP", Params.RotBezierCP, 6, ElapsedTime);
				ReadFloatArrayField(Entry, "LocBezierCP", Params.LocBezierCP, 6, ElapsedTime);
				ReadFloatArrayField(Entry, "FOVBezierCP", Params.FOVBezierCP, 6, ElapsedTime);
				Params.FOVAmplitude = EvaluateFloatField(Entry, "FOVAmplitude", Params.FOVAmplitude, ElapsedTime);
				Params.FOVFrequency = EvaluateFloatField(Entry, "FOVFrequency", Params.FOVFrequency, ElapsedTime);
				Params.bLoop = ReadBoolField(Entry, "bLoop", false);
				if (ActionSourceName.empty())
				{
					CameraOwner->StartCameraShake(Params);
				}
				else
				{
					CameraOwner->StartNamedCameraShake(ActionSourceName, Params);
				}
			}

			TriggeredActionKeys.push_back(TriggerKey);
			bTriggeredAny = true;
			return true;
		});

	return bTriggeredAny;
#else
	return false;
#endif
}

bool ULuaCameraModifier::CallModifierFunction(const char* FunctionName, float DeltaTime, FCameraViewInfo& InOutView)
{
#if WITH_LUA
	return bScriptLoaded && CallLuaModifierFunction(LuaState.get(), ScriptPath, FunctionName, DeltaTime, InOutView, LastScriptError);
#else
	(void)FunctionName;
	(void)DeltaTime;
	(void)InOutView;
	return false;
#endif
}

bool ULuaCameraModifier::CallModifierFunction(const char* FunctionName, float DeltaTime, FPostProcessSettings& InOutSettings)
{
#if WITH_LUA
	return bScriptLoaded && CallLuaModifierFunction(LuaState.get(), ScriptPath, FunctionName, DeltaTime, InOutSettings, LastScriptError);
#else
	(void)FunctionName;
	(void)DeltaTime;
	(void)InOutSettings;
	return false;
#endif
}

bool ULuaCameraModifier::CallModifierFunction(const char* FunctionName, float DeltaTime, FCameraOverlaySettings& InOutOverlay)
{
#if WITH_LUA
	return bScriptLoaded && CallLuaModifierFunction(LuaState.get(), ScriptPath, FunctionName, DeltaTime, InOutOverlay, LastScriptError);
#else
	(void)FunctionName;
	(void)DeltaTime;
	(void)InOutOverlay;
	return false;
#endif
}

void ULuaCameraModifier::SetLastScriptError(const FString& Error)
{
	LastScriptError = Error;
}
