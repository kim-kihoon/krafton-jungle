#pragma once

class UWorld;
class AActor;

struct SceneSaveData
{
	FString Name;
	int Version = 1;
	int NextUUID = 0;
	TArray<AActor*> Primitives;
};

class USceneSerializer
{
public:
	static bool SaveScene(const FString& sceneName, UWorld* World);
	static bool LoadScene(const FString& sceneName, UWorld* World);

	static bool SaveEditorConfig();
	static bool LoadEditorConfig();

private:
	static FString Serialize(SceneSaveData& sceneInfo);

	static const FString GetSaveDirectory() { return "Content/Scenes/"; }
};