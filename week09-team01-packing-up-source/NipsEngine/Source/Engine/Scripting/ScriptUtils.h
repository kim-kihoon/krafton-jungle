#pragma once

#include "Core/Containers/String.h"

struct FScriptCreateResult
{
	bool bSuccess = false;
	bool bCreated = false;
	bool bAlreadyExists = false;
	FString ScriptPath;
	FString ErrorMessage;
};

class FScriptUtils
{
public:
	static FString GetScriptDirectory();
	static FString GetTemplateScriptPath();

	static FString SanitizeFileName(const FString& Name);
	static FString MakeScriptFileName(const FString& SceneName, const FString& ActorName);
	static FString MakeActorScriptPath(const FString& SceneName, const FString& ActorName);

	static bool DoesFileExist(const FString& Path);
	static bool EnsureTemplateScript(FString* OutError = nullptr);
	static FScriptCreateResult CreateScriptFromTemplate(const FString& SceneName, const FString& ActorName);
	static bool OpenScript(const FString& ScriptPath, FString* OutError = nullptr);
};
