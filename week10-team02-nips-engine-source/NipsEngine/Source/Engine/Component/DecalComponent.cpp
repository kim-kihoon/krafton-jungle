#include "DecalComponent.h"
#include "Geometry/AABB.h"

#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Core/ResourceManager.h"
#include "Core/Logger.h"
#include "Object/ObjectFactory.h"

// GameJam
#include "Runtime/Engine.h"
#include <algorithm>
#include <cmath>

DEFINE_CLASS(UDecalComponent, UPrimitiveComponent)
REGISTER_FACTORY(UDecalComponent)

namespace
{
	constexpr float CleanCompleteThreshold = 0.66f;
	constexpr float CleanCompleteFlashDuration = 0.22f;
	constexpr float CleanCompleteFadeOutDuration = 0.65f;
}

// Decal Box가 화면 밖으로 나가도 컬링되지 않도록 합니다.
UDecalComponent::UDecalComponent()
{
	Materials.resize(1);

	UMaterial* Mat = FResourceManager::Get().GetMaterial("DecalMat");
	SetMaterial(Mat);

	Mat->DepthStencilType = EDepthStencilType::Default;
	Mat->BlendType = EBlendType::AlphaBlend;
	Mat->RasterizerType = ERasterizerType::SolidBackCull;
	Mat->SamplerType = ESamplerType::EST_Linear;

    bEnableCull = false;

    InitializeMask(256, 256);
}

// Material 포인터는 프로퍼티 시스템에 노출되지 않으므로 직접 복사합니다.
// LifeTime 은 런타임 상태이므로 복사하지 않습니다 (BeginPlay 에서 0 으로 초기화).
void UDecalComponent::PostDuplicate(UObject* Original)
{
    UPrimitiveComponent::PostDuplicate(Original);

    const UDecalComponent* Orig = Cast<UDecalComponent>(Original);
	SetMaterial(Orig->GetMaterial()); // 얕은 복사 — ResourceManager 가 소유
}

void UDecalComponent::Serialize(FArchive& Ar)
{
	UPrimitiveComponent::Serialize(Ar);

	FString MaterialName = (Materials[0] != nullptr)
		? (Materials[0]->GetFilePath().empty() ? Materials[0]->GetName() : Materials[0]->GetFilePath())
		: FString();
	Ar << "Material" << MaterialName;
	Ar << "Size" << DecalSize;
	Ar << "Color" << DecalColor;
	Ar << "Fade Start Delay" << FadeStartDelay;
	Ar << "Fade Duration" << FadeDuration;
	Ar << "Fade In Start Delay" << FadeInStartDelay;
	Ar << "Fade In Duration" << FadeInDuration;
	Ar << "Destroy Owner After Fade" << bDestroyOwnerAfterFade;

	if (Ar.IsLoading())
	{
		if (!MaterialName.empty())
		{
			SetMaterial(FResourceManager::Get().GetMaterialInterface(MaterialName));
		}
	}
}

void UDecalComponent::BeginPlay()
{
	UPrimitiveComponent::BeginPlay();

	LifeTime = 0.0f;
	CleanCompleteFlashTime = 0.0f;
	bCleanCompleteFlashActive = false;
	bCleanCompleteFlashPlayed = false;

	//GameJam
	ResizeMaskToDecalSize();
}

void UDecalComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UPrimitiveComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Size", EPropertyType::Vec3, &DecalSize });
	OutProps.push_back({ "Color", EPropertyType::Vec4, &DecalColor });
	OutProps.push_back({ "Fade Start Delay", EPropertyType::Float, &FadeStartDelay });
	OutProps.push_back({ "Fade Duration", EPropertyType::Float, &FadeDuration });
	OutProps.push_back({ "Fade In Start Delay", EPropertyType::Float, &FadeInStartDelay });
	OutProps.push_back({ "Fade In Duration", EPropertyType::Float, &FadeInDuration });
	OutProps.push_back({ "Destroy Owner After Fade", EPropertyType::Bool, &bDestroyOwnerAfterFade });
}

void UDecalComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);

	if (std::strcmp(PropertyName, "Materials") == 0)
	{
		for (int32 i = 0; i < static_cast<int32>(Materials.size()); ++i)
		{
			if (Materials[i] == nullptr)
			{
				SetMaterial(i, FResourceManager::Get().GetMaterialInterface("DecalMat"));
				continue;
			}
			SetMaterial(i, Materials[i]);
		}
	}
	else if (std::strcmp(PropertyName, "Size") == 0 || std::strcmp(PropertyName, "Transform") == 0)
	{
		ResizeMaskToDecalSize();
	}
}

