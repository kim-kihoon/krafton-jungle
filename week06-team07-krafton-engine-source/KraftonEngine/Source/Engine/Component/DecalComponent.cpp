#include "DecalComponent.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Serialization/Archive.h"
#include "Resource/ResourceManager.h"
#include "Core/ResourceTypes.h"
#include "Component/ArrowComponent.h"
#include "GameFramework/AActor.h"

IMPLEMENT_CLASS(UDecalComponent, UPrimitiveComponent)

void UDecalComponent::InitializeComponent()
{
	if (Owner != nullptr && ArrowComponent == nullptr)
	{
		ArrowComponent = Owner->AddComponent<UArrowComponent>();
		ArrowComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
		ArrowComponent->AttachToComponent(this);
	}
}

FMeshBuffer* UDecalComponent::GetMeshBuffer() const
{
	EMeshShape Shape = EMeshShape::Cube;
	return &FMeshBufferManager::Get().GetMeshBuffer(Shape);
}

FPrimitiveSceneProxy* UDecalComponent::CreateSceneProxy()
{
	// 렌더링 프록시 생성 직전에 리소스를 다시 한 번 갱신하여 누락을 방지합니다.
	SetTexture(TextureName);
	return new FDecalSceneProxy(this);
}

void UDecalComponent::Serialize(FArchive& Ar)
{
	UPrimitiveComponent::Serialize(Ar);
	Ar << TextureName;
	Ar << bHasFade;
	Ar << FadeInner;
	Ar << FadeOuter;

	if (Ar.IsLoading())
	{
		// 로드 완료 후 리소스 포인터 재연결
		SetTexture(TextureName);
	}
}

void UDecalComponent::PostDuplicate()
{
	UPrimitiveComponent::PostDuplicate();
	// 복제 직후에도 리소스를 재연결합니다.
	SetTexture(TextureName);
}

void UDecalComponent::SetTexture(const FName& InTextureName)
{
	// 이름이 같더라도 강제로 갱신을 시도하여 초기화 시점의 누락을 방지합니다.
	TextureName = InTextureName;
	CachedTexture = FResourceManager::Get().FindTexture(InTextureName);
	
	// 프록시가 이미 존재한다면 즉시 리소스를 갱신합니다.
	if (SceneProxy)
	{
		static_cast<FDecalSceneProxy*>(SceneProxy)->UpdateMaterial();
	}
	MarkProxyDirty(EDirtyFlag::Material);
}

void UDecalComponent::SetFade(bool bEnable, float Inner, float Outer)
{
	bHasFade = bEnable;
	FadeInner = Inner;
	FadeOuter = Outer;
	
	// 페이드 값 변경 시 프록시의 상수 버퍼 데이터를 갱신해야 함을 알립니다.
	MarkProxyDirty(EDirtyFlag::Transform); // Transform 플래그를 이용해 PerObject 데이터를 갱신하도록 유도
}

void UDecalComponent::SetFadeConstants(FDecalConstants& OutDecalConstants) const
{
	OutDecalConstants.FadeInner = bHasFade ? FadeInner : 1.0f;
	OutDecalConstants.FadeOuter = bHasFade ? FadeOuter : 1.0f;
	OutDecalConstants.bUseFade = bHasFade ? 1 : 0;
}

ID3D11ShaderResourceView* UDecalComponent::GetSRV() const
{
	// 만약 이름은 있는데 캐시된 리소스가 없다면 여기서 한 번 더 시도합니다. (Lazy Loading)
	if (!CachedTexture && TextureName != "None")
	{
		const_cast<UDecalComponent*>(this)->SetTexture(TextureName);
	}
	return CachedTexture ? CachedTexture->SRV : nullptr;
}

void UDecalComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UPrimitiveComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Texture", EPropertyType::Name, &TextureName });
	OutProps.push_back({ "Fade",  EPropertyType::ByteBool, &bHasFade });
	OutProps.push_back({ "Fade Inner",  EPropertyType::Float, &FadeInner, 0.0f, FadeOuter, 0.01f });
	OutProps.push_back({ "Fade Outer",  EPropertyType::Float, &FadeOuter, FadeInner, 1.0f, 0.01f });
}

void UDecalComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);

	if (strcmp(PropertyName, "Texture") == 0)
	{
		SetTexture(TextureName);
	}
}

uint64 UDecalComponent::CalculateOBBScreenPixels(const FMatrix& ViewProj, float ViewportWidth, float ViewportHeight)
{
	FVector Corners[8];
	WorldOBB.GetCorners(Corners);

	float NDCMinX = FLT_MAX, NDCMinY = FLT_MAX;
	float NDCMaxX = -FLT_MAX, NDCMaxY = -FLT_MAX;

	for (int i = 0; i < 8; ++i)
	{
		// World → Clip Space
		FVector4 Clip = ViewProj.TransformPositionWithW(Corners[i]);

		// 카메라 뒤에 있으면 스킵
		if (Clip.W <= 0.1f) continue;

		// ÷w → NDC
		float InvW = 1.f / Clip.W;
		float NDCx = Clip.X * InvW;
		float NDCy = -Clip.Y * InvW;; // Y 반전 (DX UV 방향)

		NDCMinX = std::min(NDCMinX, NDCx);
		NDCMinY = std::min(NDCMinY, NDCy);
		NDCMaxX = std::max(NDCMaxX, NDCx);
		NDCMaxY = std::max(NDCMaxY, NDCy);
	}

	// 화면 밖 클램핑
	NDCMinX = std::max(NDCMinX, -1.0f);
	NDCMinY = std::max(NDCMinY, -1.0f);
	NDCMaxX = std::min(NDCMaxX, 1.0f);
	NDCMaxY = std::min(NDCMaxY, 1.0f);

	if (NDCMaxX <= NDCMinX || NDCMaxY <= NDCMinY)
		return 0; // 화면 밖

	// NDC → Screen 픽셀
	float Width = (NDCMaxX - NDCMinX) * 0.5f * ViewportWidth;
	float Height = (NDCMaxY - NDCMinY) * 0.5f * ViewportHeight;

	return static_cast<UINT64>(Width * Height);
}
