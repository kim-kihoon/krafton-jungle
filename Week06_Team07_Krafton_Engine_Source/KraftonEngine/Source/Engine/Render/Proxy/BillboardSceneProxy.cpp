#include "Render/Proxy/BillboardSceneProxy.h"
#include "Component/BillboardComponent.h"
#include "Render/Resource/ShaderManager.h"
#include "Render/Pipeline/RenderBus.h"
#include "Render/Pipeline/RenderConstants.h"
#include "Engine/Runtime/Engine.h"
#include "Editor/EditorEngine.h"
#include <algorithm>

// ============================================================
// FBillboardSceneProxy
// ============================================================
FBillboardSceneProxy::FBillboardSceneProxy(UBillboardComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	bPerViewportUpdate = true;
	bShowAABB = false;
	bShowOBB = false;
	// 텍스처가 있으면 Batcher 경로, 없으면 기본 Primitive 경로
	bBatcherRendered = (InComponent && InComponent->GetTexture() != nullptr);
}

UBillboardComponent* FBillboardSceneProxy::GetBillboardComponent() const
{
	return static_cast<UBillboardComponent*>(Owner);
}

// ============================================================
// UpdateMesh — SelectionMask를 위한 셰이더 및 섹션 설정
// ============================================================
void FBillboardSceneProxy::UpdateMesh()
{
	MeshBuffer = Owner->GetMeshBuffer();

	UBillboardComponent* Comp = GetBillboardComponent();
	const bool bHasTexture = (Comp && Comp->GetTexture() != nullptr);
	bBatcherRendered = bHasTexture;

	if (bHasTexture)
	{
		// 1. SelectionMask(아웃라인) 시 알파 컷오프를 지원하는 Billboard 셰이더 사용
		Shader = FShaderManager::Get().GetShader(EShaderType::Billboard);
		
		// 2. 렌더 패스는 Billboard로 설정 (기즈모 위 렌더링)
		Pass = ERenderPass::Billboard; 

		// 3. SelectionMask 패스에서 텍스처 바인딩을 위한 정보 설정
		SectionDraws.clear();
		FMeshSectionDraw DecalSection;
		DecalSection.DiffuseSRV = Comp->GetTexture()->SRV;
		DecalSection.DiffuseColor = { 1.f, 1.f, 1.f, 1.f };
		DecalSection.FirstIndex = 0;
		DecalSection.IndexCount = 6; // Quad(2 Triangles)
		DecalSection.bIsUVScroll = false;
		SectionDraws.push_back(DecalSection);
	}
	else
	{
		Shader = FShaderManager::Get().GetShader(EShaderType::Primitive);
		Pass = ERenderPass::Opaque;
	}
	UpdateSortKey();
}

void FBillboardSceneProxy::CollectEntries(FRenderBus& Bus)
{
	UBillboardComponent* Comp = GetBillboardComponent();
	if (!Comp) return;

	const FTextureResource* Texture = Comp->GetTexture();
	if (!Texture || !Texture->IsLoaded()) return;

	FBillboardEntry Entry = {};
	// UpdatePerViewport에서 거리 보정이 완료된 행렬을 사용
	Entry.PerObject = PerObjectConstants;
	Entry.Billboard.Texture = Texture;
	
	// BillboardBatcher 내부의 WorldScale 곱셈을 고려하여 원본 크기만 전달
	Entry.Billboard.Width   = Comp->GetWidth();
	Entry.Billboard.Height  = Comp->GetHeight();
	
	Bus.AddBillboardEntry(std::move(Entry));
}

void FBillboardSceneProxy::UpdatePerViewport(const FRenderBus& Bus)
{
	UBillboardComponent* Comp = GetBillboardComponent();
	bVisible = Comp->IsVisible();
	
	// PIE 모드일 경우 에디터 아이콘(빌보드)을 숨깁니다.
	if (UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine))
	{
		if (EditorEngine->IsPlayingInEditor())
		{
			bVisible = false;
		}
	}

	if (!bVisible) return;

	// ==========================================================
	// 카메라 정보 및 거리 계산
	// ==========================================================
	const FVector CameraPos = Bus.GetCameraPosition();
	const FVector BillboardPos = Comp->GetWorldLocation();
	const float Distance = (BillboardPos - CameraPos).Length();

	// 비율을 계산하는 내부 임계점
	const float BaseWorldScale = 1.0f;

	// 화면 크기 축소가 시작되는 타이밍 
	const float ScreenScaleFactor = 0.05f;

	const float MasterIconScale = 5.0f;

	float DistanceScale = BaseWorldScale;

	if (!Bus.IsOrtho())
	{
		float LimitScale = Distance * ScreenScaleFactor;

		// 0.0 ~ 1.0 사이에서 작동하는 완벽한 베이스 비율 계산
		float BaseRatio = std::min(BaseWorldScale, LimitScale);
		BaseRatio = std::max(0.0001f, BaseRatio);

		DistanceScale = BaseRatio * MasterIconScale;
	}
	else
	{
		DistanceScale = Bus.GetOrthoWidth() * 0.001f * BaseWorldScale * MasterIconScale;
	}

	// ==========================================================
	// 회전 계산 및 최종 월드 행렬 생성
	// ==========================================================
	FVector BillboardForward = Bus.GetCameraForward() * -1.0f;
	FMatrix RotMatrix;
	RotMatrix.SetAxes(BillboardForward, Bus.GetCameraRight() * -1.0f, Bus.GetCameraUp());

	// 완성된 스케일을 컴포넌트의 월드 스케일에 반영하지 않고 단위 스케일(1,1,1) 기준으로 계산합니다.
	// (아이콘이 부모 액터의 스케일에 따라 찌그러지거나 커지는 것을 방지)
	FVector FinalScale = FVector(1.0f, 1.0f, 1.0f) * DistanceScale;

	FMatrix BillboardMatrix = FMatrix::MakeScaleMatrix(FinalScale)
		* RotMatrix * FMatrix::MakeTranslationMatrix(BillboardPos);

	PerObjectConstants = FPerObjectConstants::FromWorldMatrix(BillboardMatrix);
	MarkPerObjectCBDirty();
}
