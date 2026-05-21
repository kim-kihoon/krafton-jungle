#include "LightComponent.h"
#include "Object/ObjectFactory.h"
#include "Core/PropertyTypes.h"
#include "Render/Resource/ShadowAtlasManager.h"

#include <cmath>

DEFINE_CLASS(ULightComponent, ULightComponentBase)
REGISTER_FACTORY(ULightComponent)

namespace
{
	void BuildFrustumSplitCorners(
		const FMatrix& CamView,
		const FMatrix& CamProj,
		float SplitNearRatio,
		float SplitFarRatio,
		FVector OutWorldCorners[8],
		FVector OutViewCorners[8])
	{
		const FMatrix InvProj = CamProj.GetInverse();
		const FMatrix InvView = CamView.GetInverse();

		const FVector NdcNear[4] =
		{
			FVector(-1, -1, 0),
			FVector(1, -1, 0),
			FVector(-1,  1, 0),
			FVector(1,  1, 0),
		};

		const FVector NdcFar[4] =
		{
			FVector(-1, -1, 1),
			FVector(1, -1, 1),
			FVector(-1,  1, 1),
			FVector(1,  1, 1),
		};

		for (int i = 0; i < 4; ++i)
		{
			const FVector NearView = InvProj.TransformPosition(NdcNear[i]);
			const FVector FarView = InvProj.TransformPosition(NdcFar[i]);

			OutViewCorners[i] = FVector::Lerp(NearView, FarView, SplitNearRatio);
			OutViewCorners[i + 4] = FVector::Lerp(NearView, FarView, SplitFarRatio);
			OutWorldCorners[i] = InvView.TransformPosition(OutViewCorners[i]);
			OutWorldCorners[i + 4] = InvView.TransformPosition(OutViewCorners[i + 4]);
		}
	}

	void BuildLightBasis(const FVector& LightDir, FVector& OutRight, FVector& OutUp)
	{
		FVector Ref = FVector::UpVector;
		if (std::abs(FVector::DotProduct(LightDir, FVector::UpVector)) >= 0.9f)
		{
			Ref = FVector::RightVector;
		}

		OutRight = FVector::CrossProduct(Ref, LightDir).GetSafeNormal();
		OutUp = FVector::CrossProduct(LightDir, OutRight).GetSafeNormal();
	}

	FMatrix BuildLightView(const FVector& Center, float Radius, const FVector& LightDir, const FVector& Up)
	{
		const float ViewBackoff = Radius + 10.0f;
		const FVector Eye = Center - LightDir * ViewBackoff;

		return FMatrix::MakeViewLookAtLH(Eye, Center, Up);
	}

	FVector GetCornersCenter(const FVector Corners[8])
	{
		FVector Center = FVector::ZeroVector;
		for (int i = 0; i < 8; ++i)
		{
			Center += Corners[i];
		}
		return Center / 8.0f;
	}

	float GetCornersRadius(const FVector Corners[8], const FVector& Center)
	{
		float Radius = 1.0f;
		for (int i = 0; i < 8; ++i)
		{
			Radius = std::max(Radius, (Corners[i] - Center).Size());
		}
		return Radius;
	}

	float SnapToTexel(float Value, float TexelSize)
	{
		if (TexelSize <= 0.0f)
		{
			return Value;
		}

		return std::floor(Value / TexelSize + 0.5f) * TexelSize;
	}

	FVector SnapCenterToShadowTexel(
		const FVector& Center,
		const FVector& LightDir,
		const FVector& Right,
		const FVector& Up,
		float TexelSize)
	{
		const float ForwardDistance = FVector::DotProduct(Center, LightDir);
		const float RightDistance = FVector::DotProduct(Center, Right);
		const float UpDistance = FVector::DotProduct(Center, Up);

		return LightDir * ForwardDistance +
			Right * SnapToTexel(RightDistance, TexelSize) +
			Up * SnapToTexel(UpDistance, TexelSize);
	}
}

