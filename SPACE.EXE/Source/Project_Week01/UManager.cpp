#include "UManager.h"
#include "UPrimitive.h"
#include "Probe.h"
#include "USphere.h"
#include "datatype.h"
#include "USoundManager.h"
#include "MainState.h"
#include "InGameReadyState.h"
#include "InGameRunState.h"
#include "EndingState.h"
#include "LoadingState.h"
#include "BootState.h"

void UManager::ClearGameObjects()
{
	if (Player)
	{ 
		delete Player; 
		Player = nullptr;
	}

	PlanetList.clear();

	//	Reserve size
	PlanetList.reserve(PlanetListReservedSize);
}

void UManager::BootGame(ID3D11Device * device, ID3D11DeviceContext * deviceContext)
{
	if (CurRunState != ERunstate::ERS_Boot)
	{
		//	TODO : Make an error log
		return;
	}
	
	//	1. Local Score 및 ResourceManager 로드
	//LoadScore(); //-> EndingState로 이동
	ResourceManager = new UResourceManager();
	ResourceManager->Initialize(device, deviceContext);

	InputManager = new PlayerInput();

	//	2. 스테이지 정보 생성
	// Stage 1 (기본)
	FStageInfo stage1 = { EStage::ES_Stage1, 30.f };
	stage1.ObstacleList.push_back({ 0, FVector(-0.2, 0.0f, 0.0f) });
	StageInfoList.push_back(stage1);

	// Stage 2 (슬라롬)
	FStageInfo stage2 = { EStage::ES_Stage2, 45.f };
	stage2.ObstacleList.push_back({ 0, FVector(-0.2f, 0.0f, 0.0f) });
	stage2.ObstacleList.push_back({ 0, FVector(0.0f, 0.0f, 0.0f) });
	StageInfoList.push_back(stage2);

	// Stage 3 (U턴)
	FStageInfo stage3 = { EStage::ES_Stage3, 60.f };
	stage3.ObstacleList.push_back({ 0, FVector(-0.7f, 0.0f, 0.0f) });
	StageInfoList.push_back(stage3);

	//	3. 메인 State로 분기

}

void UManager::CreateNewPlanetWorld(USphere& in)
{
	PlanetList.push_back(in);	//	이후에 Reference
}

//	Public Functions
void UManager::Initialize(HWND hwnd) // 사운드 초기화
{
	m_SoundMgr.Initialize(hwnd);
	m_SoundMgr.LoadBGM(EBGM::EBGM_TitleScreen, "Sound/TitleScreen.wav");
	m_SoundMgr.LoadBGM(EBGM::EBGM_Level1, "Sound/Level1.wav");
	m_SoundMgr.LoadBGM(EBGM::EBGM_Level2, "Sound/Level2.wav");
	m_SoundMgr.LoadBGM(EBGM::EBGM_Level3, "Sound/Level3.wav");
	
	m_SoundMgr.LoadSFX(ESFX::ESFX_MouseClick, "Sound/MouseClick.wav", 5);
	m_SoundMgr.LoadSFX(ESFX::ESFX_Clear, "Sound/Clear.wav", 5);
	m_SoundMgr.LoadSFX(ESFX::ESFX_Fail, "Sound/Fail.wav", 5);

	m_SoundMgr.SetBGMVolume(0.9f); // 볼륨 조절(0.0f ~ 1.0f)
}

void UManager::OnMouseClick()
{
	// 1. 현재 상태에게 클릭 이벤트를 먼저 알립니다.
	if (m_pCurrentState) m_pCurrentState->OnMouseClick(this);

	// 2. 공통 효과음 재생
	m_SoundMgr.PlaySFX(ESFX::ESFX_MouseClick);
}

UManager::UManager(ID3D11Device* device, ID3D11DeviceContext * deviceContext)
	: CurRunState(ERunstate::ERS_Boot), CurStage(EStage::ES_None), CurAvailableStage(EStage::ES_Stage1), ResourceManager(nullptr), Score(0.f)
{
	BootGame(device , deviceContext);

	ProbeResource.GenerateSphere(0.2f);
	SphereResource.GenerateSphere(1.0f);
	GoalResource.GenerateSphere(0.1f);

	PlanetList.reserve(PlanetListReservedSize);
}

