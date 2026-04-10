#include "RenderCollector.h"
#include "Component/UUIDBillboardComponent.h"
#include "Renderer/RenderCommand.h"
#include "Actor/Actor.h"
#include "Actor/CameraActor.h"
#include "Component/StaticMeshComponent.h"
#include "Component/SubUVComponent.h"
#include "Core/Engine.h"
#include "Component/TextRenderComponent.h"
#include "Debug/EngineLog.h"
#include "Renderer/Renderer.h"
#include "Renderer/TextMeshBuilder.h"
#include "Renderer/SubUVRenderer.h"
#include "Renderer/Material.h"
#include "Renderer/MaterialManager.h"
#include "Renderer/MeshData.h"
#include "Component/BillboardComponent.h"
#include "Component/CameraArrowComponent.h"

void FLevelRenderCollector::CollectRenderCommands(const TArray<AActor*>& Actors, const FFrustum& Frustum,
	const FShowFlags& ShowFlags, const FVector& CameraPosition, FRenderCommandQueue& OutQueue)
{
	// ⭐ UActorComponent가 아니라 UPrimitiveComponent로 바로 받습니다!
	TArray<UPrimitiveComponent*> VisiblePrimitives;
	FrustrumCull(Actors, Frustum, ShowFlags, VisiblePrimitives);

	FRenderer* Renderer = GEngine ? GEngine->GetRenderer() : nullptr;
	if (!Renderer) return;

	FTextMeshBuilder& TextRenderer = Renderer->GetTextRenderer();
	FSubUVRenderer& SubUVRenderer = Renderer->GetSubUVRenderer();

	const FMatrix ViewInverse = OutQueue.ViewMatrix.GetInverse();
	const FVector CameraForward = ViewInverse.GetForwardVector();
	const bool bIsOrthographic = std::abs(OutQueue.ProjectionMatrix[3][3] - 1.0f) < 0.0001f;

	for (UPrimitiveComponent* Comp : VisiblePrimitives)
	{
		if (!Comp) continue;

		// ─── 1. 텍스트 컴포넌트 ───
		if (Comp->IsA(UTextRenderComponent::StaticClass()))
		{
			UTextRenderComponent* TextComp = static_cast<UTextRenderComponent*>(Comp);
			FRenderMesh* TextMesh = TextComp->GetRenderMesh();

			if (TextMesh)
			{
				bool bBuilt = false;
				if (TextComp->IsTextMeshDirty())
				{
					bBuilt = TextRenderer.BuildTextMesh(TextComp->GetDisplayText(), *TextMesh);
					if (bBuilt)
					{
						TextMesh->bIsDirty = true;
						TextComp->ClearTextMeshDirty();
					}
				}

				if (!TextMesh->Vertices.empty())
				{
					FMaterial* FontMat = TextRenderer.GetFontMaterial();
					if (FontMat)
					{
						FVector4 Color = TextComp->GetTextColor();
						FontMat->SetParameterData("TextColor", &Color, 16);

						FRenderCommand Command;
						Command.RenderMesh = TextMesh;
						Command.Material = FontMat;

						if (!Comp->IsA(UUUIDBillboardComponent::StaticClass()))
						{
							Command.RenderLayer = ERenderLayer::Default;
						}
						else
						{
							Command.RenderLayer = ERenderLayer::Overlay;
						}

						const FVector WorldPos = TextComp->GetRenderWorldPosition();
						const FVector Scale = TextComp->GetRenderWorldScale();

						if (TextComp->IsBillboard())
						{
							if (bIsOrthographic)
							{
								Command.WorldMatrix = FMatrix::MakeScale(Scale) * FMatrix::MakeBillboardFromForward(WorldPos, CameraForward);
							}
							else
							{
								Command.WorldMatrix = FMatrix::MakeScale(Scale) * FMatrix::MakeBillboard(WorldPos, CameraPosition);
							}
						}
						else
						{
							const float TextScale = TextComp->GetTextScale();
							Command.WorldMatrix =
								FMatrix::MakeScale(FVector(TextScale, TextScale, TextScale)) *
								TextComp->GetWorldTransform();
						}

						OutQueue.AddCommand(Command);
					}
				}
			}
			continue;
		}

		// ─── 2. SubUV 스프라이트 컴포넌트 ───
		if (Comp->IsA(USubUVComponent::StaticClass()))
		{
			USubUVComponent* SubUVComponent = static_cast<USubUVComponent*>(Comp);
			FRenderMesh* SubUVMesh = SubUVComponent->GetSubUVMesh();
			if (SubUVMesh && SubUVRenderer.BuildSubUVMesh(SubUVComponent->GetSize(), *SubUVMesh))
			{
				SubUVMesh->bIsDirty = true;
				float TotalTime = GEngine ? static_cast<float>(GEngine->GetTimer().GetTotalTime()) : 0.0f;
				SubUVRenderer.UpdateAnimationParams(
					SubUVComponent->GetColumns(), SubUVComponent->GetRows(), SubUVComponent->GetTotalFrames(),
					SubUVComponent->GetFirstFrame(), SubUVComponent->GetLastFrame(),
					SubUVComponent->GetFPS(), TotalTime, SubUVComponent->IsLoop()
				);

				FMaterial* SubUVMat = SubUVRenderer.GetSubUVMaterial();
				if (SubUVMat)
				{
					FRenderCommand Command;
					Command.RenderMesh = SubUVMesh;
					Command.Material = SubUVMat;
					Command.WorldMatrix = SubUVComponent->GetWorldTransform();

					if (SubUVComponent->IsBillboard())
					{
						const FVector WorldPos = Command.WorldMatrix.GetTranslation();
						const FVector Scale = Command.WorldMatrix.GetScaleVector();
						if (bIsOrthographic)
						{
							Command.WorldMatrix = FMatrix::MakeScale(Scale) * FMatrix::MakeBillboardFromForward(WorldPos, CameraForward);
						}
						else
						{
							Command.WorldMatrix = FMatrix::MakeScale(Scale) * FMatrix::MakeBillboard(WorldPos, CameraPosition);
						}
					}

					OutQueue.AddCommand(Command);
				}
			}
			continue;
		}

		// ─── 3. 정적 메쉬 컴포넌트 (과거 프리미티브 대통합) ───
		if (Comp->IsA(UStaticMeshComponent::StaticClass()))
		{
			UStaticMeshComponent* SMC = static_cast<UStaticMeshComponent*>(Comp);
			FRenderMesh* TargetMesh = SMC->GetRenderMesh();

			if (TargetMesh)
			{
				int32 NumSections = TargetMesh->GetNumSection();
				if (NumSections <= 0)
				{
					FRenderCommand Command;
					Command.RenderMesh = TargetMesh;
					Command.WorldMatrix = SMC->GetWorldTransform();
					std::shared_ptr<FMaterial> MatPtr = SMC->GetMaterial(0);
					Command.Material = MatPtr ? MatPtr.get() : Renderer->GetDefaultMaterial();

					OutQueue.AddCommand(Command);
				}
				else
				{
					for (int32 i = 0; i < NumSections; ++i)
					{
						const FMeshSection& Section = TargetMesh->Sections[i];

						FRenderCommand Command;
						Command.RenderMesh = TargetMesh;
						Command.WorldMatrix = SMC->GetWorldTransform();

						Command.IndexStart = Section.StartIndex;
						Command.IndexCount = Section.IndexCount;

						// 2. 인덱스(i)에 맞는 머티리얼을 꺼내서 주문서에 붙이기
						std::shared_ptr<FMaterial> MatPtr = SMC->GetMaterial(i);
						Command.Material = MatPtr ? MatPtr.get() : Renderer->GetDefaultMaterial();
						OutQueue.AddCommand(Command);
					}
				}
			}
			continue;
		}

		// ─── 4. 카메라 방향 화살표 컴포넌트 ───
		if (Comp->IsA(UCameraArrowComponent::StaticClass()))
		{
			UCameraArrowComponent* ArrowComp = static_cast<UCameraArrowComponent*>(Comp);
			FDynamicMesh* ArrowMesh = ArrowComp->GetArrowMesh();
			if (ArrowMesh && !ArrowMesh->Vertices.empty())
			{
				auto GizmoMat = FMaterialManager::Get().FindByName("M_Gizmos");
				FRenderCommand Command;
				Command.RenderMesh = ArrowMesh;
				Command.Material = GizmoMat ? GizmoMat.get() : Renderer->GetDefaultMaterial();
				Command.WorldMatrix = ArrowComp->GetWorldTransform();
				OutQueue.AddCommand(Command);
			}
			continue;
		}

		if (Comp->IsA(UBillboardComponent::StaticClass()))
		{
			UBillboardComponent* BillboardComp = static_cast<UBillboardComponent*>(Comp);
			FRenderMesh* BillboardMesh = BillboardComp->GetBillboardMesh();

			// SubUVRenderer 를 안 사용하는게 맞는데 우선 테스트 용으로 재사용
			if (BillboardMesh && SubUVRenderer.BuildSubUVMesh(BillboardComp->GetSize(), *BillboardMesh))
			{
				BillboardMesh->bIsDirty = true;
				FMaterial* BillboardMat = BillboardComp->GetMaterialInstance();
				if (BillboardMat)
				{
					FRenderCommand Command;
					Command.RenderMesh = BillboardMesh;
					Command.Material = BillboardMat;
					Command.WorldMatrix = BillboardComp->GetWorldTransform();
					if (BillboardComp->IsBillboard())
					{
						const FVector WorldPos = Command.WorldMatrix.GetTranslation();
						const FVector Scale = Command.WorldMatrix.GetScaleVector();
						if (bIsOrthographic)
						{
							Command.WorldMatrix = FMatrix::MakeScale(Scale) * FMatrix::MakeBillboardFromForward(WorldPos, CameraForward);
						}
						else
						{
							Command.WorldMatrix = FMatrix::MakeScale(Scale) * FMatrix::MakeBillboard(WorldPos, CameraPosition);
						}
					}
					OutQueue.AddCommand(Command);
				}
			}
			continue;
		}
	}
}

