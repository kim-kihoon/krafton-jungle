#include "World.h"
#include "ObjectFactory.h"
#include "Component/CameraComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/TextBatchComponent.h"
#include "Component/GizmoComponent.h"
#include "EngineStatics.h"
#include "SerializeHelper.h"
#include <windows.h>

UScene* UWorld::activeScene;
UScene* UWorld::permanentScene;

UWorld::UWorld() 
{
	permanentScene = Cast<UScene>(FObjectFactory::ConstructObject(UScene::GetClass()));

	LineBatcher = Cast<ULineBatchComponent>(AddPermanentSceneComponent<ULineBatchComponent>());
	PersistentLineBatcher = Cast<ULineBatchComponent>(AddPermanentSceneComponent<ULineBatchComponent>());
	ForegroundLineBatcher = Cast<ULineBatchComponent>(FObjectFactory::ConstructObject(ULineBatchComponent::GetClass()));
}

ULineBatchComponent* UWorld::GetLineBatcher(bool bPersistentLines, uint8_t DepthPriority) const
{
	// 1. 우선순위가 높으면 Foreground 전용 배처
	if (DepthPriority > 0) // 예: 특정 값 이상은 최상단 출력
	{
		return ForegroundLineBatcher;
	}

	// 2. 지속 선(Persistent) 여부에 따라 선택
	return bPersistentLines ? PersistentLineBatcher : LineBatcher;
}

UWorld::~UWorld() {
	UnloadScene();

	// permanentScene 정리
	FLevel::GetInstance().Clear();
	for (auto it = permanentScene->SceneComponents.begin(); it != permanentScene->SceneComponents.end(); it++)
	{
		delete *it;
	}
	permanentScene->SceneComponents.clear();

	delete ForegroundLineBatcher;
	ForegroundLineBatcher = nullptr;

	delete permanentScene;
	permanentScene = nullptr;
}

UScene* UWorld::GetActiveScene()
{
	return activeScene;
}

void UWorld::SetActiveScene(UScene* scene)
{
	activeScene = scene;
}

UScene* UWorld::NewScene()
{
	if (GetActiveScene() != nullptr)
		UnloadScene();

	UScene* newScene = Cast<UScene>(FObjectFactory::ConstructObject(UScene::GetClass()));
	SetActiveScene(newScene);
	//UEngineStatics::NextUUID = 0;

	//UE_LOG("Created new scene\n");
	return GetActiveScene();
}

void UWorld::SaveScene(FString sceneName)
{
	UScene* activeScene = GetWorld().GetActiveScene();

	activeScene->NextUUID = UEngineStatics::NextUUID;

	FWString wSceneName(sceneName.begin(), sceneName.end());

	OPENFILENAME ofn;
	wchar_t szFile[260] = { 0 };

	if (!sceneName.empty())
	{
		wcscpy_s(szFile, MAX_PATH, wSceneName.c_str());
	}

	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = L"Scene Files\0*.Scene\0All Files\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrDefExt = L"Default";
	ofn.lpstrFileTitle = nullptr;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = nullptr;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

	if (GetSaveFileName(&ofn) == TRUE)
	{
		FWString filePath = ofn.lpstrFile;
		SerializeHelper::SaveAsJson(activeScene, filePath);
	}

	return;
}

UScene* UWorld::LoadScene()
{
	FWString sceneName;

	OPENFILENAME ofn;
	wchar_t szFile[260] = { 0 };

	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = L"Scene Files\0*.Scene\0All Files\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = nullptr;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = nullptr;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

	if (GetSaveFileName(&ofn) == TRUE)
	{
		sceneName = ofn.lpstrFile;
	}
	
	UScene* loadedScene = SerializeHelper::LoadFromJson<UScene>(sceneName);

	if (loadedScene != nullptr)
	{
		if (GetActiveScene() != nullptr)
			UnloadScene();
		SetActiveScene(loadedScene);
		UEngineStatics::NextUUID = loadedScene->NextUUID;
	}

	return loadedScene;
}

UScene* UWorld::LoadScene(FWString& outSceneName)
{
	FWString sceneName;

	OPENFILENAME ofn;
	wchar_t szFile[260] = { 0 };

	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = L"Scene Files\0*.Scene\0All Files\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = nullptr;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = nullptr;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

	if (GetSaveFileName(&ofn) == TRUE)
	{
		wchar_t* fileNameOnly = ofn.lpstrFile + ofn.nFileOffset;
		sceneName = ofn.lpstrFile;
		outSceneName = FWString(fileNameOnly);
	}

	UScene* loadedScene = SerializeHelper::LoadFromJson<UScene>(sceneName);

	if (loadedScene != nullptr)
	{
		if (GetActiveScene() != nullptr)
			UnloadScene();
		SetActiveScene(loadedScene);
		UEngineStatics::NextUUID = loadedScene->NextUUID;
	}

	return loadedScene;
}

