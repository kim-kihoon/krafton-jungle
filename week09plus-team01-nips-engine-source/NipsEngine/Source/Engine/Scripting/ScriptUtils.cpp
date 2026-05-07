#include "Scripting/ScriptUtils.h"

#include "Core/Paths.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <shellapi.h>

namespace
{
	constexpr const char* DefaultSceneName = "DefaultScene";
	constexpr const char* DefaultActorName = "Actor";
	constexpr const char* ScriptExtension = ".lua";

	constexpr const char* DefaultTemplateScript =
		"-- LuaScriptComponent contract\n"
		"-- owner: AActor bound from C++\n"
		"-- otherActor: AActor or nil\n"
		"-- hit: FHitResult\n"
		"-- Log(message): writes to the editor console\n"
		"-- StartCoroutine(function() ... end), wait(seconds): coroutine helpers\n"
		"\n"
		"function BeginPlay(owner)\n"
		"    print(\"BeginPlay\", owner:GetName())\n"
		"\n"
		"    StartCoroutine(function()\n"
		"        print(\"coroutine start\")\n"
		"\n"
		"        wait(1.0)\n"
		"        print(\"after 1 sec\")\n"
		"\n"
		"        wait(2.0)\n"
		"        print(\"after 3 sec total\")\n"
		"    end)\n"
		"end\n"
		"\n"
		"function EndPlay(owner)\n"
		"end\n"
		"\n"
		"function OnOverlap(owner, otherActor)\n"
		"end\n"
		"\n"
		"function OnEndOverlap(owner, otherActor)\n"
		"end\n"
		"\n"
		"function OnHit(owner, hit)\n"
		"end\n"
		"\n"
		"function OnInteract(owner, interactor)\n"
		"end\n"
		"\n"
		"function Tick(owner, deltaTime)\n"
		"end\n";

	std::filesystem::path ToAbsolutePath(const FString& Path)
	{
		return std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(Path))).lexically_normal();
	}

	FString MakeErrorMessage(const char* Prefix, const std::exception& Exception)
	{
		FString Result = Prefix;
		Result += ": ";
		Result += Exception.what();
		return Result;
	}
}

FString FScriptUtils::GetScriptDirectory()
{
	return "Asset/Scripts";
}

FString FScriptUtils::GetTemplateScriptPath()
{
	return GetScriptDirectory() + "/template.lua";
}

FString FScriptUtils::SanitizeFileName(const FString& Name)
{
	FString Result;
	Result.reserve(Name.size());

	for (char Ch : Name)
	{
		const unsigned char Byte = static_cast<unsigned char>(Ch);
		const bool bAlphaNumeric =
			(Ch >= 'a' && Ch <= 'z') ||
			(Ch >= 'A' && Ch <= 'Z') ||
			(Ch >= '0' && Ch <= '9');

		if (Byte >= 0x80 || bAlphaNumeric || Ch == '_' || Ch == '-')
		{
			Result.push_back(Ch);
		}
		else if (Ch == ' ' || Ch == '.' || Ch == '/' || Ch == '\\' || Ch == ':' ||
			Ch == '*' || Ch == '?' || Ch == '"' || Ch == '<' || Ch == '>' || Ch == '|')
		{
			Result.push_back('_');
		}
	}

	while (!Result.empty() && Result.front() == '_')
	{
		Result.erase(Result.begin());
	}
	while (!Result.empty() && Result.back() == '_')
	{
		Result.pop_back();
	}

	return Result.empty() ? FString("Unnamed") : Result;
}

FString FScriptUtils::MakeScriptFileName(const FString& SceneName, const FString& ActorName)
{
	const FString SafeSceneName = SanitizeFileName(SceneName.empty() ? FString(DefaultSceneName) : SceneName);
	const FString SafeActorName = SanitizeFileName(ActorName.empty() ? FString(DefaultActorName) : ActorName);
	return SafeSceneName + "_" + SafeActorName + ScriptExtension;
}

FString FScriptUtils::MakeActorScriptPath(const FString& SceneName, const FString& ActorName)
{
	return GetScriptDirectory() + "/" + MakeScriptFileName(SceneName, ActorName);
}

bool FScriptUtils::DoesFileExist(const FString& Path)
{
	return std::filesystem::exists(ToAbsolutePath(Path));
}

bool FScriptUtils::EnsureTemplateScript(FString* OutError)
{
	try
	{
		const std::filesystem::path TemplatePath = ToAbsolutePath(GetTemplateScriptPath());
		std::filesystem::create_directories(TemplatePath.parent_path());

		if (std::filesystem::exists(TemplatePath))
		{
			return true;
		}

		std::ofstream File(TemplatePath, std::ios::binary);
		if (!File.is_open())
		{
			if (OutError) *OutError = "Failed to create template.lua";
			return false;
		}

		File << DefaultTemplateScript;
		return true;
	}
	catch (const std::exception& Exception)
	{
		if (OutError) *OutError = MakeErrorMessage("Failed to ensure template.lua", Exception);
		return false;
	}
}

FScriptCreateResult FScriptUtils::CreateScriptFromTemplate(const FString& SceneName, const FString& ActorName)
{
	FScriptCreateResult Result;
	Result.ScriptPath = MakeActorScriptPath(SceneName, ActorName);

	try
	{
		FString Error;
		if (!EnsureTemplateScript(&Error))
		{
			Result.ErrorMessage = Error;
			return Result;
		}

		const std::filesystem::path SourcePath = ToAbsolutePath(GetTemplateScriptPath());
		const std::filesystem::path TargetPath = ToAbsolutePath(Result.ScriptPath);

		std::filesystem::create_directories(TargetPath.parent_path());

		if (std::filesystem::exists(TargetPath))
		{
			Result.bSuccess = true;
			Result.bAlreadyExists = true;
			return Result;
		}

		std::filesystem::copy_file(SourcePath, TargetPath, std::filesystem::copy_options::none);
		Result.bSuccess = true;
		Result.bCreated = true;
		return Result;
	}
	catch (const std::exception& Exception)
	{
		Result.ErrorMessage = MakeErrorMessage("Failed to create Lua script", Exception);
		return Result;
	}
}

bool FScriptUtils::OpenScript(const FString& ScriptPath, FString* OutError)
{
	try
	{
		const std::filesystem::path AbsolutePath = ToAbsolutePath(ScriptPath);
		if (!std::filesystem::exists(AbsolutePath))
		{
			if (OutError) *OutError = "Lua script file does not exist";
			return false;
		}

		HINSTANCE Instance = ShellExecuteW(
			nullptr,
			L"open",
			AbsolutePath.c_str(),
			nullptr,
			AbsolutePath.parent_path().c_str(),
			SW_SHOWNORMAL);

		if (reinterpret_cast<INT_PTR>(Instance) <= 32)
		{
			if (OutError) *OutError = "ShellExecuteW failed to open Lua script";
			return false;
		}

		return true;
	}
	catch (const std::exception& Exception)
	{
		if (OutError) *OutError = MakeErrorMessage("Failed to open Lua script", Exception);
		return false;
	}
}
