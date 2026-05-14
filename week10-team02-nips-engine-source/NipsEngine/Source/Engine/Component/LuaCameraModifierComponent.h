#pragma once

#include "ActorComponent.h"

class ULuaCameraModifierComponent : public UActorComponent
{
public:
	DECLARE_CLASS(ULuaCameraModifierComponent, UActorComponent)

	ULuaCameraModifierComponent() = default;
	~ULuaCameraModifierComponent() override = default;

	void Serialize(FArchive& Ar) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostDuplicate(UObject* Original) override;
	void PostEditProperty(const char* PropertyName) override;

	const FString& GetScriptPath() const { return ScriptPath; }
	void SetScriptPath(const FString& InScriptPath);

private:
	FString ScriptPath;
};