void FLevelRenderCollector::FrustrumCull(const TArray<AActor*>& Actors, const FFrustum& Frustum,
	const FShowFlags& ShowFlags, TArray<UPrimitiveComponent*>& OutVisible)
{
	for (AActor* Actor : Actors)
	{
		if (!Actor || Actor->IsPendingDestroy() || !Actor->IsVisible()) continue;
		if (!Actor->IsVisible()) continue;

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (!Component->IsA(UPrimitiveComponent::StaticClass())) continue;

			UPrimitiveComponent* PrimitiveComponent = static_cast<UPrimitiveComponent*>(Component);

			const bool bIsUUID = PrimitiveComponent->IsA(UUUIDBillboardComponent::StaticClass());
			const bool bIsSubUV = PrimitiveComponent->IsA(USubUVComponent::StaticClass());
			const bool bIsText = PrimitiveComponent->IsA(UTextRenderComponent::StaticClass());
			const bool bIsCameraArrow = PrimitiveComponent->IsA(UCameraArrowComponent::StaticClass());
			// ─── ShowFlags에 따른 필터링 ───
			if (bIsUUID)
			{
				if (!ShowFlags.HasFlag(EEngineShowFlags::SF_UUID)) continue;

				// [수정]: PIE 중에는 카메라 액터의 UUID만 특별히 숨긴다.
				if (Actor->GetWorld()->GetWorldType() == EWorldType::PIE)
				{
					if (Actor->IsA(ACameraActor::StaticClass()))
					{
						continue;
					}
				}
			}
			else if (bIsCameraArrow)
			{
				// 에디터 전용 – PIE(SF_EditorActorVisualization 꺼짐)에서는 스킵
				if (!ShowFlags.HasFlag(EEngineShowFlags::SF_EditorActorVisualization)) continue;
				if (!PrimitiveComponent->GetRenderMesh()) continue;
			}
			else if (bIsSubUV)
			{
				if (!ShowFlags.HasFlag(EEngineShowFlags::SF_Billboard))
				{
					continue;
				}
			}
			else if (bIsText)
			{
				if (!ShowFlags.HasFlag(EEngineShowFlags::SF_Text))
				{
					continue;
				}
			}
			else
			{
				// Billboard 중 에디터 전용(카메라 아이콘 등)은 별도 플래그로 필터링
				if (PrimitiveComponent->IsA(UBillboardComponent::StaticClass()))
				{
					const UBillboardComponent* BB = static_cast<const UBillboardComponent*>(PrimitiveComponent);
					if (BB->IsEditorOnly())
					{
						if (!ShowFlags.HasFlag(EEngineShowFlags::SF_EditorActorVisualization)) continue;
					}
					else
					{
						if (!ShowFlags.HasFlag(EEngineShowFlags::SF_Billboard)) continue;
					}
				}
				else
				{
					if (!ShowFlags.HasFlag(EEngineShowFlags::SF_Primitives)) continue;
				}
				if (!PrimitiveComponent->GetRenderMesh()) continue;
			}

			if (Frustum.IsVisible(PrimitiveComponent->GetWorldBounds()))
			{
				OutVisible.push_back(PrimitiveComponent);
			}
		}
	}
}