MeshResource* UManager::getProbeResource() 
{	
	return &ProbeResource;
}

MeshResource* UManager::getSphereResource()
{	
	return &SphereResource;
}
MeshResource* UManager::getGoalResource()
{
	return &GoalResource;
}
void UManager::setProbeResource( MeshResource& mr)
{
	this->ProbeResource = mr;
}

void UManager::setGoalResource(MeshResource& mr)
{
	this->GoalResource = mr;

}
void UManager::setSphereResource( MeshResource& mr)
{
	this->SphereResource = mr;
}



void UManager::Release()
{
	// 1. 사운드 자원 해제 (DirectSound 인터페이스 및 버퍼 정리)
	m_SoundMgr.Dispose();

	// 2. 게임 데이터 정리 및 저장 (점수 저장 등 기존 로직)
	//ShutDownGame();
}


void MeshResource::GenerateTriangle()
{
	// FTextureVertex 구조: { x, y, z,  r, g, b, a,  u, v }
	Vertices = {
		// 0: 왼쪽 위 (Left-Top)
		{ -0.3f,  0.3f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f,  0.0f, 0.0f },

		// 1: 오른쪽 위 (Right-Top)
		{  0.3f,  0.3f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f,  1.0f, 0.0f },

		// 2: 오른쪽 아래 (Right-Bottom)
		{  0.3f, -0.3f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f,  1.0f, 1.0f },

		// 3: 왼쪽 아래 (Left-Bottom)
		{ -0.3f, -0.3f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f,  0.0f, 1.0f }
	};

	// 시계 방향(CW)으로 인덱스 설정
	Indexes = {
	0, 1, 2,
	0, 2, 3
	};

	VertexCount = static_cast<UINT>(Vertices.size());
	IndexCount = static_cast<UINT>(Indexes.size());
}

void MeshResource::GenerateSphere(float radius)
{
	// 원을 구성할 삼각형의 개수 (높을수록 부드러워짐)
	const uint32_t segmentCount = 50;

	Vertices.clear();
	Indexes.clear();

	// 1. 중심 정점 생성 (UV: 0.5, 0.5)
	FTextureVertex center;
	center.x = 0.0f; center.y = 0.0f; center.z = 0.0f;
	center.r = 1.0f; center.g = 1.0f; center.b = 1.0f; center.a = 1.0f;
	center.u = 0.5f; center.v = 0.5f;
	Vertices.push_back(center);

	// 2. 테두리 정점 생성
	for (uint32_t i = 0; i <= segmentCount; ++i)
	{
		float theta = 2.0f * 3.1415926535f * (float)i / segmentCount;

		float cosT = cosf(theta);
		float sinT = sinf(theta);

		FTextureVertex v;
		v.x = radius * cosT;
		v.y = radius * sinT;
		v.z = 0.0f;

		v.r = 1.0f; v.g = 1.0f; v.b = 1.0f; v.a = 1.0f;

		// UV 좌표: 중심(0.5, 0.5)을 기준으로 -1~1 범위를 0~1로 변환
		// cosT는 -1~1이므로 0.5를 곱하고 0.5를 더함
		v.u = cosT * 0.5f + 0.5f;
		v.v = 1.0f - (sinT * 0.5f + 0.5f); // DirectContext 좌표계에 맞게 Y(V) 반전

		Vertices.push_back(v);
	}

	// 3. 인덱스 생성 (중심점을 포함한 삼각형 리스트)
	for (uint32_t i = 1; i <= segmentCount; ++i)
	{
		Indexes.push_back(0);         // 중심점
		Indexes.push_back(i + 1);     // 다음 테두리 점
		Indexes.push_back(i);         // 현재 테두리 점
	}

	VertexCount = static_cast<UINT>(Vertices.size());
	IndexCount = static_cast<UINT>(Indexes.size());
}