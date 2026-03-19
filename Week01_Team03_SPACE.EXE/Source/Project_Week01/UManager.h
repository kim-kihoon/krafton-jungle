#pragma once

#include <fstream>
#include <vector>
#include <algorithm>
#include <string>
#include <sstream>
#include <random>

#include <d3d11.h>
#include <d3dcompiler.h>

#include "Probe.h"
#include "datatype.h"
#include "USoundManager.h"
#include "UPrimitive.h"
#include "USphere.h"
#include "UResourceManager.h"
#include "PlayerInput.h"
#include "IState.h"
#include "Goal.h"


//	Constants
constexpr float GameTopBorder = -1.f;
constexpr float GameBottomBorder = 1.f;
constexpr float GameLeftBorder = -1.f;
constexpr float GameRightBorder = 0.75f;

constexpr size_t PlanetListReservedSize = 50;
constexpr float GravitationalConstant = 0.1f;

#pragma region __GAME_STATE__

//	None 상태를 갖지 않음 (MECE)
enum class ERunstate
{
	ERS_Boot,
	ERS_Main,
	ERS_StageSelect,
	ERS_Loading,
	ERS_InGameReady,
	ERS_InGameRun,
	ERS_Ending,	//	플레이어의 사망 || 게임 클리어
	ERS_Destroy	//	Destroy process가 끝나면 실행됨
};

#pragma endregion

#pragma region __STAGE_INFO__

enum class EStage
{
	ES_None,
	ES_Stage1,
	ES_Stage2,
	ES_Stage3,
};

//	스테이지에 대한 정보 저장
struct FStageInfo
{
	EStage StageName;
	float MaxTime;	//	Seconds

	//	1. 행성의 종류(idx 구분)와 개수에 대한 정보
	std::vector<std::pair<int, int>> CreatablePlanetList; //인덱스와 개수 정보

	//	2. 장애물 위치 및 종류(idx), 개수 정보 -> 위치, 개수를 pair (혹은 개수만)
	std::vector<std::pair<int, FVector>> ObstacleList; //인덱스와 위치 정보

	//	3. 탈출구 객체 위치
	Goal goal;

};

#pragma endregion


enum RESOURCE_TYPE
{
	PROBE,
	SPHERE
};

struct MeshResource {
	ID3D11Buffer* VB = nullptr;      // Vertex Buffer
	ID3D11Buffer* IB = nullptr;      // Index Buffer 
	std::vector<FTextureVertex> Vertices;
	std::vector<unsigned int> Indexes;
	unsigned int VertexCount = 0;    // 정점 개수
	unsigned int IndexCount = 0;     // 인덱스 개수 (Draw 호출 시 필수)
	unsigned int Stride = sizeof(FTextureVertex);         // 정점 1개의 크기 (sizeof(FVertex))
	float Scale = 0;

	MeshResource& operator=(const MeshResource& mr)
	{
		VB = mr.VB;
		IB = mr.IB;
		VertexCount = mr.VertexCount;
		IndexCount = mr.IndexCount;
		Stride = mr.Stride;
		Vertices = mr.Vertices;
		return *this;
	}
	void GenerateSphere(float radius = 1.0f);
	void GenerateTriangle();


};

class UManager
{
private:
	IState* m_pCurrentState = nullptr;
	
	//	Game State
	ERunstate CurRunState;
	EStage CurStage;
	EStage CurAvailableStage;	//	선택 가능한 스테이지

	//	Time
	float RemainTimer;
	
	//	Player Runtime Data
	float Score;
	std::string PlayerName;

	//  Sound
	USoundManager m_SoundMgr;

	/* GameObjects */
	Probe* Player;
	std::vector<USphere> PlanetList;	//	이후에 template 수정할 수도 있음
	Goal* goal;


	/* Game Data */
	std::vector<FStageInfo> StageInfoList;
	
	/* Other Managers */
	UResourceManager* ResourceManager;
	

	/* Player Inputs */
	PlayerInput* InputManager;

	//MeshResource
	MeshResource ProbeResource;
	MeshResource SphereResource;
	MeshResource GoalResource;

	bool success; // 엔딩으로 넘어갈때 성공 여부

public:
	UManager(ID3D11Device* device, ID3D11DeviceContext*);

	~UManager()
	{
		m_SoundMgr.Dispose();
		//ShutDownGame();
	}
	void BootGame(ID3D11Device* device, ID3D11DeviceContext* deviceContext);
	void Initialize(HWND hwnd);
	void Release();
	void OnMouseClick();

	//	새로운 행성 생성 (Invoke from PlanetPlacementManager)	  
	void CreateNewPlanetWorld(USphere& in);
	std::vector<USphere>& GetPlanetList() { return PlanetList; }

	bool GetSuccess() { return success; }
	void SetSuccess(int _success) { success = _success; }

	// StageInfo 관련
	EStage GetCurAvailableStage() const { return CurAvailableStage; }
	EStage GetCurStage() const { return CurStage; }
	void SetCurStage(EStage stage) { CurStage = stage; }
	void SetCurAvailableStage(EStage stage) { CurAvailableStage = stage; }
	int GetCurStageInt() const { return (int)CurStage - 1; }
	const std::vector<FStageInfo>& GetStageInfoList() const { return StageInfoList; }
	
	// Remain Timer 관련
	float GetRemainTimer() const { return RemainTimer; }
	void SetRemainTimer(float time) { RemainTimer = time; }
    
	Probe* GetProbe() const { return Player; }
	void SetPlayer(Probe* p) { Player = p; }

	Goal* getGoal() { return goal; };
	void SetGoal(Goal* g) { goal = g; };

	MeshResource* getSphereResource();
	void setSphereResource(MeshResource& mr);
	MeshResource* getProbeResource();
	void setProbeResource(MeshResource& mr);
	MeshResource* getGoalResource();
	void setGoalResource(MeshResource& mr);

	std::string GetPlayerName() const { return PlayerName; }
	void SetPlayerName(const std::string& name) { PlayerName = name; }
	int GetScore() const { return Score; }

	UResourceManager* GetResourceManager() { return ResourceManager; }
	PlayerInput* GetInputManager() { return InputManager; }

	void ClearGameObjects();

	// Sound 관련
	void PlayBGM(EBGM bgm) { m_SoundMgr.PlayBGM(bgm); }
	void PlaySFX(ESFX sfx) { m_SoundMgr.PlaySFX(sfx); }
};