void UDecalComponent::UpdateWorldAABB() const
{
    // 로컬 단위 박스를 데칼 행렬(Scale*WorldTM)로 변환해 회전이 반영된 월드 AABB를 얻습니다.
    const FAABB LocalBox(FVector(-0.5f, -0.5f, -0.5f), FVector(0.5f, 0.5f, 0.5f));
    WorldAABB = FAABB::TransformAABB(LocalBox, GetDecalMatrix());
}

bool UDecalComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
{
    // 레이를 데칼 로컬 공간으로 변환해 OBB 교차를 단위 박스 AABB 테스트로 처리합니다.
    // T 파라미터는 선형 변환 하에서 보존되므로 결과 T가 곧 월드 공간 거리입니다.
    const FMatrix InvDecalMat = GetDecalMatrix().GetInverse();
    const FVector LocalOrigin = InvDecalMat.TransformPosition(Ray.Origin);
    const FVector LocalDir    = InvDecalMat.TransformVector(Ray.Direction);

    const FAABB UnitBox(FVector(-0.5f, -0.5f, -0.5f), FVector(0.5f, 0.5f, 0.5f));
    float TMin = 0.0f, TMax = 0.0f;
    if (!UnitBox.IntersectRay(FRay(LocalOrigin, LocalDir), TMin, TMax))
        return false;

    const float T = TMin >= 0.0f ? TMin : TMax;
    if (T < 0.0f)
        return false;

    OutHitResult.bHit = true;
    OutHitResult.Distance = T;
    OutHitResult.Location = Ray.Origin + Ray.Direction * T;
    OutHitResult.Normal = -Ray.Direction;
    OutHitResult.HitComponent = this;
    return true;
}

FMatrix UDecalComponent::GetDecalMatrix() const
{
	FMatrix WorldMatrix = FMatrix::MakeScaleMatrix(DecalSize) * GetWorldMatrix();
	return WorldMatrix;
}

void UDecalComponent::TickComponent(float DeltaTime)
{
	UPrimitiveComponent::TickComponent(DeltaTime);

	LifeTime += DeltaTime;

	// GameJam
	if (bMaskDirty)
	{
		UpdateMaskTexture();
	}

	if (FadeInStartDelay + FadeInDuration > 0 && LifeTime < FadeInStartDelay + FadeInDuration)
	{
		TickFadeIn();
	}
	else
	{
		// 기본적으로 청소된 만큼 투명해집니다 (비례)
		const float CleanAlpha = MathUtil::Clamp(1.0f - CachedCleanPercentage, 0.0f, 1.0f);

		if (FadeStartDelay + FadeDuration > 0)
		{
			TickFadeOut();
		}
		else if (bCleanCompleteFlashActive)
		{
			TickCleanCompleteFlash(DeltaTime);
		}
		else
		{
			DecalColor.A = CleanAlpha;

			// 85% 이상 청소되면 자동 페이드 아웃 및 파괴 시퀀스 시작
			if (!bCleanCompleteFlashPlayed && CachedCleanPercentage > CleanCompleteThreshold)
			{
				StartCleanCompleteFlash(CleanAlpha);
			}
		}
	}
}

void UDecalComponent::TickFadeIn()
{
	float FadeInTime = LifeTime - FadeInStartDelay;
	if (FadeInTime < 0.0f)
	{
		DecalColor.A = 0.0f;
		return;
	}
	
	if (FadeInDuration <= 0.0f)
	{
		DecalColor.A = 1.0f;
		return;
	}

	float Alpha = FadeInTime / FadeInDuration;

	DecalColor.A = MathUtil::Clamp(Alpha, 0.0f, 1.0f);
}

void UDecalComponent::TickFadeOut()
{
	float FadeOutLifeTime = LifeTime - FadeInStartDelay - FadeInDuration;

	float FadeOutTime = FadeOutLifeTime - FadeStartDelay;
	if (FadeOutTime < 0.0f) return;

	float Alpha = 1.0f - (FadeOutTime / FadeDuration);
	DecalColor.A = MathUtil::Clamp(Alpha, 0.0f, 1.0f);

	// 페이드 아웃이 거의 완료되었거나(Alpha < 0.05), 시간이 다 되면 즉시 삭제
	if (DecalColor.A < 0.05f || FadeOutLifeTime >= FadeStartDelay + FadeDuration)
	{
		SetActive(false);
		if (bDestroyOwnerAfterFade && GetOwner())
		{
			if (UWorld* World = GetOwner()->GetFocusedWorld())
			{
				World->DeactivateActor(GetOwner());
			}
		}
	}
}