FMatrix ULightComponent::GetLightViewProj(const FMatrix& CamView, const FMatrix& CamProj,
	const TArray<FBoundingBox>* VisibleObjectsBounds) const
{
	switch (eShadowMapType)
	{
	case EShadowMap::CSM:
		return ComputeCascadeShadowMatrix(CamView, CamProj, 0.0f, 0.001f, 0.0f);
	case EShadowMap::PSM:
		return ComputePerspectiveShadowMatrix(CamView, CamProj, VisibleObjectsBounds);
	default:
		return FMatrix::Identity;
	}
}

FMatrix ULightComponent::GetLightViewProj(const FMatrix& CamView, const FMatrix& CamProj,
	float SplitNearT, float SplitFarT, const TArray<FBoundingBox>* VisibleObjectsBounds, float ShadowMapResolution) const
{
	switch (eShadowMapType)
	{
	case EShadowMap::CSM:
		return ComputeCascadeShadowMatrix(CamView, CamProj, SplitNearT, SplitFarT, ShadowMapResolution);
	case EShadowMap::PSM:
		return ComputePerspectiveShadowMatrix(CamView, CamProj, VisibleObjectsBounds);
	default:
		return FMatrix::Identity;
	}
}

void ULightComponent::PostDuplicate(UObject* Original)
{
	ULightComponentBase::PostDuplicate(Original);
	ULightComponent* Orig = Cast<ULightComponent>(Original);

	eShadowMapType = Orig->eShadowMapType;
	bShadowTexelSnapped = Orig->bShadowTexelSnapped;
}

void ULightComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	ULightComponentBase::GetEditableProperties(OutProps);

	static const char* ShadowMapTypeNames[] = { "CSM" ,"PSM" };
	OutProps.push_back({ "ShadowMapType", EPropertyType::Enum, &eShadowMapType, 0.f, 0.f, 0.f, ShadowMapTypeNames, 2 });

	OutProps.push_back({ "Shadow Resolution Scale", EPropertyType::Int, &ShadowResolutionScale});
	OutProps.push_back({ "Shadow Texel Snap", EPropertyType::Bool, &bShadowTexelSnapped });
    PrintShadowMapDebugInfo(OutProps);

	// ConstantBias = DepthBias * (1 / 2 ^ 24 or 16) + SlopeScaledBias * (MaxDepthSlope)
	// MaxDepthSlope는 이제 셰이더에서 직접 구할 예정. 레스터라이즈로 할 경우 PSM 처리가 안됨
	OutProps.push_back({ "Constant Bias ( DepthBias ^ (1 / TextureBit))", EPropertyType::Float, &ConstantBias, 0.000f, 0.01f, 0.001f });
	OutProps.push_back({ "Slope-Scaled Bias", EPropertyType::Float, &SlopeScaledBias, 0.0f, 1.0f, 0.01f });
    OutProps.push_back({ "Shadow Sharpen", EPropertyType::Float, &ShadowSharpen });
}

void ULightComponent::Serialize(FArchive& Ar)
{
	ULightComponentBase::Serialize(Ar);
	
	uint32 ShadowMapTypeValue = static_cast<uint32>(eShadowMapType);
	Ar << "ShadowMapType" << ShadowMapTypeValue;
	Ar << "ShadowResolutionScale" << ShadowResolutionScale;
	if (Ar.IsSaving() || Ar.HasKey("ShadowTexelSnap"))
	{
		Ar << "ShadowTexelSnap" << bShadowTexelSnapped;
	}
	Ar << "ConstantBias" << ConstantBias;
	Ar << "SlopeScaledBias" << SlopeScaledBias;
	Ar << "ShadowSharpen" << ShadowSharpen;

	if (Ar.IsLoading())
	{
		eShadowMapType = static_cast<EShadowMap>(ShadowMapTypeValue);
	}
}