void UWorld::UnloadScene()
{
	UScene* curScene = GetWorld().GetActiveScene();

	// loop 내에서의 삽입/삭제로 인한 오류 방지
	auto deleteList = curScene->SceneComponents;
	for (auto obj : deleteList)
	{
		RemoveSceneComponent(obj);
	}
	delete GetActiveScene();
	SetActiveScene(nullptr);
}

void UWorld::DrawDebugLine(FVector const& LineStart, FVector const& LineEnd, FColor const& Color, bool bPersistentLines, EShowFlag Type)
{
	LineBatcher->DrawLine(LineStart, LineEnd, Color, Type);
}

void UWorld::DrawDebugLine(const TArray<FVector>& Vertices, const TArray<uint32>& Indices, FColor const& Color, bool bPersistentLines, EShowFlag Type)
{
	// 1. 안전장치: 인덱스 배열이 비어있거나, 홀수 개면 선(시작점-끝점 짝)을 만들 수 없으므로 종료합니다.
	if (Indices.empty() || Indices.size() % 2 != 0)
	{
		return;
	}

	// 2. 인덱스 배열을 2개씩 건너뛰며 순회합니다. (0, 2, 4, 6...)
	for (size_t i = 0; i < Indices.size(); i += 2)
	{
		uint32 StartIdx = Indices[i];
		uint32 EndIdx = Indices[i + 1];

		// 3. 안전장치: 인덱스가 실제 정점 배열의 크기를 벗어나면(Out of Bounds) 크래시가 나므로 방어합니다.
		if (StartIdx < Vertices.size() && EndIdx < Vertices.size())
		{
			// 4. 추출한 두 정점을 이용해 기존의 DrawDebugLine을 호출합니다.
			// (참고: 이전 코드에서 쓰시던 LifeTime, DepthPriority 등의 매개변수가 있다면 여기에 맞춰서 넣어주시면 됩니다)
			DrawDebugLine(Vertices[StartIdx], Vertices[EndIdx], Color, bPersistentLines, Type);
		}
	}
}

void UWorld::RemoveSceneComponent(USceneComponent* toDespawn)
{
	// Remove render object first
	auto primToDespawn = Cast<UPrimitiveComponent>(toDespawn);
	if (primToDespawn != nullptr)
	{
		TArray<RenderObject*> renderObjs = primToDespawn->GetRenderObjects();
		for (auto& renderObj : renderObjs)
		{
			FLevel::GetInstance().UnregisterRenderObject(renderObj);
		}
	}

	bTextLabelDirty = true;

	for (auto it = activeScene->SceneComponents.begin(); it != activeScene->SceneComponents.end(); it++)
	{
		if ((*it) == toDespawn)
		{
			delete toDespawn;
			activeScene->SceneComponents.erase(it);
			break;
		}
	}
}

void UWorld::Update(float deltaTime)
{
	permanentScene->Update(deltaTime);
	// TODO: 멀티 씬 구조에서 for each scene 루프 중첩 추가
	activeScene->Update(deltaTime);
}

void UWorld::OnBeforeRender()
{
	// TODO: 멀티 씬 구조에서 for each scene 루프 중첩 추가
	auto sceneComponents = permanentScene->SceneComponents;
	for (USceneComponent* s : sceneComponents)
	{
		UPrimitiveComponent* primitive = Cast<UPrimitiveComponent>(s);
		if (primitive != nullptr)
		{
			// TODO: Check each render object dirty flag instead of whole primitive
			TArray<RenderObject*> renderObjs = primitive->GetRenderObjects();
			for (auto& renderObj : renderObjs)
			{
				if (renderObj->bIsDirty)
					primitive->ResolveRenderStateDirty();
			}
		}
	}

	float AxisLength = 500.0f;

	// X축 (Red)
	DrawDebugLine(FVector(-AxisLength, 0, 0), FVector(AxisLength, 0, 0), FColor::Red(), false, EShowFlag::Axis);
	// Y축 (Green)
	DrawDebugLine(FVector(0, -AxisLength, 0), FVector(0, AxisLength, 0), FColor::Green(), false, EShowFlag::Axis);
	// Z축 (Blue)
	DrawDebugLine(FVector(0, 0, -AxisLength), FVector(0, 0, AxisLength), FColor::Blue(), false, EShowFlag::Axis);

	sceneComponents = activeScene->SceneComponents;
	for (USceneComponent* s : sceneComponents)
	{
		if (UPrimitiveComponent* primitive = Cast<UPrimitiveComponent>(s))
		{
			// TODO: Check each render object dirty flag instead of whole primitive
			TArray<RenderObject*> renderObjs = primitive->GetRenderObjects();
			for (auto& renderObj : renderObjs)
			{
				if (renderObj->bIsDirty)
					primitive->ResolveRenderStateDirty();
			}
		}
	}
}

UWorld& GetWorld()
{
	return UWorld::GetInstance();
}