void UDecalComponent::StartCleanCompleteFlash(float CleanAlpha)
{
	bCleanCompleteFlashActive = true;
	bCleanCompleteFlashPlayed = true;
	CleanCompleteFlashTime = 0.0f;

	DecalColor.R = 1.0f;
	DecalColor.G = 1.0f;
	DecalColor.B = 1.0f;
	DecalColor.A = std::max(CleanAlpha, 0.75f);
}

void UDecalComponent::TickCleanCompleteFlash(float DeltaTime)
{
	CleanCompleteFlashTime += std::max(0.0f, DeltaTime);

	const float FlashT = MathUtil::Clamp(CleanCompleteFlashTime / CleanCompleteFlashDuration, 0.0f, 1.0f);
	DecalColor.R = 1.0f;
	DecalColor.G = 1.0f;
	DecalColor.B = 1.0f;
	DecalColor.A = 1.0f - (0.15f * FlashT);

	if (CleanCompleteFlashTime >= CleanCompleteFlashDuration)
	{
		bCleanCompleteFlashActive = false;
		const float FadeOutLifeTime = LifeTime - FadeInStartDelay - FadeInDuration;
		SetFadeOut(FadeOutLifeTime, CleanCompleteFadeOutDuration, true);
	}
}

void UDecalComponent::SetFadeIn(float InStartDelay, float InDuration)
{
	FadeInStartDelay = InStartDelay;
	FadeInDuration = InDuration;
}

void UDecalComponent::SetFadeOut(float InStartDelay, float InDuration, bool bInDestroyOwnerAfterFade)
{
	FadeStartDelay = InStartDelay;
	FadeDuration = InDuration;
	bDestroyOwnerAfterFade = bInDestroyOwnerAfterFade;
}

void UDecalComponent::ResizeMaskToDecalSize()
{
    // 기준: DecalSize (5,5,5) → 256x256. 월드 크기에 비례해 해상도를 조정한다.
    // UV.X = Local.Y, UV.Y = Local.Z 이므로 Width는 Y축, Height는 Z축 기준.
    constexpr float BaseUnit       = 5.0f;
    constexpr uint32 BaseRes       = 256;
    constexpr uint32 MinRes        = 64;
    constexpr uint32 MaxRes        = 2048;

    const FVector WorldSize = DecalSize * GetRelativeScale();

    auto Clamp = [](uint32 V, uint32 Lo, uint32 Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); };

    const uint32 W = Clamp(static_cast<uint32>(WorldSize.Y / BaseUnit * BaseRes), MinRes, MaxRes);
    const uint32 H = Clamp(static_cast<uint32>(WorldSize.Z / BaseUnit * BaseRes), MinRes, MaxRes);

    InitializeMask(W, H);
}

void UDecalComponent::InitializeMask(uint32 InWidth, uint32 InHeight)
{
    MaskWidth = InWidth;
    MaskHeight = InHeight;

	MaskPixels.assign(MaskWidth * MaskHeight, 255);
	CachedCleanPercentage = 0.0f;

	D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = MaskWidth;
    desc.Height = MaskHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM; // 0~255를 0.0~1.0으로 매핑
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DYNAMIC; // CPU에서 쓰기 위해 Dynamic
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; // CPU에서 GPU로 복사 허용

	D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = MaskPixels.data();
    initData.SysMemPitch = MaskWidth;

	auto device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
    if (!device)
        return;

    device->CreateTexture2D(&desc, &initData, MaskTexture.GetAddressOf());
    device->CreateShaderResourceView(MaskTexture.Get(), nullptr, MaskSRV.GetAddressOf());
}

