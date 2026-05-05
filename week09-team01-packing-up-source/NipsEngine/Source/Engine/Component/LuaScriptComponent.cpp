#include "Component/LuaScriptComponent.h"

#include "Component/PrimitiveComponent.h"
#include "Core/Paths.h"
#include "GameFramework/AActor.h"
#include "Object/ObjectFactory.h"
#include "Scripting/LuaScriptSystem.h"
#include "Core/Logger.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <shellapi.h>
#include <cstring>

DEFINE_CLASS(ULuaScriptComponent, UActorComponent)
REGISTER_FACTORY(ULuaScriptComponent)

namespace
{
	FString SanitizeScriptName(FString Name)
	{
		if (Name.empty())
		{
			return "Actor";
		}

		for (char& Ch : Name)
		{
			const bool bAlphaNum = (Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z') || (Ch >= '0' && Ch <= '9');
			if (!bAlphaNum && Ch != '_' && Ch != '-')
			{
				Ch = '_';
			}
		}

		return Name;
	}

	FString StripLuaExtension(FString Name)
	{
		constexpr size_t ExtensionLength = 4;
		if (Name.size() < ExtensionLength)
		{
			return Name;
		}

		const size_t Offset = Name.size() - ExtensionLength;
		if ((Name[Offset] == '.') &&
			(Name[Offset + 1] == 'l' || Name[Offset + 1] == 'L') &&
			(Name[Offset + 2] == 'u' || Name[Offset + 2] == 'U') &&
			(Name[Offset + 3] == 'a' || Name[Offset + 3] == 'A'))
		{
			Name.resize(Offset);
		}

		return Name;
	}
}

ULuaScriptComponent::ULuaScriptComponent()
{
	bCanEverTick = true;
}

ULuaScriptComponent::~ULuaScriptComponent()
{
	FLuaScriptSystem::Get().UnloadScript(this);
}

void ULuaScriptComponent::BeginPlay()
{
	UActorComponent::BeginPlay();
	BindCollisionEvents();

	if (!ScriptPath.empty())
	{
		if (!bUseDefaultScriptPath || HasScriptFile())
		{
			ReloadScript();
		}
	}

	if (bLoaded)
	{
		FLuaScriptSystem::Get().CallBeginPlay(this, GetOwner());
	}
}

void ULuaScriptComponent::EndPlay()
{
	if (bLoaded)
	{
		FLuaScriptSystem::Get().CallEndPlay(this, GetOwner());
	}

	FLuaScriptSystem::Get().UnloadScript(this);
	bLoaded = false;
	UActorComponent::EndPlay();
}

void ULuaScriptComponent::OnRegister()
{
	if (bRegistered)
	{
		return;
	}

	bRegistered = true;

	if (GetOwner())
	{
		BindCollisionEvents();
	}
}

void ULuaScriptComponent::OnUnregister()
{
	UnbindCollisionEvents();
	FLuaScriptSystem::Get().UnloadScript(this);
	bLoaded = false;
	bRegistered = false;
}

void ULuaScriptComponent::PostDuplicate(UObject* Original)
{
	UObject::PostDuplicate(Original);

	const ULuaScriptComponent* OriginalScript = Cast<ULuaScriptComponent>(Original);
	if (!OriginalScript)
	{
		return;
	}

	if (ScriptPath.empty())
	{
		if (!OriginalScript->ScriptPath.empty())
		{
			ScriptPath = OriginalScript->ScriptPath;
		}
	}

	bLoaded = false;
	bCollisionEventsBound = false;
	bLoggedRuntimeDisabled = false;
	LastScriptError.clear();
}

void ULuaScriptComponent::Serialize(FArchive& Ar)
{
	UActorComponent::Serialize(Ar);
	Ar << "ScriptPath" << ScriptPath;
	Ar << "AutoLoad" << bAutoLoad;
	Ar << "AutoCreateScript" << bAutoCreateScript;
	Ar << "AutoReloadScript" << bAutoReloadScript;
	Ar << "UseDefaultScriptPath" << bUseDefaultScriptPath;

	if (Ar.IsLoading())
	{
		bUseDefaultScriptPath = bUseDefaultScriptPath || ScriptPath.empty();
	}
}

void ULuaScriptComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UActorComponent::GetEditableProperties(OutProps);
}