FMatrix ULightComponent::ComputeCascadeShadowMatrix(const FMatrix& CamView, const FMatrix& CamProj,
	float SplitNearT, float SplitFarT, float ShadowMapResolution) const
{
	constexpr float XYPad = 2.0f;
	constexpr float DepthPad = 10.0f;

	FVector SplitCorners[8];
	FVector ViewSplitCorners[8];
	BuildFrustumSplitCorners(CamView, CamProj, SplitNearT, SplitFarT, SplitCorners, ViewSplitCorners);

	const FVector LightDir = GetForwardVector().GetSafeNormal();
	const FVector SplitCenter = GetCornersCenter(SplitCorners);
	const FVector ViewSplitCenter = GetCornersCenter(ViewSplitCorners);
	const float CascadeRadius = GetCornersRadius(ViewSplitCorners, ViewSplitCenter);
	const float RawHalfExtent = CascadeRadius + XYPad;
	const float ExtentMagnitude = RawHalfExtent > 0.0f ? std::pow(2.0f, std::floor(std::log2(RawHalfExtent))) : 1.0f;
	const float ExtentQuantum = std::max(1.0f / 16.0f, ExtentMagnitude / 1024.0f);
	const float HalfExtent = std::ceil(RawHalfExtent / ExtentQuantum) * ExtentQuantum;
	const float Resolution = std::max(1.0f, ShadowMapResolution > 0.0f ? ShadowMapResolution : static_cast<float>(ShadowResolutionScale));
	const float TexelSize = (HalfExtent * 2.0f) / Resolution;

	FVector LightRight;
	FVector LightUp;
	BuildLightBasis(LightDir, LightRight, LightUp);

	const FVector ShadowCenter = IsShadowTexelSnapped()
		? SnapCenterToShadowTexel(SplitCenter, LightDir, LightRight, LightUp, TexelSize)
		: SplitCenter;
	const FMatrix LightView = BuildLightView(ShadowCenter, HalfExtent, LightDir, LightUp);
	const FMatrix LightProj = FMatrix::MakeOrthographicLH(
		HalfExtent * 2.0f,
		HalfExtent * 2.0f,
		0.0f,
		HalfExtent * 2.0f + DepthPad);

	return LightView * LightProj;
}

void ULightComponent::PrintShadowMapDebugInfo(TArray<FPropertyDescriptor>& OutProps) const
{
    FShadowAtlasManager& AtlasManager = FShadowAtlasManager::Get();
    static const char* ShadowMapFaceNames[] = { "PositiveX", "NegativeX", "PositiveY", "NegativeY", "PositiveZ", "NegativeZ" };
    static ID3D11ShaderResourceView* FaceSRV[6];

    if (bHasDebugShadowAtlasTile)
    {
        const FVector4& SO = bHasDebugShadowAtlasTile
                                 ? DebugShadowAtlasScaleOffset
                                 : FVector4(1, 1, 0, 0);


        static FSRVDisplayInfo ShadowMapDisplay;
        ShadowMapDisplay = {
            256.f,
            256.f,
            SO.Z,
            SO.W,
            SO.Z + SO.X,
            SO.W + SO.Y
        };

        OutProps.push_back({ "ShadowMap", EPropertyType::SRV, AtlasManager.GetSRV(), 0.f, 0.f, 0.f, nullptr, 0, &ShadowMapDisplay });
    }
    else if (bHasDebugShadowCubeTile)
    {
        for (int i = 0; i < 6; i++)
        {
            FaceSRV[i] = AtlasManager.GetCubeDebugSRV(static_cast<int>(DebugShadowCubeIndex), i);
            if (!FaceSRV[i])
                return;
        }

        OutProps.push_back({ "CubeMap",
                             EPropertyType::CubeSRV,
                             FaceSRV, 0.f, 0.f, 0.f, nullptr, 0 });
	}
}
