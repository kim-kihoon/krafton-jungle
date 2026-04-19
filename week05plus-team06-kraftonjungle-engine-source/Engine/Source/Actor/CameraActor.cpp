#include "CameraActor.h"
#include "Component/BillboardComponent.h"
#include "Component/CameraArrowComponent.h"
#include "Primitive/PrimitiveGizmo.h"
#include "Core/Engine.h"
#include "Core/Paths.h"
#include "Renderer/Renderer.h"
#include "Renderer/Material.h"
#include "Renderer/MaterialManager.h"
#include "Component/CameraComponent.h"
#include "Object/ObjectFactory.h"
#include "Object/Class.h"

IMPLEMENT_RTTI(ACameraActor, AActor)

void ACameraActor::PostSpawnInitialize()
{
	CameraComponent = FObjectFactory::ConstructObject<UCameraComponent>(this, "CameraComponent");
	AddOwnedComponent(CameraComponent);
	SetRootComponent(CameraComponent);

	IconBillboard = FObjectFactory::ConstructObject<UBillboardComponent>(this, "CameraIconBillboard");
	AddOwnedComponent(IconBillboard);
	IconBillboard->AttachTo(CameraComponent);
	IconBillboard->SetSize(FVector2(0.5f, 0.5f));
	IconBillboard->SetEditorOnly(true);
	IconBillboard->ResetMaterial("M_BillboardIcon");

	if (GEngine)
	{
		if (FRenderer* Renderer = GEngine->GetRenderer())
		{
			std::filesystem::path TexPath = FPaths::ProjectRoot() / "Editor/Icon/PlayWorld.png";
			ID3D11ShaderResourceView* SRV = nullptr;
			if (Renderer->CreateTextureFromSTB(Renderer->GetDevice(), TexPath, &SRV))
			{
				auto MatTex = std::make_shared<FMaterialTexture>();
				MatTex->TextureSRV = SRV;
				IconBillboard->SetSpriteTexture(MatTex);
			}
		}
	}

	constexpr float ArrowScale = 0.02f;
	FTransform ArrowTransform;
	ArrowTransform.SetScale3D(FVector(ArrowScale, ArrowScale, ArrowScale));

	ArrowX = FObjectFactory::ConstructObject<UCameraArrowComponent>(this, "CameraArrowX");
	AddOwnedComponent(ArrowX);
	ArrowX->AttachTo(CameraComponent);
	ArrowX->SetRelativeTransform(ArrowTransform);
	ArrowX->SetArrowMesh(FPrimitiveGizmo::CreateTranslationAxisMesh(EAxis::X, FVector4(0.67f, 0.90f, 1.0f, 1.0f)));

	AActor::PostSpawnInitialize();
}

void ACameraActor::FixupReferences(const FDuplicateionContext& Context)
{
	AActor::FixupReferences(Context);

	if (this->CameraComponent)
	{
		this->CameraComponent = static_cast<UCameraComponent*>(Context.GetMappedObject(this->CameraComponent));
	}
	if (this->IconBillboard)
	{
		this->IconBillboard = static_cast<UBillboardComponent*>(Context.GetMappedObject(this->IconBillboard));
	}
	if (this->ArrowX)
	{
		this->ArrowX = static_cast<UCameraArrowComponent*>(Context.GetMappedObject(this->ArrowX));
	}
}