void ULuaScriptComponent::PostEditProperty(const char* PropertyName)
{
	UActorComponent::PostEditProperty(PropertyName);
	if (PropertyName && strcmp(PropertyName, "Script Path") == 0)
	{
		bLoaded = false;
		LastScriptWriteTime = 0;
		ReloadCheckElapsed = 0.0f;
		if (ScriptPath.empty())
		{
			bUseDefaultScriptPath = true;
		}
		else
		{
			bUseDefaultScriptPath = false;
			const std::filesystem::path Path(FPaths::ToWide(ScriptPath));
			EditorScriptName = FPaths::ToUtf8(Path.stem().wstring());
		}
	}
	else if (PropertyName && strcmp(PropertyName, "Use Default Script Path") == 0)
	{
		bLoaded = false;
		if (bUseDefaultScriptPath)
		{
			ScriptPath = MakeDefaultScriptPath();
			LastScriptWriteTime = 0;
			ReloadCheckElapsed = 0.0f;
		}
	}
	else if (PropertyName && strcmp(PropertyName, "Use Actor Name Script") == 0)
	{
		SetUseActorNameScript(bUseDefaultScriptPath);
	}
}

bool ULuaScriptComponent::ReloadScript()
{
	if (ScriptPath.empty())
	{
		SetLastScriptError("No Lua script assigned.");
		UE_LOG("LuaScriptComponent: failed to reload script: %s", LastScriptError.c_str());
		return false;
	}

	const bool bHadLoadedScript = bLoaded;
	const bool bReloaded = FLuaScriptSystem::Get().ReloadScript(this, FPaths::ToAbsoluteString(FPaths::ToWide(ScriptPath)));
	SetLastScriptError(FLuaScriptSystem::Get().GetLastError());

	if (bReloaded)
	{
		bLoaded = true;
		bLoggedRuntimeDisabled = false;
		RefreshScriptWriteTime();
		return true;
	}

	bLoaded = bHadLoadedScript;

	if (!IsLuaRuntimeEnabled() && !bLoggedRuntimeDisabled)
	{
		UE_LOG("LuaScriptComponent: Lua runtime is disabled. Script '%s' was not loaded.", ScriptPath.c_str());
		bLoggedRuntimeDisabled = true;
	}
	else if (!LastScriptError.empty())
	{
		UE_LOG("LuaScriptComponent: failed to load '%s': %s", ScriptPath.c_str(), LastScriptError.c_str());
	}

	return false;
}

bool ULuaScriptComponent::EnsureScriptFile()
{
	if (ScriptPath.empty())
	{
		SetLastScriptError("No Lua script assigned.");
		UE_LOG("LuaScriptComponent: %s", LastScriptError.c_str());
		return false;
	}

	std::filesystem::path Target(FPaths::ToAbsolute(FPaths::ToWide(ScriptPath)));
	if (std::filesystem::exists(Target))
	{
		SetLastScriptError("");
		RefreshScriptWriteTime();
		return true;
	}

	std::error_code ErrorCode;
	std::filesystem::create_directories(Target.parent_path(), ErrorCode);
	if (ErrorCode)
	{
		SetLastScriptError("Failed to create script directory: " + ErrorCode.message());
		UE_LOG("LuaScriptComponent: %s", LastScriptError.c_str());
		return false;
	}

	std::filesystem::path Template(FPaths::ToAbsolute(L"Asset/Scripts/template.lua"));
	if (!std::filesystem::exists(Template))
	{
		SetLastScriptError("Template script does not exist: Asset/Scripts/template.lua");
		UE_LOG("LuaScriptComponent: %s", LastScriptError.c_str());
		return false;
	}

	std::filesystem::copy_file(Template, Target, std::filesystem::copy_options::overwrite_existing, ErrorCode);
	if (ErrorCode)
	{
		SetLastScriptError("Failed to copy template.lua: " + ErrorCode.message());
		UE_LOG("LuaScriptComponent: %s", LastScriptError.c_str());
		return false;
	}

	SetLastScriptError("");
	RefreshScriptWriteTime();
	return true;
}

