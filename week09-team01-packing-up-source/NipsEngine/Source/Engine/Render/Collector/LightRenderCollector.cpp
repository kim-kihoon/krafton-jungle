#include "LightRenderCollector.h"

#include "Component/Light/AmbientLightComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Component/Light/PointLightComponent.h"
#include "Component/Light/SpotLightComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Engine/Asset/StaticMesh.h"
#include "Engine/Geometry/Frustum.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Math/Utils.h"
#include "Render/Renderer/RenderFlow/ShadowAtlasManager.h"
#include "Render/Resource/MeshBufferManager.h"
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace
{
	constexpr float SpotShadowNearPlane = 0.1f;
	constexpr float SpotShadowBaseResolution = 1024.0f;
	constexpr size_t ShadowDepthBytesPerPixel = 4;
	constexpr size_t ShadowVSMBytesPerPixel = 8;
	constexpr size_t ShadowBytesPerPixel = ShadowDepthBytesPerPixel + ShadowVSMBytesPerPixel;

	constexpr float PointShadowNearPlane = 0.1f;
	constexpr float PointShadowBaseResolution = 256.0f;
	constexpr int32 MaxPointShadowCount = 8;

	struct FDirectionalCSMBuildResult
	{
		FMatrix LightViewProj[MAX_CASCADE_COUNT];
		FVector4 SplitDistances;
		FVector4 CascadeRadius;
	};

	struct FDirectionalPSMBuildResult
	{
		FMatrix LightViewProj;
	};

	static_assert(static_cast<uint32>(EShadowMode::CSM) == DirectionalShadowModeValue::CSM);
	static_assert(static_cast<uint32>(EShadowMode::PSM) == DirectionalShadowModeValue::PSM);
	FVector MakeLightColorVector(const ULightComponentBase* LightComponent)
	{
		if (LightComponent == nullptr)
		{
			return FVector::ZeroVector;
		}

		const FColor& LightColor = LightComponent->GetLightColor();
		return FVector(LightColor.r, LightColor.g, LightColor.b);
	}

	FVector MakeStableUpVector(const FVector& Direction)
	{
		const FVector NormalizedDirection = Direction.GetSafeNormal();
		FVector Up = FVector::UpVector;
		if (std::fabs(FVector::DotProduct(Up, NormalizedDirection)) > 0.98f)
		{
			Up = FVector::RightVector;
		}
		return Up;
	}

	FVector4 TransformVector4ByMatrix(const FVector4& Vector, const FMatrix& Matrix)
	{
		return Matrix.TransformVector4(Vector, Matrix);
	}

	float MakeSpotShadowFarPlane(const USpotLightComponent* SpotLight)
	{
		return std::max(SpotLight->GetAttenuationRadius(), SpotShadowNearPlane + 1.0f);
	}

	float MakeSpotShadowResolution(const ULightComponent* LightComponent)
	{
		return std::max(1.0f, SpotShadowBaseResolution * LightComponent->GetShadowResolutionScale());
	}

	float MakePointShadowResolution(const ULightComponent* LightComponent)
	{
		return std::max(1.0f, PointShadowBaseResolution * LightComponent->GetShadowResolutionScale());
	}

	size_t CalculateShadowTileMemory(uint32 Width, uint32 Height)
	{
		return static_cast<size_t>(Width) * static_cast<size_t>(Height) * ShadowBytesPerPixel;
	}

	FMatrix MakeSpotShadowViewProjection(
		const USpotLightComponent* SpotLight,
		const FVector& LightDirection,
		float NearPlane,
		float FarPlane)
	{
		const FVector Direction = LightDirection.GetSafeNormal();
		const FVector LightPosition = SpotLight->GetWorldLocation();
		const float FovY = MathUtil::DegreesToRadians(
			MathUtil::Clamp(SpotLight->GetOuterConeAngle() * 2.0f, 1.0f, 175.0f));

		const FMatrix LightView = FMatrix::MakeViewLookAtLH(
			LightPosition,
			LightPosition + Direction,
			MakeStableUpVector(Direction));
		const FMatrix LightProjection = FMatrix::MakePerspectiveFovLH(FovY, 1.0f, NearPlane, FarPlane);
		return LightView * LightProjection;
	}

	void MakePointShadowViewProjections(
		const FVector& LightPosition,
		float NearPlane,
		float FarPlane,
		FMatrix OutViewProj[6])
	{
		static constexpr FVector FaceDirections[6] =
		{
			FVector(1.0f, 0.0f, 0.0f),
			FVector(-1.0f, 0.0f, 0.0f),
			FVector(0.0f, 1.0f, 0.0f),
			FVector(0.0f, -1.0f, 0.0f),
			FVector(0.0f, 0.0f, 1.0f),
			FVector(0.0f, 0.0f, -1.0f)
		};
		static constexpr FVector FaceUps[6] =
		{
			FVector(0.0f, 0.0f, 1.0f),   // +X
			FVector(0.0f, 0.0f, 1.0f),   // -X
			FVector(0.0f, 0.0f, 1.0f),   // +Y
			FVector(0.0f, 0.0f, 1.0f),   // -Y
			FVector(-1.0f, 0.0f, 0.0f),  // +Z
			FVector(1.0f, 0.0f, 0.0f)    // -Z
		};

		// Cube face seam을 줄이기 위해 90도보다 아주 살짝 넓게 잡습니다.
		const FMatrix LightProjection =
			FMatrix::MakePerspectiveFovLH(MathUtil::DegreesToRadians(90.5f), 1.0f, NearPlane, FarPlane);

		for (uint32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
		{
			const FVector FaceDirection = FaceDirections[FaceIndex];
			const FMatrix LightView = FMatrix::MakeViewLookAtLH(
				LightPosition,
				LightPosition + FaceDirection,
				FaceUps[FaceIndex]);

			OutViewProj[FaceIndex] = LightView * LightProjection;
		}
	}

	/* 밝을수록/반경이 클수록/카메라에 가까울수록 점수를 크게 주도록 합니다. */
	float ComputeSpotShadowPriority(const ULightComponent* LightComponent, const FVector& LightLocation, float AttenuationRadius, const FVector& CameraPosition)
	{
		const FVector ToCamera = LightLocation - CameraPosition;
		const float DistanceSq = std::max(FVector::DotProduct(ToCamera, ToCamera), 1.0f);
		
		const float ScreenCoverage = std::clamp((AttenuationRadius * AttenuationRadius) / DistanceSq, 0.05f, 8.0f);
	
		return std::max(LightComponent->GetIntensity(), 0.0f) * ScreenCoverage;
	}

	int32 ExtractActorNumericSuffix(const AActor* Actor)
	{
		if (Actor == nullptr)
		{
			return -1;
		}

		const FString ActorName = Actor->GetFName().ToString();
		const size_t UnderscorePos = ActorName.find_last_of('_');
		if (UnderscorePos == FString::npos || UnderscorePos + 1 >= ActorName.size())
		{
			return -1;
		}

		int32 Value = 0;
		bool bHasDigit = false;
		for (size_t Index = UnderscorePos + 1; Index < ActorName.size(); ++Index)
		{
			const unsigned char Ch = static_cast<unsigned char>(ActorName[Index]);
			if (!std::isdigit(Ch))
			{
				return -1;
			}

			bHasDigit = true;
			Value = (Value * 10) + static_cast<int32>(Ch - '0');
		}

		return bHasDigit ? Value : -1;
	}

	/* Frustum 근평면, 원평면 꼭짓점 기준으로 비례식을 세워서 TargetDepth 깊이의 절단면의 꼭짓점을 찾습니다. */
	FVector InterpolateFrustumCorner(
		const FVector& NearCorner,
		const FVector& FarCorner,
		float NearDepth,
		float FarDepth,
		float TargetDepth)
	{
		const float Range = FarDepth - NearDepth;
		if (std::fabs(Range) <= MathUtil::KindaSmallNumber)
		{
			return FarCorner;
		}

		const float T = (TargetDepth - NearDepth) / Range;
		return NearCorner + (FarCorner - NearCorner) * T;
	}

	/* PSSM(Parallel - Split Shadow Map) 공식에 따라 View Frustum을 평행 분할합니다.
	 * Lambda[0, 1] → Linear : 0.0f, Logarithmic : 1.0f */
	void CalculatePSSMSplits(int32 CascadeCount, float Lambda, float NearPlane, float ShadowDistance, float* OutSplits)
	{
		OutSplits[0] = NearPlane;
		for (int32 i = 1; i < CascadeCount; ++i)
		{
			float Fraction = static_cast<float>(i) / CascadeCount; // 전체 Cascade 구간 중 경계선
			float LogarithmSplit = std::pow(ShadowDistance / NearPlane, Fraction);
			float UniformSplit = NearPlane + (ShadowDistance - NearPlane) * Fraction;
			OutSplits[i] =  Lambda * LogarithmSplit + (1.0f - Lambda) * UniformSplit;
		}
		OutSplits[CascadeCount] = ShadowDistance;
	}

	void BuildPSMCameraViewProjection(
		const UDirectionalLightComponent* Light,
		const FRenderBus& RenderBus,
		FMatrix& OutView,
		FMatrix& OutProj)
	{
		OutView = RenderBus.GetView();
		OutProj = RenderBus.GetProj();

		const float VirtualSlideBack = Light != nullptr ? Light->GetPSMVirtualSlideBack() : 0.0f;
		if (VirtualSlideBack <= MathUtil::KindaSmallNumber || RenderBus.IsOrthographic())
		{
			return;
		}

		const FVector CameraForward = RenderBus.GetCameraForward().GetSafeNormal();
		if (CameraForward.IsNearlyZero())
		{
			return;
		}

		FVector CameraUp = RenderBus.GetCameraUp().GetSafeNormal();
		if (CameraUp.IsNearlyZero())
		{
			CameraUp = MakeStableUpVector(CameraForward);
		}

		const float XScale = OutProj.M[1][0];
		const float YScale = OutProj.M[2][1];
		if (std::fabs(XScale) <= MathUtil::KindaSmallNumber ||
			std::fabs(YScale) <= MathUtil::KindaSmallNumber)
		{
			return;
		}

		const FVector VirtualCameraPosition = RenderBus.GetCameraPosition() - CameraForward * VirtualSlideBack;
		const float FovY = 2.0f * std::atan(1.0f / std::fabs(YScale));
		const float AspectRatio = std::fabs(YScale / XScale);
		const float NearPlane = std::max(RenderBus.GetNear() + VirtualSlideBack, 0.001f);
		const float FarPlane = std::max(RenderBus.GetFar(), NearPlane + 0.001f);

		OutView = FMatrix::MakeViewLookAtLH(
			VirtualCameraPosition,
			VirtualCameraPosition + CameraForward,
			CameraUp);
		OutProj = FMatrix::MakePerspectiveFovLH(FovY, AspectRatio, NearPlane, FarPlane);
	}

	bool BuildOrthographicPostProjectiveViewProjection(
		const FVector& LightDirectionPP,
		const FVector& CubeCenterPP,
		float CubeRadiusPP,
		float MinPlaneGap,
		FMatrix& OutViewPP,
		FMatrix& OutProjPP)
	{
		FVector NormalizedLightDirectionPP = LightDirectionPP;
		if (!NormalizedLightDirectionPP.Normalize())
		{
			return false;
		}

		const FVector LightPositionPP = CubeCenterPP + NormalizedLightDirectionPP * (2.0f * CubeRadiusPP);
		const FVector ViewDirectionPP = (CubeCenterPP - LightPositionPP).GetSafeNormal();
		if (ViewDirectionPP.IsNearlyZero())
		{
			return false;
		}

		const float DistToCenter = FVector::Dist(LightPositionPP, CubeCenterPP);
		const float NearPP = std::max(MinPlaneGap, DistToCenter - CubeRadiusPP);
		const float FarPP = std::max(NearPP + MinPlaneGap, DistToCenter + CubeRadiusPP);

		OutViewPP = FMatrix::MakeViewLookAtLH(LightPositionPP, CubeCenterPP, MakeStableUpVector(ViewDirectionPP));
		OutProjPP = FMatrix::MakeOrthographicLH(CubeRadiusPP * 2.0f, CubeRadiusPP * 2.0f, NearPP, FarPP);
		return true;
	}

	/* View Frustum을 PSSM 공식에 따라 평행하게 잘라서 Cascade 구간으로 나눕니다.
	 * 이후 각 구간을 포함하는 최소 크기의 Bounding Sphere를 구하고,
	 * 빛의 시점에서 Bounding Sphere를 덮는 직교 투영 행렬과 뷰 행렬을 생성합니다. */
	bool BuildDirectionalCSMViewProjection(
		const UDirectionalLightComponent* Light,
		const FRenderBus& RenderBus, 
		const FVector& ToLight,
		FDirectionalCSMBuildResult& OutResult)
	{
		constexpr int32 CascadeCount = MAX_CASCADE_COUNT;
		const float NearPlane = std::max(RenderBus.GetNear(), 1.0f);
		const float Lambda = Light->GetCascadeSplitWeight();
		const float ShadowDistance = Light->GetShadowDistance();

		float Splits[MAX_CASCADE_COUNT + 1]; // CascadeCount + 1
		CalculatePSSMSplits(CascadeCount, Lambda, NearPlane, ShadowDistance, Splits);

		const FMatrix InverseViewProjection = (RenderBus.GetView() * RenderBus.GetProj()).GetInverse();
		static constexpr float NdcX[4] = { -1.0f, 1.0f, 1.0f, -1.0f };
		static constexpr float NdcY[4] = { -1.0f, -1.0f, 1.0f, 1.0f };

		FVector NearCorners[4];
		FVector FarCorners[4];
		for (int32 i = 0; i < 4; ++i) // i: Corner Index
		{
			NearCorners[i] = InverseViewProjection.TransformPosition(FVector(NdcX[i], NdcY[i], 0.0f));
			FarCorners[i] = InverseViewProjection.TransformPosition(FVector(NdcX[i], NdcY[i], 1.0f));
		}

		const FVector& CameraPosition = RenderBus.GetCameraPosition();
		const FVector& CameraForward = RenderBus.GetCameraForward();
		const float NearDepth = FVector::DotProduct(NearCorners[0] - CameraPosition, CameraForward);
		const float FarDepth = FVector::DotProduct(FarCorners[0] - CameraPosition, CameraForward);
		const FVector LightDirection = (ToLight * -1.0f).GetSafeNormal();

		for (int32 i = 0; i < CascadeCount; ++i) // i: Cascade Index
		{
			const float CascadeNear = Splits[i];
			const float CascadeFar = Splits[i + 1];
			OutResult.SplitDistances.XYZW[i] = CascadeFar;

			FVector CascadeCorners[8];
			for (int32 j = 0; j < 4; ++j) // j: Corner Index
			{
				CascadeCorners[j] = InterpolateFrustumCorner(NearCorners[j], FarCorners[j], NearDepth, FarDepth, CascadeNear);
				CascadeCorners[j + 4] = InterpolateFrustumCorner(NearCorners[j], FarCorners[j], NearDepth, FarDepth, CascadeFar);
			}

			FVector Center = FVector::ZeroVector;
			for (const FVector& Corner : CascadeCorners)
			{
				Center += Corner;
			}
			Center *= 1.0f / 8.0f;

			float Radius = 1.0f;
			for (const FVector& Corner : CascadeCorners)
			{
				Radius = std::max(Radius, FVector::Dist(Center, Corner));
			}
			
			if (Light->IsShadowTexelSnapped())
			{
				const float TexelSize = (Radius * 2.0f) / static_cast<float>(FShadowAtlasManager::DirectionalCascadeResolution);
				const FVector LightForward = LightDirection.GetSafeNormal();
				const FVector LightRight = FVector::CrossProduct(MakeStableUpVector(LightForward), LightForward).GetSafeNormal();
				const FVector LightUp = FVector::CrossProduct(LightForward, LightRight).GetSafeNormal();

				const float CenterRight = FVector::DotProduct(Center, LightRight);
				const float CenterUp = FVector::DotProduct(Center, LightUp);
				const float SnappedRight = std::round(CenterRight / TexelSize) * TexelSize;
				const float SnappedUp = std::round(CenterUp / TexelSize) * TexelSize;

				Center += LightRight * (SnappedRight - CenterRight);
				Center += LightUp * (SnappedUp - CenterUp);
			}

			const FVector LightPosition = Center - LightDirection * Radius;

			const float ZNear = 0.0f;
			const float ZFar = Radius * 2.0f;

			const FMatrix LightView = FMatrix::MakeViewLookAtLH(LightPosition, Center, MakeStableUpVector(LightDirection));
			const FMatrix LightProjection = FMatrix::MakeOrthographicLH(Radius * 2.0f, Radius * 2.0f, ZNear, ZFar);
			OutResult.LightViewProj[i] = LightView * LightProjection;
			OutResult.CascadeRadius.XYZW[i] = Radius;
		}

		return true;
	}

	bool BuildDirectionalPSMViewProjection(
		const UDirectionalLightComponent* Light,
		const FRenderBus& RenderBus,
		const FVector& ToLight,
		FDirectionalPSMBuildResult& OutResult)
	{
		const FVector ToLightDirection = ToLight.GetSafeNormal();
		if (ToLightDirection.IsNearlyZero())
		{
			return false;
		}

		// Direct3D NDC: x/y=[-1, 1], z=[0, 1].
		const FVector CubeCenterPP(0.0f, 0.0f, 0.5f);
		constexpr float CubeRadiusPP = 1.5f;
		constexpr float WThreshold = 0.001f;
		constexpr float MinNearPlane = 0.1f;
		constexpr float MinPlaneGap = 0.001f;
		constexpr float MinFovPP = MathUtil::DegreesToRadians(1.0f);
		constexpr float MaxFovPP = MathUtil::DegreesToRadians(175.0f);

		FMatrix PSMCameraView = FMatrix::Identity;
		FMatrix PSMCameraProj = FMatrix::Identity;
		BuildPSMCameraViewProjection(Light, RenderBus, PSMCameraView, PSMCameraProj);

		const FVector EyeLightDirection = PSMCameraView.TransformVector(ToLightDirection);
		const FVector4 LightPP = TransformVector4ByMatrix(FVector4(EyeLightDirection, 0.0f), PSMCameraProj);
		const bool bUseOrthoMatrix = std::fabs(LightPP.W) <= WThreshold;
		const bool bLightIsBehindEye = LightPP.W < -WThreshold;

		FMatrix ViewPP = FMatrix::Identity;
		FMatrix ProjPP = FMatrix::Identity;

		if (bUseOrthoMatrix)
		{
			if (!BuildOrthographicPostProjectiveViewProjection(
				FVector(LightPP.X, LightPP.Y, LightPP.Z),
				CubeCenterPP,
				CubeRadiusPP,
				MinPlaneGap,
				ViewPP,
				ProjPP))
			{
				return false;
			}
		}
		else
		{
			const float InvW = 1.0f / LightPP.W;
			const FVector LightPositionPP(LightPP.X * InvW, LightPP.Y * InvW, LightPP.Z * InvW);

			const FVector LookAtCubePP = CubeCenterPP - LightPositionPP;
			const float DistToCube = LookAtCubePP.Size();
			if (DistToCube <= MathUtil::KindaSmallNumber)
			{
				return false;
			}

			// The original OpenGL PSM uses a negative-near projection when the
			// light is behind the eye. In our D3D LESS-depth path that reverses
			// depth ordering, so keep this case on an orthographic PP fallback.
			if (bLightIsBehindEye || DistToCube <= CubeRadiusPP + MinNearPlane)
			{
				FVector FallbackLightDirectionPP = bLightIsBehindEye
					? FVector(LightPP.X, LightPP.Y, LightPP.Z)
					: LightPositionPP - CubeCenterPP;
				if (FallbackLightDirectionPP.IsNearlyZero())
				{
					FallbackLightDirectionPP = FVector(LightPP.X, LightPP.Y, LightPP.Z);
				}

				if (!BuildOrthographicPostProjectiveViewProjection(
					FallbackLightDirectionPP,
					CubeCenterPP,
					CubeRadiusPP,
					MinPlaneGap,
					ViewPP,
					ProjPP))
				{
					return false;
				}
			}
			else
			{
				const FVector ViewDirectionPP = LookAtCubePP * (1.0f / DistToCube);
				const float SinHalfFovPP = MathUtil::Clamp(CubeRadiusPP / DistToCube, 0.0f, 1.0f);
				// asin gives the tangent cone that contains the whole bounding sphere.
				const float FovPP = MathUtil::Clamp(2.0f * std::asin(SinHalfFovPP), MinFovPP, MaxFovPP);
				const float NearPP = std::max(MinNearPlane, DistToCube - CubeRadiusPP);
				const float FarPP = std::max(NearPP + MinPlaneGap, DistToCube + CubeRadiusPP);

				ViewPP = FMatrix::MakeViewLookAtLH(LightPositionPP, CubeCenterPP, MakeStableUpVector(ViewDirectionPP));
				ProjPP = FMatrix::MakePerspectiveFovLH(FovPP, 1.0f, NearPP, FarPP);
			}
		}

		OutResult.LightViewProj = PSMCameraView * PSMCameraProj * ViewPP * ProjPP;
		return true;
	}

	void PackDirectionalCSMShadowConstants(const FDirectionalCSMBuildResult& BuildResult, FDirectionalShadowConstants& OutConstants)
	{
		for (int32 i = 0; i < MAX_CASCADE_COUNT; ++i)
		{
			OutConstants.LightViewProj[i] = BuildResult.LightViewProj[i];
		}

		OutConstants.SplitDistances = BuildResult.SplitDistances;
		OutConstants.CascadeRadius = BuildResult.CascadeRadius;
		OutConstants.ShadowMode = DirectionalShadowModeValue::CSM;
	}

	void PackDirectionalPSMShadowConstants(const FDirectionalPSMBuildResult& BuildResult, FDirectionalShadowConstants& OutConstants)
	{
		for (int32 i = 0; i < MAX_CASCADE_COUNT; ++i)
		{
			OutConstants.LightViewProj[i] = BuildResult.LightViewProj;
		}

		constexpr float PSMCascadeSplitSentinel = 1.0e30f;
		OutConstants.SplitDistances = FVector4(
			PSMCascadeSplitSentinel,
			PSMCascadeSplitSentinel,
			PSMCascadeSplitSentinel,
			PSMCascadeSplitSentinel);
		OutConstants.CascadeRadius = FVector4::ZeroVector();
		OutConstants.ShadowMode = DirectionalShadowModeValue::PSM;
	}

}
void FLightRenderCollector::CollectAmbientLight(FRenderLight& RenderLight, FRenderBus& RenderBus)
{
	++GetStats().Shadow.AmbientLightCount;
	RenderBus.AddLight(RenderLight);
}

void FLightRenderCollector::CollectDirectionalLight(const ULightComponent* LightComponent, FRenderLight& RenderLight, FRenderBus& RenderBus)
{
	++GetStats().Shadow.DirectionalLightCount;

	FVector Direction = LightComponent->GetForwardVector() * -1.0f; // 빛 방향 벡터
	Direction.Normalize();
	RenderLight.Direction = Direction;

	// 씬의 첫 번째 Directional Light만 그림자를 반영한다.
	if (RenderBus.GetShowFlags().bShadow && !RenderBus.HasDirectionalShadow() && LightComponent->IsCastShadows())
	{
		const UDirectionalLightComponent* DirectionalLight = Cast<UDirectionalLightComponent>(LightComponent);
		if (DirectionalLight != nullptr)
		{
			FDirectionalShadowConstants ShadowConstants;
			ShadowConstants.ShadowBias = LightComponent->GetShadowBias();
			ShadowConstants.ShadowSlopeBias = LightComponent->GetShadowSlopeBias();
			ShadowConstants.ShadowSharpen = LightComponent->GetShadowSharpen();
			ShadowConstants.bCascadeDebug = RenderBus.GetViewMode() == EViewMode::CascadeShadow ? 1 : 0;

			bool bBuiltDirectionalShadow = false;
			switch (DirectionalLight->GetShadowMode())
			{
			case EShadowMode::CSM:
			{
				FDirectionalCSMBuildResult CSMResult = {};
				if (BuildDirectionalCSMViewProjection(DirectionalLight, RenderBus, RenderLight.Direction, CSMResult))
				{
					PackDirectionalCSMShadowConstants(CSMResult, ShadowConstants);
					bBuiltDirectionalShadow = true;
				}
				break;
			}
			case EShadowMode::PSM:
			{
				FDirectionalPSMBuildResult PSMResult = {};
				if (BuildDirectionalPSMViewProjection(DirectionalLight, RenderBus, RenderLight.Direction, PSMResult))
				{
					PackDirectionalPSMShadowConstants(PSMResult, ShadowConstants);
					bBuiltDirectionalShadow = true;
				}
				break;
			}
			default:
				break;
			}

			if (bBuiltDirectionalShadow)
			{
				ShadowConstants.ShadowFilterType = static_cast<uint32>(RenderBus.GetShadowFilterType());
				RenderBus.SetDirectionalShadow(ShadowConstants);
				RenderLight.bCastShadows = 1; // uint32
				GetStats().Shadow.DirectionalShadowConstants = ShadowConstants;
				GetStats().Shadow.DirectionalShadowCount = 1;

				const uint32 DirectionalTileCount =
					DirectionalLight->GetShadowMode() == EShadowMode::PSM ? 1u : FShadowAtlasManager::DirectionalCascadeCount;
				GetStats().Shadow.DirectionalShadowMemoryBytes =
					CalculateShadowTileMemory(
						FShadowAtlasManager::DirectionalCascadeResolution,
						FShadowAtlasManager::DirectionalCascadeResolution) * DirectionalTileCount;
			}
		}
	}

	RenderBus.AddLight(RenderLight);
}

void FLightRenderCollector::CollectPointLight(
	const ULightComponent* LightComponent,
	FRenderLight& RenderLight,
	FRenderBus& RenderBus,
	const FFrustum* ViewFrustum,
	int32& NextPointShadowIndex)
{
	const UPointLightComponent* PointLight = Cast<UPointLightComponent>(LightComponent);
	if (PointLight == nullptr)
	{
		return;
	}

	const FVector LightLocation = PointLight->GetWorldLocation();
	const float Attenuation = PointLight->GetAttenuationRadius();

	// View Frustum에 대한 Bounding Sphere 교차 검사
	if (ViewFrustum && !ViewFrustum->IntersectsBoundingSphere(LightLocation, Attenuation))
	{
		return;
	}

	RenderLight.Position = LightLocation;
	RenderLight.Radius = Attenuation;
	RenderLight.FalloffExponent = PointLight->GetLightFalloffExponent();

	if (!RenderBus.GetShowFlags().bShadow || !LightComponent->IsCastShadows())
	{
		RenderBus.AddLight(RenderLight);
		return;
	}

	if (NextPointShadowIndex >= MaxPointShadowCount)
	{
		RenderBus.AddLight(RenderLight);
		return;
	}

	const float RequestedPointResolution = MakePointShadowResolution(LightComponent);
	const uint32 PointTileResolution = FShadowAtlasManager::SnapPointTileSize(RequestedPointResolution);

	FPointAtlasSlotDesc PointAtlasSlot = {};
	if (!FShadowAtlasManager::RequestPointAtlasSlot(PointTileResolution, PointAtlasSlot))
	{
		RenderBus.AddLight(RenderLight);
		return;
	}

	const int32 PointShadowIndex = NextPointShadowIndex++;
	const float NearPlane = PointShadowNearPlane;
	const float FarPlane = std::max(Attenuation, NearPlane + 1.0f);
	const float ShadowBias = LightComponent->GetShadowBias();
	const float ShadowSharpen = LightComponent->GetShadowSharpen();
	const float ShadowSlopeBias = LightComponent->GetShadowSlopeBias();

	RenderLight.bCastShadows = 1;
	RenderLight.ShadowMapIndex = PointShadowIndex;
	RenderLight.ShadowBias = ShadowBias;

	FPointShadowConstants ShadowData = {};
	MakePointShadowViewProjections(LightLocation, NearPlane, FarPlane, ShadowData.LightViewProj);
	ShadowData.LightPosition = LightLocation;
	ShadowData.FarPlane = FarPlane;

	for (uint32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
	{
		ShadowData.FaceAtlasRects[FaceIndex] = PointAtlasSlot.FaceAtlasRects[FaceIndex];
	}

	ShadowData.ShadowBias = ShadowBias;
	ShadowData.ShadowResolution = static_cast<float>(PointAtlasSlot.TileResolution);
	ShadowData.ShadowSharpen = ShadowSharpen;
	ShadowData.ShadowSlopeBias = ShadowSlopeBias;
	ShadowData.bHasShadowMap = 1;

	++GetStats().Shadow.PointShadowCount;
	GetStats().Shadow.PointShadowMemoryBytes += CalculateShadowTileMemory(
		PointAtlasSlot.TileResolution,
		PointAtlasSlot.TileResolution) * FShadowAtlasManager::PointCubeFaceCount;

	RenderBus.AddCastPointShadowLight(ShadowData);
	RenderBus.AddLight(RenderLight);
}

void FLightRenderCollector::CollectSpotLight(
	const ULightComponent* LightComponent,
	FRenderLight& RenderLight,
	FRenderBus& RenderBus,
	const FFrustum* ViewFrustum,
	TArray<FSpotShadowCandidate>& SpotShadowCandidates)
{
	const USpotLightComponent* SpotLight = Cast<USpotLightComponent>(LightComponent);
	if (SpotLight == nullptr)
	{
		return;
	}

	const FVector LightLocation = SpotLight->GetWorldLocation();
	const float Attenuation = SpotLight->GetAttenuationRadius();
	const float InnerAngle = SpotLight->GetInnerConeAngle(); // Degree 단위 주의
	const float OuterAngle = SpotLight->GetOuterConeAngle();

	// -z 축을 forward로 사용
	FVector LightDirection = SpotLight->GetUpVector() * -1.0f;
	LightDirection.Normalize();

	// 원뿔 각도에 따라 줄어든 Bounding Sphere 교차 검사
	if (ViewFrustum)
	{
		const float SpotAngle = MathUtil::Clamp(std::max(OuterAngle, InnerAngle), 0.0f, 89.0f);
		const float SpotRadian = MathUtil::DegreesToRadians(SpotAngle);

		FVector Center = LightLocation;
		float Radius = Attenuation;

		if (SpotAngle <= 45.0f)
		{
			const float Offset = Attenuation * 0.5f;

			Center = LightLocation + (LightDirection * Offset);

			const float TanAngle = std::tan(SpotRadian);
			const float BaseRadius = Attenuation * TanAngle;

			Radius = std::sqrt((Offset * Offset) + (BaseRadius * BaseRadius));
		}

		if (!ViewFrustum->IntersectsBoundingSphere(Center, Radius))
		{
			return;
		}
	}

	++GetStats().Shadow.SpotLightCount;

	RenderLight.Position = LightLocation;
	RenderLight.Direction = LightDirection;
	RenderLight.Radius = Attenuation;
	RenderLight.FalloffExponent = SpotLight->GetLightFalloffExponent();
	RenderLight.SpotInnerCos = std::cos(MathUtil::DegreesToRadians(InnerAngle));
	RenderLight.SpotOuterCos = std::cos(MathUtil::DegreesToRadians(OuterAngle));

	if (!RenderBus.GetShowFlags().bShadow || !LightComponent->IsCastShadows())
	{
		RenderBus.AddLight(RenderLight);
		return;
	}

	FSpotShadowCandidate Candidate = {};
	Candidate.RenderLight = RenderLight;
	Candidate.LightComponent = LightComponent;
	Candidate.SpotLight = SpotLight;
	Candidate.LightDirection = LightDirection;

	Candidate.RequestedResolution = MakeSpotShadowResolution(LightComponent);
	Candidate.RequestedTileSize = FShadowAtlasManager::SnapSpotTileSize(Candidate.RequestedResolution);
	Candidate.PriorityScore = ComputeSpotShadowPriority(LightComponent, LightLocation, Attenuation, RenderBus.GetCameraPosition());

	SpotShadowCandidates.push_back(Candidate);
}

void FLightRenderCollector::AllocateSpotShadowCandidates(
	TArray<FSpotShadowCandidate>& SpotShadowCandidates,
	FRenderBus& RenderBus,
	int32& NextSpotShadowIndex)
{
	// Priority가 높은 라이트부터 atlas에 넣고, 같은 priority라면 큰 타일부터 먼저 넣음.
	std::sort(SpotShadowCandidates.begin(), SpotShadowCandidates.end(),
		[](const FSpotShadowCandidate& A, const FSpotShadowCandidate& B)
		{
			if (std::fabs(A.PriorityScore - B.PriorityScore) > 1.0e-4f)
			{
				return A.PriorityScore > B.PriorityScore;
			}
			if (A.RequestedTileSize != B.RequestedTileSize)
			{
				return A.RequestedTileSize > B.RequestedTileSize;
			}
			return A.RenderLight.Intensity > B.RenderLight.Intensity;
		});

	// 정렬된 순서대로 atlas 영역을 배정.
	for (FSpotShadowCandidate& Candidate : SpotShadowCandidates)
	{
		FSpotAtlasSlotDesc SpotSlot = {};
		bool bAllocated = false;

		// 1차 시도: 라이트가 원한 타일 크기로 먼저 배정
		uint32 AttemptTileSize = Candidate.RequestedTileSize;
		while (true)
		{
			if (FShadowAtlasManager::RequestSpotSlot(AttemptTileSize, SpotSlot))
			{
				bAllocated = true;
				break;
			}

			// 실패하면 절반 크기로 낮춰서 다시 시도
			if (AttemptTileSize <= FShadowAtlasManager::MinSpotTileResolution)
			{
				break;
			}

			AttemptTileSize >>= 1u;
			if (AttemptTileSize < FShadowAtlasManager::MinSpotTileResolution)
			{
				AttemptTileSize = FShadowAtlasManager::MinSpotTileResolution;
			}
		}

		FRenderLight FinalLight = Candidate.RenderLight;

		if (bAllocated)
		{
			const int32 ShadowMapIndex = NextSpotShadowIndex++;
			const float NearPlane = SpotShadowNearPlane;
			const float FarPlane = MakeSpotShadowFarPlane(Candidate.SpotLight);
			const float ShadowBias = Candidate.LightComponent->GetShadowBias();
			const float ShadowSlopeBias = Candidate.LightComponent->GetShadowSlopeBias();
			const float ShadowSharpen = Candidate.LightComponent->GetShadowSharpen();
			const int32 DebugLightId = ExtractActorNumericSuffix(Candidate.LightComponent->GetOwner());

			FinalLight.bCastShadows = 1;
			FinalLight.ShadowMapIndex = ShadowMapIndex;
			FinalLight.ShadowBias = ShadowBias;

			SpotSlot.DebugLightId = DebugLightId;
			FShadowAtlasManager::UpdateSpotSlotDebugLightId(SpotSlot.TileIndex, DebugLightId);

			FSpotShadowConstants ShadowData = {};
			ShadowData.LightViewProj = MakeSpotShadowViewProjection(Candidate.SpotLight, Candidate.LightDirection, NearPlane, FarPlane);
			ShadowData.AtlasRect = SpotSlot.AtlasRect;
			ShadowData.ShadowSlopeBias = ShadowSlopeBias;
			ShadowData.ShadowBias = ShadowBias;
			ShadowData.ShadowSharpen = ShadowSharpen;
			ShadowData.ShadowFarPlane = FarPlane;

			RenderBus.AddCastShadowSpotLight(ShadowData);
			++GetStats().Shadow.SpotShadowCount;
			GetStats().Shadow.SpotShadowMemoryBytes += CalculateShadowTileMemory(SpotSlot.Width, SpotSlot.Height);
		}

		// 끝까지 atlas에 못 들어간 경우에는 shadow만 빠지고 light만 살아있음.
		RenderBus.AddLight(FinalLight);
	}
}

void FLightRenderCollector::CollectLight(
	UWorld* World,
	FRenderBus& RenderBus,
	FRenderCollectionStats& LastStats,
	const FFrustum* ViewFrustum)
{
	CurrentStats = &LastStats;

	const TArray<FLightSlot>& LightSlots = World->GetWorldLightSlots();
	int32 NextSpotShadowIndex = 0;
	int32 NextPointShadowIndex = 0;

	// shadow-casting Spot Light 후보를 잠시 모아두는 배열입니다.
	// Spot shadow는 "보이는 순서"가 아니라 "중요한 라이트 순서"로 atlas에 넣어야 하므로,
	// Spot Light를 발견하자마자 바로 할당하지 않고 먼저 후보를 수집합니다.
	TArray<FSpotShadowCandidate> SpotShadowCandidates;

	// Spot atlas allocation 상태는 프레임마다 다시 시작함.
	FShadowAtlasManager::BeginSpotFrame();
	FShadowAtlasManager::BeginPointFrame();

	for (const FLightSlot& Slot : LightSlots)
	{
		if (!Slot.bAlive || !Slot.LightData)
			continue;

		const ULightComponent* LightComponent = Cast<ULightComponent>(Slot.LightData);
		if (LightComponent == nullptr || !LightComponent->IsVisible())
		{
			continue;
		}

		FRenderLight RenderLight = {};
		RenderLight.Type = static_cast<uint32>(LightComponent->GetLightType());
		RenderLight.Color = MakeLightColorVector(LightComponent);
		RenderLight.Intensity = LightComponent->GetIntensity();

		switch (LightComponent->GetLightType())
		{
		case ELightType::LightType_AmbientLight:
			CollectAmbientLight(RenderLight, RenderBus);
			break;
		case ELightType::LightType_Directional:
			CollectDirectionalLight(LightComponent, RenderLight, RenderBus);
			break;
		case ELightType::LightType_Point:
			CollectPointLight(LightComponent, RenderLight, RenderBus, ViewFrustum, NextPointShadowIndex);
			break;
		case ELightType::LightType_Spot:
			CollectSpotLight(LightComponent, RenderLight, RenderBus, ViewFrustum, SpotShadowCandidates);
			break;
		default:
			break;
		}
	}

	AllocateSpotShadowCandidates(SpotShadowCandidates, RenderBus, NextSpotShadowIndex);
	CurrentStats = nullptr;
}

// 조명별 shadow 영향 볼륨으로 BVH를 조회해 shadow caster command를 수집합니다.
void FLightRenderCollector::CollectShadowCasters(UWorld* World, FRenderBus& RenderBus)
{
	if (World == nullptr || MeshBufferManager == nullptr)
	{
		return;
	}

	const EWorldType WorldType = World->GetWorldType();
	std::unordered_set<UPrimitiveComponent*> AddedPrimitives;

	auto AddShadowCaster = [&](UPrimitiveComponent* Primitive)
	{
		if (Primitive == nullptr || !Primitive->IsVisible()) return;
		if (Primitive->IsEditorOnly() && WorldType != EWorldType::Editor) return;
		if (Primitive->GetPrimitiveType() != EPrimitiveType::EPT_StaticMesh) return;
		if (!AddedPrimitives.insert(Primitive).second) return;

		UStaticMeshComponent* StaticMeshComp = static_cast<UStaticMeshComponent*>(Primitive);
		const UStaticMesh* StaticMesh = StaticMeshComp->GetStaticMesh();
		if (StaticMesh == nullptr || !StaticMesh->HasValidMeshData()) return;

		FMeshBuffer* MeshBuffer = MeshBufferManager->GetStaticMeshBuffer(StaticMesh, 0);
		if (MeshBuffer == nullptr || !MeshBuffer->IsValid()) return;

		const FStaticMesh* MeshData = StaticMesh->GetMeshData(0);
		if (MeshData == nullptr) return;

		for (const FStaticMeshSection& Section : MeshData->Sections)
		{
			FRenderCommand Cmd = {};
			Cmd.PerObjectConstants = FPerObjectConstants{ Primitive->GetWorldMatrix(), FColor::White().ToVector4() };
			Cmd.Type = ERenderCommandType::StaticMesh;
			Cmd.MeshBuffer = MeshBuffer;
			Cmd.SectionIndexStart = Section.StartIndex;
			Cmd.SectionIndexCount = Section.IndexCount;

			RenderBus.AddCommand(ERenderPass::ShadowCasters, Cmd);
		}
	};

	auto AddQueryResults = [&]()
	{
		for (UPrimitiveComponent* Primitive : ShadowVisiblePrimitiveScratch)
		{
			AddShadowCaster(Primitive);
		}
	};

	if (const FDirectionalShadowConstants* DirectionalShadow = RenderBus.GetDirectionalShadow())
	{
		const int32 DirectionalQueryCount =
			(DirectionalShadow->ShadowMode == DirectionalShadowModeValue::PSM) ? 1 : MAX_CASCADE_COUNT;
		for (int32 CascadeIndex = 0; CascadeIndex < DirectionalQueryCount; ++CascadeIndex)
		{
			FFrustum CascadeFrustum;
			CascadeFrustum.UpdateFromCamera(DirectionalShadow->LightViewProj[CascadeIndex]);
			World->GetSpatialIndex().FrustumQueryPrimitives(CascadeFrustum, ShadowVisiblePrimitiveScratch, ShadowFrustumQueryScratch);
			AddQueryResults();
		}
	}

	for (const FLightSlot& Slot : World->GetWorldLightSlots())
	{
		const ULightComponent* Light = Cast<ULightComponent>(Slot.LightData);
		if (!Slot.bAlive || Light == nullptr || !Light->IsVisible() || !Light->IsCastShadows()) continue;

		FVector Center = FVector::ZeroVector;
		float Radius = 0.0f;

		if (Light->GetLightType() == ELightType::LightType_Point)
		{
			const UPointLightComponent* PointLight = Cast<UPointLightComponent>(Light);
			if (PointLight == nullptr) continue;

			Center = PointLight->GetWorldLocation();
			Radius = PointLight->GetAttenuationRadius();
		}
		else if (Light->GetLightType() == ELightType::LightType_Spot)
		{
			const USpotLightComponent* SpotLight = Cast<USpotLightComponent>(Light);
			if (SpotLight == nullptr) continue;

			const float SpotAngle = MathUtil::Clamp(std::max(SpotLight->GetOuterConeAngle(), SpotLight->GetInnerConeAngle()), 0.0f, 89.0f);
			Center = SpotLight->GetWorldLocation();
			Radius = SpotLight->GetAttenuationRadius();

			if (SpotAngle <= 45.0f)
			{
				const float Offset = Radius * 0.5f;
				const float BaseRadius = Radius * std::tan(MathUtil::DegreesToRadians(SpotAngle));
				Center += (SpotLight->GetUpVector() * -1.0f).GetSafeNormal() * Offset;
				Radius = std::sqrt((Offset * Offset) + (BaseRadius * BaseRadius));
			}
		}
		else
		{
			continue;
		}

		World->GetSpatialIndex().SphereQueryPrimitives(Center, Radius, ShadowVisiblePrimitiveScratch, ShadowSphereQueryScratch);
		AddQueryResults();
	}
}