void UDecalComponent::PaintMask(FVector2 UV, float Radius, uint8 Value)
{
    int32 CenterX = static_cast<int32>(UV.X * MaskWidth);
    int32 CenterY = static_cast<int32>(UV.Y * MaskHeight);
    int32 PixelRadius = static_cast<int32>(Radius * MaskWidth);

	if (PixelRadius <= 0)
        return;

	int32 StartX = MathUtil::Clamp(CenterX - PixelRadius, 0, (int32)MaskWidth - 1);
	int32 EndX = MathUtil::Clamp(CenterX + PixelRadius, 0, (int32)MaskWidth - 1);
	int32 StartY = MathUtil::Clamp(CenterY - PixelRadius, 0, (int32)MaskHeight - 1);
    int32 EndY = MathUtil::Clamp(CenterY + PixelRadius, 0, (int32)MaskHeight - 1);

	float InvPixelRadius = 1.0f / static_cast<float>(PixelRadius);
    float RadiusSq = static_cast<float>(PixelRadius * PixelRadius);

	for (int32 y = StartY; y <= EndY; y++)
	{
		for (int32 x = StartX; x <= EndX; x++)
		{
            int32 DiffX = x - CenterX;
            int32 DiffY = y - CenterY;
            int32 DistSq = (DiffX * DiffX) + (DiffY * DiffY);

			if (DistSq <= RadiusSq)
			{
                float Distance = static_cast<float>(std::sqrt(DistSq));
				float NormalizedDistance = Distance / PixelRadius;
                float Falloff = 1.0f - NormalizedDistance;
				Falloff = std::pow(Falloff, 2.0f);

				int32 AppliedValue = static_cast<int32>(Value * Falloff + 0.5f);

                int32 CurrentValue = MaskPixels[y * MaskWidth + x];
                int32 NewValue = CurrentValue - AppliedValue;

				MaskPixels[y * MaskWidth + x] = static_cast<uint8>(NewValue < 0 ? 0 : NewValue);

                bMaskDirty = true;
			}
		}
	}
}

bool UDecalComponent::WorldPosToDecalUV(const FVector& WorldPos, FVector2& OutUV) const
{
    FMatrix InvDecal = GetDecalMatrix();
    InvDecal = InvDecal.GetInverse();
    const FVector Local = InvDecal.TransformPosition(WorldPos);

    if (std::abs(Local.X) > 0.5f || std::abs(Local.Y) > 0.5f || std::abs(Local.Z) > 0.5f)
        return false;

    OutUV.X = Local.Y + 0.5f;
    OutUV.Y = 1.0f - (Local.Z + 0.5f);
    return true;
}

float UDecalComponent::GetCleanPercentage() const
{
    return CachedCleanPercentage;
}

bool UDecalComponent::IsPixelCleanAt(FVector2 UV) const
{
    if (MaskPixels.empty())
        return false;

    const int32 CX = static_cast<int32>(UV.X * MaskWidth);
    const int32 CY = static_cast<int32>(UV.Y * MaskHeight);
    if (CX < 0 || CX >= (int32)MaskWidth || CY < 0 || CY >= (int32)MaskHeight)
        return true;

    // 중심 주변 3x3 픽셀 평균으로 판단해 경계 노이즈를 줄입니다.
    int32 Sum = 0, Count = 0;
    for (int32 DY = -1; DY <= 1; ++DY)
    {
        for (int32 DX = -1; DX <= 1; ++DX)
        {
            const int32 X = CX + DX, Y = CY + DY;
            if (X < 0 || X >= (int32)MaskWidth || Y < 0 || Y >= (int32)MaskHeight)
                continue;
            Sum += MaskPixels[Y * MaskWidth + X];
            ++Count;
        }
    }
    constexpr int32 CleanThreshold = 10;
    return Count > 0 && (Sum / Count) <= CleanThreshold;
}

void UDecalComponent::UpdateMaskTexture()
{
    if (!bMaskDirty || !MaskTexture)
        return;

	auto context = GEngine->GetRenderer().GetFD3DDevice().GetDeviceContext();
    D3D11_MAPPED_SUBRESOURCE mappedResource;

	if (!context)
        return;

	if (SUCCEEDED(context->Map(MaskTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource)))
	{
        uint8_t* dest = static_cast<uint8_t*>(mappedResource.pData);
        const uint8_t* src = MaskPixels.data();

		uint64 TotalValue = 0;
		for (uint32 i = 0; i < MaskHeight; i++)
		{
            memcpy(dest + (i * mappedResource.RowPitch), src + (i * MaskWidth), MaskWidth);
			
			// 청소율 계산용 합계 (최적화: 텍스처 복사 시 함께 수행)
			for (uint32 j = 0; j < MaskWidth; j++)
			{
				TotalValue += src[i * MaskWidth + j];
			}
		}

		context->Unmap(MaskTexture.Get(), 0);
        bMaskDirty = false;

		// 캐시된 청소율 갱신 (0~1 범위)
		float MaxTotal = static_cast<float>(MaskWidth * MaskHeight) * 255.0f;
		CachedCleanPercentage = 1.0f - (static_cast<float>(TotalValue) / MaxTotal);
	}
}