bool ULuaScriptComponent::CreateScriptFileFromName(const FString& InScriptName, bool bOverwriteExisting)
{
	const FString NewScriptPath = MakeScriptPathFromName(InScriptName);
	const std::filesystem::path Target(FPaths::ToAbsolute(FPaths::ToWide(NewScriptPath)));
	if (std::filesystem::exists(Target) && !bOverwriteExisting)
	{
		SetLastScriptError("Script already exists: " + NewScriptPath);
		UE_LOG("LuaScriptComponent: %s", LastScriptError.c_str());
		return false;
	}

	ScriptPath = NewScriptPath;
	EditorScriptName = SanitizeScriptName(StripLuaExtension(InScriptName));
	bLoaded = false;
	bUseDefaultScriptPath = false;
	bLoggedRuntimeDisabled = false;
	LastScriptWriteTime = 0;
	ReloadCheckElapsed = 0.0f;

	std::error_code ErrorCode;
	std::filesystem::create_directories(Target.parent_path(), ErrorCode);
	if (ErrorCode)
	{
		SetLastScriptError("Failed to create script directory: " + ErrorCode.message());
		UE_LOG("LuaScriptComponent: %s", LastScriptError.c_str());
		return false;
	}

	const std::filesystem::path Template(FPaths::ToAbsolute(L"Asset/Scripts/template.lua"));
	if (!std::filesystem::exists(Template))
	{
		SetLastScriptError("Template script does not exist: Asset/Scripts/template.lua");
		UE_LOG("LuaScriptComponent: %s", LastScriptError.c_str());
		return false;
	}

	const auto CopyOptions = bOverwriteExisting
		? std::filesystem::copy_options::overwrite_existing
		: std::filesystem::copy_options::none;
	std::filesystem::copy_file(Template, Target, CopyOptions, ErrorCode);
	if (ErrorCode)
	{
		SetLastScriptError("Failed to copy template.lua: " + ErrorCode.message());
		UE_LOG("LuaScriptComponent: %s", LastScriptError.c_str());
		return false;
	}

	SetLastScriptError("");
	RefreshScriptWriteTime();
	UE_LOG("LuaScriptComponent: %s script '%s'.", bOverwriteExisting ? "overwrote" : "created", ScriptPath.c_str());
	return true;
}

bool ULuaScriptComponent::HasScriptFile() const
{
	if (ScriptPath.empty())
	{
		return false;
	}

	return std::filesystem::exists(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(ScriptPath))));
}

FString ULuaScriptComponent::GetAbsoluteScriptPath() const
{
	if (ScriptPath.empty())
	{
		return "";
	}

	return FPaths::ToAbsoluteString(FPaths::ToWide(ScriptPath));
}

FString ULuaScriptComponent::GetScriptPathForName(const FString& InScriptName) const
{
	return MakeScriptPathFromName(InScriptName);
}

bool ULuaScriptComponent::DoesScriptFileExistForName(const FString& InScriptName) const
{
	const FString TargetPath = MakeScriptPathFromName(InScriptName);
	return std::filesystem::exists(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(TargetPath))));
}

