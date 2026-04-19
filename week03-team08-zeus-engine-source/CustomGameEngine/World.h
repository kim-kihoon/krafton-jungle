#pragma once

#include "Singleton.h"
#include "Logger.h"		// TEST
#include "Scene.h"
#include "Renderer/Level.h"
#include "Renderer/TextRenderObject.h"
#include "Component/TextBatchComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/LineBatchComponent.h"

class FLevel;

class UWorld : public UObject, public ISingleton<UWorld>
{
	friend class ISingleton<UWorld>;
private:
	UWorld();

public:
	~UWorld() override;

	// === Scene Management
	UScene* GetActiveScene();
	void SetActiveScene(UScene*);
	UScene* NewScene();
	void SaveScene(FString sceneName);
	UScene* LoadScene();
	UScene* LoadScene(FWString& sceneName);
	void UnloadScene();
	// === Scene Management

	inline URenderer* GetRenderer() const { return Renderer; }

	// Batch Line Rendering
	ULineBatchComponent* GetLineBatcher(bool bPersistentLines, uint8_t DepthPriority) const;
	void DrawDebugLine(FVector const& LineStart, FVector const& LineEnd, FColor const& Color, bool bPersistentLines, EShowFlag Type = EShowFlag::Debug);
	void DrawDebugLine(const TArray<FVector>& Vertices, const TArray<uint32>& Indices, FColor const& Color, bool bPersistentLines, EShowFlag Type = EShowFlag::Debug);

public:
	template <typename T>
	T* AddSceneComponent(UScene* targetScene)
	{
		// TODO: 액터 개념 도입되면 USceneComponent 대신 UActor로 교체
		static_assert(std::is_base_of<USceneComponent, T>::value, "T must derive from USceneComponent");

		UObject* obj = FObjectFactory::ConstructObject(T::GetClass());
		T* instance = Cast<T>(obj);

		targetScene->SceneComponents.push_back(instance);
		bTextLabelDirty = true;

		instance->OnComponentAdded();

		return instance;
	}

	template <typename T>
	T* AddSceneComponent()
	{
		return AddSceneComponent<T>(activeScene);
	}

	template <typename T>
	T* AddPermanentSceneComponent()
	{
		return AddSceneComponent<T>(permanentScene);
	}

	void RemoveSceneComponent(USceneComponent* toDespawn);

	void Update(float deltaTime);
	inline void InjectRenderer(URenderer* InRenderer) { Renderer = InRenderer; }
	void OnBeforeRender();

private:
	static UScene* activeScene;
	static UScene* permanentScene;
	URenderer* Renderer;

	// Batch Line Rendering
	ULineBatchComponent* LineBatcher;
	ULineBatchComponent* PersistentLineBatcher;
	ULineBatchComponent* ForegroundLineBatcher;

public:
	bool bTextLabelDirty = true;
};

UWorld& GetWorld();