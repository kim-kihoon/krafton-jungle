#pragma once

#include "ActorComponent.h"
#include "Core/CollisionTypes.h"

class ULuaScriptComponent : public UActorComponent
{
public:
	DECLARE_CLASS(ULuaScriptComponent, UActorComponent)

	ULuaScriptComponent();
	~ULuaScriptComponent() override;

	void BeginPlay() override;
	void EndPlay() override;
	void OnRegister() override;
	void OnUnregister() override;
	void PostDuplicate(UObject* Original) override;
	void Serialize(FArchive& Ar) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	bool ReloadScript();
	bool EnsureScriptFile();
	bool CreateScriptFileFromName(const FString& InScriptName, bool bOverwriteExisting = false);
	bool HasScriptFile() const;
	bool OpenScriptFile();

	const FString& GetScriptPath() const { return ScriptPath; }
	FString GetAbsoluteScriptPath() const;
	FString GetScriptPathForName(const FString& InScriptName) const;
	bool DoesScriptFileExistForName(const FString& InScriptName) const;
	void SetScriptPath(const FString& InScriptPath);
	bool GetUseActorNameScript() const { return bUseDefaultScriptPath; }
	void SetUseActorNameScript(bool bInUseActorNameScript);
	FString GetActorNameScriptName() const;
	FString GetEditorScriptName() const;
	void SetEditorScriptName(const FString& InScriptName);
	bool IsScriptLoaded() const { return bLoaded; }
	bool IsLuaRuntimeEnabled() const;
	const FString& GetLastScriptError() const { return LastScriptError; }

	void HandleBeginOverlap(const FOverlapResult& Overlap);
	void HandleEndOverlap(const FOverlapResult& Overlap);
	void HandleHit(const FHitResult& Hit);
	void HandleInteract(AActor* Interactor);
	void HandlePickedUp(AActor* Picker);

protected:
	void TickComponent(float DeltaTime) override;

private:
	void BindCollisionEvents();
	void UnbindCollisionEvents();
	void TickAutoReload(float DeltaTime);
	uint64 GetScriptLastWriteTime() const;
	void RefreshScriptWriteTime();
	void EnsureDefaultScriptPath();
	FString MakeDefaultScriptPath() const;
	FString MakeDefaultScriptPathForActor(const AActor* OwnerActor) const;
	FString MakeScriptPathFromName(const FString& InScriptName) const;
	void SetLastScriptError(const FString& Error);

	FString ScriptPath;
	FString EditorScriptName;
	FString LastScriptError;
	bool bAutoLoad = true;
	bool bAutoCreateScript = true;
	bool bAutoReloadScript = true;
	bool bLoaded = false;
	bool bCollisionEventsBound = false;
	bool bUseDefaultScriptPath = true;
	bool bLoggedRuntimeDisabled = false;
	float ReloadCheckElapsed = 0.0f;
	float ReloadCheckInterval = 0.25f;
	uint64 LastScriptWriteTime = 0;
};