bool ULuaScriptComponent::OpenScriptFile()
{
	if (ScriptPath.empty())
	{
		SetLastScriptError("No Lua script assigned.");
		UE_LOG("LuaScriptComponent: failed to open script: %s", LastScriptError.c_str());
		return false;
	}

	if (!HasScriptFile())
	{
		SetLastScriptError("Script file does not exist: " + ScriptPath);
		UE_LOG("LuaScriptComponent: failed to open script: %s", LastScriptError.c_str());
		return false;
	}

	const std::wstring AbsolutePath = FPaths::ToAbsolute(FPaths::ToWide(ScriptPath));
	const HINSTANCE Result = ShellExecuteW(nullptr, L"open", AbsolutePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	const bool bOpened = reinterpret_cast<intptr_t>(Result) > 32;
	if (!bOpened)
	{
		SetLastScriptError("Failed to open script: " + ScriptPath);
		UE_LOG("LuaScriptComponent: %s", LastScriptError.c_str());
	}
	else
	{
		SetLastScriptError("");
	}
	return bOpened;
}

void ULuaScriptComponent::HandleBeginOverlap(const FOverlapResult& Overlap)
{
	if (bLoaded)
	{
		FLuaScriptSystem::Get().CallOverlap(this, GetOwner(), Overlap);
	}
}

void ULuaScriptComponent::HandleEndOverlap(const FOverlapResult& Overlap)
{
	if (bLoaded)
	{
		FLuaScriptSystem::Get().CallEndOverlap(this, GetOwner(), Overlap);
	}
}

void ULuaScriptComponent::HandleHit(const FHitResult& Hit)
{
	if (bLoaded)
	{
		FLuaScriptSystem::Get().CallHit(this, GetOwner(), Hit);
	}
}

void ULuaScriptComponent::HandleInteract(AActor* Interactor)
{
	if (bLoaded)
	{
		FLuaScriptSystem::Get().CallInteract(this, GetOwner(), Interactor);
	}
}

void ULuaScriptComponent::HandlePickedUp(AActor* Picker)
{
	if (bLoaded)
	{
		FLuaScriptSystem::Get().CallPickedUp(this, GetOwner(), Picker);
	}
}

void ULuaScriptComponent::SetScriptPath(const FString& InScriptPath)
{
	ScriptPath = InScriptPath;
	bLoaded = false;
	bUseDefaultScriptPath = ScriptPath.empty();
	LastScriptWriteTime = 0;
	ReloadCheckElapsed = 0.0f;
	if (!ScriptPath.empty())
	{
		const std::filesystem::path Path(FPaths::ToWide(ScriptPath));
		EditorScriptName = FPaths::ToUtf8(Path.stem().wstring());
	}
	SetLastScriptError("");
	bLoggedRuntimeDisabled = false;
}

void ULuaScriptComponent::SetUseActorNameScript(bool bInUseActorNameScript)
{
	bUseDefaultScriptPath = bInUseActorNameScript;
	bLoaded = false;
	LastScriptWriteTime = 0;
	ReloadCheckElapsed = 0.0f;

	if (bUseDefaultScriptPath)
	{
		EditorScriptName = GetActorNameScriptName();
		ScriptPath = MakeDefaultScriptPath();
	}
}

FString ULuaScriptComponent::GetActorNameScriptName() const
{
	const AActor* OwnerActor = GetOwner();
	const FString ActorName = OwnerActor ? OwnerActor->GetFName().ToString() : "Actor";
	return SanitizeScriptName(ActorName);
}

FString ULuaScriptComponent::GetEditorScriptName() const
{
	if (bUseDefaultScriptPath || EditorScriptName.empty())
	{
		return GetActorNameScriptName();
	}

	return EditorScriptName;
}

void ULuaScriptComponent::SetEditorScriptName(const FString& InScriptName)
{
	EditorScriptName = SanitizeScriptName(StripLuaExtension(InScriptName));
}

bool ULuaScriptComponent::IsLuaRuntimeEnabled() const
{
	return FLuaScriptSystem::Get().IsLuaEnabled();
}

void ULuaScriptComponent::TickComponent(float DeltaTime)
{
	if (!bCollisionEventsBound)
	{
		BindCollisionEvents();
	}

	if (bLoaded)
	{
		FLuaScriptSystem::Get().CallTick(this, GetOwner(), DeltaTime);
	}

	TickAutoReload(DeltaTime);
}

void ULuaScriptComponent::TickAutoReload(float DeltaTime)
{
	if (ScriptPath.empty())
	{
		return;
	}

	ReloadCheckElapsed += DeltaTime;
	if (ReloadCheckElapsed < ReloadCheckInterval)
	{
		return;
	}
	ReloadCheckElapsed = 0.0f;

	const uint64 CurrentWriteTime = GetScriptLastWriteTime();
	if (CurrentWriteTime == 0)
	{
		return;
	}

	if (LastScriptWriteTime == 0)
	{
		LastScriptWriteTime = CurrentWriteTime;
		return;
	}

	if (CurrentWriteTime != LastScriptWriteTime)
	{
		LastScriptWriteTime = CurrentWriteTime;
		UE_LOG("LuaScriptComponent: auto reloading '%s'.", ScriptPath.c_str());
		ReloadScript();
	}
}

uint64 ULuaScriptComponent::GetScriptLastWriteTime() const
{
	if (ScriptPath.empty())
	{
		return 0;
	}

	const std::filesystem::path Target(FPaths::ToAbsolute(FPaths::ToWide(ScriptPath)));
	std::error_code ErrorCode;
	const auto WriteTime = std::filesystem::last_write_time(Target, ErrorCode);
	if (ErrorCode)
	{
		return 0;
	}

	const auto Duration = WriteTime.time_since_epoch();
	return static_cast<uint64>(std::chrono::duration_cast<std::chrono::nanoseconds>(Duration).count());
}

void ULuaScriptComponent::RefreshScriptWriteTime()
{
	LastScriptWriteTime = GetScriptLastWriteTime();
	ReloadCheckElapsed = 0.0f;
}

void ULuaScriptComponent::BindCollisionEvents()
{
	if (bCollisionEventsBound)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	bool bAnyBound = false;
	for (UPrimitiveComponent* Primitive : OwnerActor->GetPrimitiveComponents())
	{
		if (!Primitive)
		{
			continue;
		}

		Primitive->OnComponentBeginOverlap.AddDynamic(this, &ULuaScriptComponent::HandleBeginOverlap);
		Primitive->OnComponentEndOverlap.AddDynamic(this, &ULuaScriptComponent::HandleEndOverlap);
		Primitive->OnComponentHit.AddDynamic(this, &ULuaScriptComponent::HandleHit);
		bAnyBound = true;
	}

	bCollisionEventsBound = bAnyBound;
}

void ULuaScriptComponent::UnbindCollisionEvents()
{
	if (!bCollisionEventsBound)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		bCollisionEventsBound = false;
		return;
	}

	for (UPrimitiveComponent* Primitive : OwnerActor->GetPrimitiveComponents())
	{
		if (!Primitive)
		{
			continue;
		}

		Primitive->OnComponentBeginOverlap.RemoveDynamic(this);
		Primitive->OnComponentEndOverlap.RemoveDynamic(this);
		Primitive->OnComponentHit.RemoveDynamic(this);
	}

	bCollisionEventsBound = false;
}

void ULuaScriptComponent::EnsureDefaultScriptPath()
{
	if (!ScriptPath.empty())
	{
		return;
	}

	ScriptPath = MakeDefaultScriptPath();
	bUseDefaultScriptPath = true;
}

FString ULuaScriptComponent::MakeDefaultScriptPath() const
{
	return MakeDefaultScriptPathForActor(GetOwner());
}

FString ULuaScriptComponent::MakeDefaultScriptPathForActor(const AActor* OwnerActor) const
{
	const FString ActorName = OwnerActor ? OwnerActor->GetFName().ToString() : "Actor";
	return "Asset/Scripts/" + SanitizeScriptName(ActorName) + ".lua";
}

FString ULuaScriptComponent::MakeScriptPathFromName(const FString& InScriptName) const
{
	return "Asset/Scripts/" + SanitizeScriptName(StripLuaExtension(InScriptName)) + ".lua";
}

void ULuaScriptComponent::SetLastScriptError(const FString& Error)
{
	LastScriptError = Error;
}
