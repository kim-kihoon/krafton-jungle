#include "Renderer.h"

#include "Render/Types/RenderTypes.h"
#include "Render/Resource/ShaderManager.h"
#include "Render/Resource/TexturePool/TextureAtlasPool.h"
#include "Render/Resource/TexturePool/TextureCubeShadowPool.h"
#include "Core/Log.h"
#include "Render/Proxy/FScene.h"
#include "Render/Proxy/SceneEnvironment.h"
#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "GameFramework/World.h"
#include "Component/Light/PointLightComponent.h"
#include "Component/Light/SpotLightComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Profiling/Stats.h"
#include "Profiling/GPUProfiler.h"
#include "Materials/MaterialManager.h"
#include "Math/MathUtils.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace
{
	const char* GetShadowLogWorldTypeName(EWorldType WorldType)
	{
		switch (WorldType)
		{
		case EWorldType::Editor: return "Editor";
		case EWorldType::Game: return "Game";
		case EWorldType::PIE: return "PIE";
		default: return "Unknown";
		}
	}

	struct FShadowPassConstants
	{
		FMatrix LightVP;
		FMatrix CameraVP;
		uint32  bIsPSM;
		uint32  bPSMFlipNegativeW;
		uint32  _pad[2];
	};

	struct FVSMBlurPassConstants
	{
		float SourceUVRect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
		float InvTextureSize[2] = { 1.0f, 1.0f };
		float BlurDirection[2] = { 1.0f, 0.0f };
		uint32 SourceSlice = 0;
		uint32 Padding[3] = {};
	};

	struct FPerspectiveShadowDebugData
	{
		FMatrix PostPerspectiveLightVP = FMatrix::Identity;
		FVector4 LightPP = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
		bool bFlipNegativeW = false;
	};

	FVector4 TransformVector4(const FVector4& Vector, const FMatrix& Matrix)
	{
		return FVector4(
			Vector.X * Matrix.M[0][0] + Vector.Y * Matrix.M[1][0] + Vector.Z * Matrix.M[2][0] + Vector.W * Matrix.M[3][0],
			Vector.X * Matrix.M[0][1] + Vector.Y * Matrix.M[1][1] + Vector.Z * Matrix.M[2][1] + Vector.W * Matrix.M[3][1],
			Vector.X * Matrix.M[0][2] + Vector.Y * Matrix.M[1][2] + Vector.Z * Matrix.M[2][2] + Vector.W * Matrix.M[3][2],
			Vector.X * Matrix.M[0][3] + Vector.Y * Matrix.M[1][3] + Vector.Z * Matrix.M[2][3] + Vector.W * Matrix.M[3][3]);
	}

	class FShadowUtil
	{
	public:
		static FBoundingBox MakeSphereBounds(const FVector& Center, float Radius)
		{
			const FVector Extent(Radius, Radius, Radius);
			return FBoundingBox(Center - Extent, Center + Extent);
		}

		static FMatrix MakeAxesViewMatrix(const FVector& Eye, FVector Right, FVector Up, FVector Forward)
		{
			Right.Normalize();
			Up.Normalize();
			Forward.Normalize();

			return FMatrix(
				Right.X, Up.X, Forward.X, 0.0f,
				Right.Y, Up.Y, Forward.Y, 0.0f,
				Right.Z, Up.Z, Forward.Z, 0.0f,
				-Eye.Dot(Right), -Eye.Dot(Up), -Eye.Dot(Forward), 1.0f);
		}

		static FMatrix MakeReversedZPerspective(float VerticalFovRadians, float AspectRatio, float NearZ, float FarZ)
		{
			const float Cot = 1.0f / tanf(VerticalFovRadians * 0.5f);
			const float Denom = NearZ - FarZ;
			return FMatrix(
				Cot / AspectRatio, 0.0f, 0.0f, 0.0f,
				0.0f, Cot, 0.0f, 0.0f,
				0.0f, 0.0f, NearZ / Denom, 1.0f,
				0.0f, 0.0f, -(FarZ * NearZ) / Denom, 0.0f);
		}

		static FMatrix MakeViewToTarget(const FVector& Eye, const FVector& Target)
		{
			FVector Forward = (Target - Eye).Normalized();
			if (Forward.Length() <= 0.0001f)
			{
				Forward = FVector(0.0f, 0.0f, 1.0f);
			}

			FVector UpHint(0.0f, 1.0f, 0.0f);
			if (fabsf(UpHint.Dot(Forward)) > 0.99f)
			{
				UpHint = FVector(1.0f, 0.0f, 0.0f);
			}

			FVector Right = UpHint.Cross(Forward).Normalized();
			FVector Up = Forward.Cross(Right).Normalized();
			return MakeAxesViewMatrix(Eye, Right, Up, Forward);
		}

		static FMatrix MakePointShadowProjection(float AttenuationRadius, float& OutNearZ, float& OutFarZ)
		{
			OutNearZ = FMath::Clamp(AttenuationRadius * 0.01f, 0.05f, 5.0f);
			OutFarZ = AttenuationRadius > OutNearZ ? AttenuationRadius : (OutNearZ + 1.0f);
			return MakeReversedZPerspective(FMath::Pi * 0.5f, 1.0f, OutNearZ, OutFarZ);
		}

		static FMatrix MakeReversedZOrthographic(float Width, float Height, float NearZ, float FarZ)
		{
			const float HalfW = Width * 0.5f;
			const float HalfH = Height * 0.5f;
			const float Denom = NearZ - FarZ;
			return FMatrix(
				1.0f / HalfW, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f / HalfH, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f / Denom, 0.0f,
				0.0f, 0.0f, -FarZ / Denom, 1.0f);
		}

		static FVector SnapCenterToShadowTexels(
			const FVector& Center,
			const FVector& Right,
			const FVector& Up,
			float Width,
			float Height,
			float ShadowMapWidth,
			float ShadowMapHeight)
		{
			const float TexelSizeX = Width / std::max(ShadowMapWidth, 1.0f);
			const float TexelSizeY = Height / std::max(ShadowMapHeight, 1.0f);
			if (TexelSizeX <= 0.0f || TexelSizeY <= 0.0f)
			{
				return Center;
			}

			const float CenterX = Center.Dot(Right);
			const float CenterY = Center.Dot(Up);
			const float SnappedX = std::round(CenterX / TexelSizeX) * TexelSizeX;
			const float SnappedY = std::round(CenterY / TexelSizeY) * TexelSizeY;
			return Center + Right * (SnappedX - CenterX) + Up * (SnappedY - CenterY);
		}

		static bool MakeDirectionalShadowMatrix(
			const FFrameContext& Frame,
			const UDirectionalLightComponent& DirectionalLight,
			FMatrix& OutLightVP,
			float& OutNearZ,
			float ShadowMapWidth,
			float ShadowMapHeight)
		{
			const float ShadowDistance = FMath::Clamp(Frame.FarClip * 0.12f, 12.0f, 60.0f);
			const float ShadowExtent = FMath::Clamp(Frame.FarClip * 0.1f, 15.0f, 60.0f);
			const FVector LightRight = DirectionalLight.GetRightVector();
			const FVector LightUp = DirectionalLight.GetUpVector();
			const FVector LightDir = DirectionalLight.GetForwardVector();
			const FVector CameraCenter = SnapCenterToShadowTexels(
				Frame.CameraPosition + Frame.CameraForward * (ShadowExtent * 0.5f),
				LightRight,
				LightUp,
				ShadowExtent * 2.0f,
				ShadowExtent * 2.0f,
				ShadowMapWidth,
				ShadowMapHeight);
			const FVector Eye = CameraCenter - DirectionalLight.GetForwardVector() * ShadowDistance;

			const FMatrix LightView = MakeAxesViewMatrix(
				Eye,
				LightRight,
				LightUp,
				LightDir);

			const float NearZ = 0.1f;
			const FMatrix LightProj = MakeReversedZOrthographic(
				ShadowExtent * 2.0f,
				ShadowExtent * 2.0f,
				NearZ,
				ShadowDistance + ShadowExtent * 2.0f);

			OutLightVP = LightView * LightProj;
			OutNearZ = NearZ;
			return true;
		}

		static bool MakePerspectiveShadowMatrix(
			const FFrameContext& Frame,
			const UDirectionalLightComponent& DirectionalLight,
			FMatrix& OutLightVP,
			float& OutNearZ,
			bool& bOutFlipNegativeW,
			FPerspectiveShadowDebugData* OutDebugData = nullptr)
		{
			bOutFlipNegativeW = false;
			if (Frame.bIsOrtho)
			{
				return false;
			}

			const float VirtualSlideBack = FMath::Clamp(Frame.FarClip * 0.01f, 1.0f, 10.0f);
			const float VirtualNearZ = FMath::Clamp(Frame.NearClip + VirtualSlideBack, Frame.NearClip, Frame.FarClip - 1.0f);
			const float Aspect = Frame.Proj.M[1][1] / Frame.Proj.M[0][0];
			const float VerticalFov = 2.0f * atanf(1.0f / Frame.Proj.M[1][1]);
			const FVector VirtualCameraPosition = Frame.CameraPosition - Frame.CameraForward * VirtualSlideBack;
			const FMatrix VirtualCameraView = MakeAxesViewMatrix(
				VirtualCameraPosition,
				Frame.CameraRight,
				Frame.CameraUp,
				Frame.CameraForward);
			const FMatrix VirtualCameraProj = MakeReversedZPerspective(VerticalFov, Aspect, VirtualNearZ, Frame.FarClip);
			const FMatrix CameraVP = VirtualCameraView * VirtualCameraProj;

			const FVector CubeCenterPP(0.0f, 0.0f, 0.5f);
			const float CubeRadiusPP = FVector(1.0f, 1.0f, 0.5f).Length();
			const FVector LightTravelDir = DirectionalLight.GetForwardVector().Normalized();
			const FVector LightSourceDir = (LightTravelDir * -1.0f).Normalized();
			const FVector EyeLightDir = VirtualCameraView.TransformVector(LightSourceDir);
			const FVector4 LightPP = TransformVector4(FVector4(EyeLightDir, 0.0f), VirtualCameraProj);
			if (OutDebugData)
			{
				OutDebugData->LightPP = LightPP;
			}

			const float W_Epsilon = 0.001f;
			const bool bLightAtInfinity = fabsf(LightPP.W) <= W_Epsilon;
			FMatrix ViewPP = FMatrix::Identity;
			FMatrix ProjPP = FMatrix::Identity;

			if (bLightAtInfinity)
			{
				FVector LightDirPP(LightPP.X, LightPP.Y, LightPP.Z);
				if (LightDirPP.Length() <= 0.0001f)
				{
					return false;
				}
				LightDirPP.Normalize();

				const FVector LightPosPP = CubeCenterPP + LightDirPP * (CubeRadiusPP * 2.0f);
				const float DistToCenter = (LightPosPP - CubeCenterPP).Length();
				OutNearZ = std::max(0.001f, DistToCenter - CubeRadiusPP);
				ViewPP = MakeViewToTarget(LightPosPP, CubeCenterPP);
				ProjPP = MakeReversedZOrthographic(CubeRadiusPP * 2.0f, CubeRadiusPP * 2.0f, OutNearZ, DistToCenter + CubeRadiusPP);
			}
			else
			{
				const FVector LightPosPP(
					LightPP.X / LightPP.W,
					LightPP.Y / LightPP.W,
					LightPP.Z / LightPP.W);
				FVector LookAtCubePP = CubeCenterPP - LightPosPP;
				float DistToCenter = LookAtCubePP.Length();
				if (DistToCenter <= 0.0001f)
				{
					return false;
				}
				LookAtCubePP /= DistToCenter;

				ViewPP = MakeViewToTarget(LightPosPP, CubeCenterPP);
				const float FovPP = std::min(2.8f, 2.0f * atanf(CubeRadiusPP / DistToCenter));

				if (LightPP.W < 0.0f)
				{
					const float NearMagnitude = std::max(0.1f, DistToCenter - CubeRadiusPP);
					OutNearZ = NearMagnitude;
					ProjPP = MakeReversedZPerspective(FovPP, 1.0f, -NearMagnitude, NearMagnitude);
					bOutFlipNegativeW = true;
				}
				else
				{
					const float NearPP = std::max(0.1f, DistToCenter - CubeRadiusPP);
					const float FarPP = std::max(NearPP + 0.001f, DistToCenter + CubeRadiusPP);
					OutNearZ = NearPP;
					ProjPP = MakeReversedZPerspective(FovPP, 1.0f, NearPP, FarPP);
				}
			}

			if (OutDebugData)
			{
				OutDebugData->PostPerspectiveLightVP = ViewPP * ProjPP;
				OutDebugData->bFlipNegativeW = bOutFlipNegativeW;
			}
			OutLightVP = CameraVP * ViewPP * ProjPP;
			return true;
		}

		static D3D11_VIEWPORT MakeAtlasViewport(const FAtlasUV& AtlasUV, uint32 AtlasTextureSize)
		{
			D3D11_VIEWPORT Viewport = {};
			Viewport.TopLeftX = AtlasUV.u1 * AtlasTextureSize;
			Viewport.TopLeftY = AtlasUV.v1 * AtlasTextureSize;
			Viewport.Width = (AtlasUV.u2 - AtlasUV.u1) * AtlasTextureSize;
			Viewport.Height = (AtlasUV.v2 - AtlasUV.v1) * AtlasTextureSize;
			Viewport.MinDepth = 0.0f;
			Viewport.MaxDepth = 1.0f;
			return Viewport;
		}

		static D3D11_VIEWPORT MakeFullViewport(uint32 TextureSize)
		{
			D3D11_VIEWPORT Viewport = {};
			Viewport.TopLeftX = 0.0f;
			Viewport.TopLeftY = 0.0f;
			Viewport.Width = static_cast<float>(TextureSize);
			Viewport.Height = static_cast<float>(TextureSize);
			Viewport.MinDepth = 0.0f;
			Viewport.MaxDepth = 1.0f;
			return Viewport;
		}
	};

	bool IsOpaqueShadowCaster(const FPrimitiveSceneProxy* Proxy)
	{
		return Proxy
			&& Proxy->IsVisible()
			&& Proxy->CastsShadow()
			&& Proxy->GetRenderPass() == ERenderPass::Opaque
			&& Proxy->GetMeshBuffer()
			&& Proxy->GetMeshBuffer()->IsValid();
	}

	D3D11_BOX MakeViewportBlurBox(const D3D11_VIEWPORT& Viewport, uint32 TextureSize, bool& bOutValid)
	{
		D3D11_BOX Box = {};
		Box.left = static_cast<UINT>(std::max(0.0f, std::floor(Viewport.TopLeftX)));
		Box.top = static_cast<UINT>(std::max(0.0f, std::floor(Viewport.TopLeftY)));
		Box.right = static_cast<UINT>(std::min(static_cast<float>(TextureSize), std::ceil(Viewport.TopLeftX + Viewport.Width)));
		Box.bottom = static_cast<UINT>(std::min(static_cast<float>(TextureSize), std::ceil(Viewport.TopLeftY + Viewport.Height)));
		Box.front = 0;
		Box.back = 1;
		bOutValid = Box.right > Box.left && Box.bottom > Box.top;
		return Box;
	}

	bool BuildSpaceCornersFromClip(const FMatrix& ClipFromSpace, FVector(&OutCorners)[8])
	{
		const FMatrix SpaceFromClip = ClipFromSpace.GetInverse();
		const FVector ClipCorners[8] = {
			FVector(-1.0f, -1.0f, 0.0f),
			FVector(1.0f, -1.0f, 0.0f),
			FVector(1.0f,  1.0f, 0.0f),
			FVector(-1.0f,  1.0f, 0.0f),
			FVector(-1.0f, -1.0f, 1.0f),
			FVector(1.0f, -1.0f, 1.0f),
			FVector(1.0f,  1.0f, 1.0f),
			FVector(-1.0f,  1.0f, 1.0f),
		};

		for (int32 i = 0; i < 8; ++i)
		{
			OutCorners[i] = SpaceFromClip.TransformPositionWithW(ClipCorners[i]);
			if (!std::isfinite(OutCorners[i].X)
				|| !std::isfinite(OutCorners[i].Y)
				|| !std::isfinite(OutCorners[i].Z))
			{
				return false;
			}
		}
		return true;
	}

	void AddDebugBoxLines(TArray<FEditorDebugLine>& Lines, const FVector(&Corners)[8], const FColor& Color)
	{
		static constexpr int32 Edges[12][2] = {
			{0, 1}, {1, 2}, {2, 3}, {3, 0},
			{4, 5}, {5, 6}, {6, 7}, {7, 4},
			{0, 4}, {1, 5}, {2, 6}, {3, 7},
		};

		for (const int32(&Edge)[2] : Edges)
		{
			Lines.push_back({ Corners[Edge[0]], Corners[Edge[1]], Color });
		}
	}

	FVector TransformPostPerspectiveDebugPoint(const FFrameContext& Frame, const FVector& Point, const FVector& Center, float Radius)
	{
		constexpr float Distance = 8.0f;
		constexpr float MiniScale = 1.6f;
		const FVector Anchor = Frame.CameraPosition
			+ Frame.CameraForward * Distance
			- Frame.CameraRight * 3.0f
			+ Frame.CameraUp * 1.8f;
		const FVector Local = (Point - Center) * (MiniScale / std::max(Radius, 0.001f));
		return Anchor
			+ Frame.CameraRight * Local.X
			+ Frame.CameraUp * Local.Y
			+ Frame.CameraForward * Local.Z;
	}

	void AddDebugBoxInPostPerspectiveMiniView(TArray<FEditorDebugLine>& Lines, const FFrameContext& Frame, const FVector(&Corners)[8], const FVector& Center, float Radius, const FColor& Color)
	{
		FVector WorldCorners[8];
		for (int32 i = 0; i < 8; ++i)
		{
			WorldCorners[i] = TransformPostPerspectiveDebugPoint(Frame, Corners[i], Center, Radius);
		}
		AddDebugBoxLines(Lines, WorldCorners, Color);
	}

	void ExpandDebugBounds(FBoundingBox& Bounds, const FVector& Point)
	{
		if (std::isfinite(Point.X) && std::isfinite(Point.Y) && std::isfinite(Point.Z))
		{
			Bounds.Expand(Point);
		}
	}

	void AddPerspectiveShadowDebug(TArray<FEditorDebugLine>& Lines, const FFrameContext& Frame, const FPerspectiveShadowDebugData& DebugData)
	{
		const FVector UnitCubePP[8] = {
			FVector(-1.0f, -1.0f, 0.0f),
			FVector(1.0f, -1.0f, 0.0f),
			FVector(1.0f,  1.0f, 0.0f),
			FVector(-1.0f,  1.0f, 0.0f),
			FVector(-1.0f, -1.0f, 1.0f),
			FVector(1.0f, -1.0f, 1.0f),
			FVector(1.0f,  1.0f, 1.0f),
			FVector(-1.0f,  1.0f, 1.0f),
		};

		FVector LightFrustumPP[8];
		const bool bHasLightFrustum = BuildSpaceCornersFromClip(DebugData.PostPerspectiveLightVP, LightFrustumPP);

		FBoundingBox Bounds;
		for (const FVector& Corner : UnitCubePP)
		{
			ExpandDebugBounds(Bounds, Corner);
		}
		if (bHasLightFrustum)
		{
			for (const FVector& Corner : LightFrustumPP)
			{
				ExpandDebugBounds(Bounds, Corner);
			}
		}

		constexpr float W_Epsilon = 0.001f;
		const bool bHasFiniteLightPP = fabsf(DebugData.LightPP.W) > W_Epsilon;
		FVector LightPosPP(0.0f, 0.0f, 0.5f);
		FVector LightDirPP(DebugData.LightPP.X, DebugData.LightPP.Y, DebugData.LightPP.Z);
		if (bHasFiniteLightPP)
		{
			LightPosPP = FVector(
				DebugData.LightPP.X / DebugData.LightPP.W,
				DebugData.LightPP.Y / DebugData.LightPP.W,
				DebugData.LightPP.Z / DebugData.LightPP.W);
			ExpandDebugBounds(Bounds, LightPosPP);
		}
		else if (LightDirPP.Length() > 0.0001f)
		{
			LightDirPP.Normalize();
			ExpandDebugBounds(Bounds, FVector(0.0f, 0.0f, 0.5f) - LightDirPP * 2.0f);
			ExpandDebugBounds(Bounds, FVector(0.0f, 0.0f, 0.5f) + LightDirPP * 2.0f);
		}

		const FVector Center = Bounds.GetCenter();
		const FVector Extent = Bounds.GetExtent();
		const float Radius = std::max(std::max(Extent.X, Extent.Y), Extent.Z);

		AddDebugBoxInPostPerspectiveMiniView(Lines, Frame, UnitCubePP, Center, Radius, FColor::Yellow());
		if (bHasLightFrustum)
		{
			AddDebugBoxInPostPerspectiveMiniView(Lines, Frame, LightFrustumPP, Center, Radius, FColor(255, 0, 255));
		}

		if (bHasFiniteLightPP)
		{
			const FVector LightWorld = TransformPostPerspectiveDebugPoint(Frame, LightPosPP, Center, Radius);
			const FVector CubeCenterWorld = TransformPostPerspectiveDebugPoint(Frame, FVector(0.0f, 0.0f, 0.5f), Center, Radius);
			const float CrossSize = 0.08f;
			Lines.push_back({ LightWorld - Frame.CameraRight * CrossSize, LightWorld + Frame.CameraRight * CrossSize, FColor::Red() });
			Lines.push_back({ LightWorld - Frame.CameraUp * CrossSize, LightWorld + Frame.CameraUp * CrossSize, FColor::Red() });
			Lines.push_back({ LightWorld - Frame.CameraForward * CrossSize, LightWorld + Frame.CameraForward * CrossSize, FColor::Red() });
			Lines.push_back({ LightWorld, CubeCenterWorld, FColor::Red() });
		}
		else if (LightDirPP.Length() > 0.0001f)
		{
			const FVector LineA = TransformPostPerspectiveDebugPoint(Frame, FVector(0.0f, 0.0f, 0.5f) - LightDirPP * 2.0f, Center, Radius);
			const FVector LineB = TransformPostPerspectiveDebugPoint(Frame, FVector(0.0f, 0.0f, 0.5f) + LightDirPP * 2.0f, Center, Radius);
			Lines.push_back({ LineA, LineB, FColor::Red() });
		}
	}

	struct FShadowScreenRect
			{
				float MinX = FLT_MAX;
				float MinY = FLT_MAX;
				float MaxX = -FLT_MAX;
				float MaxY = -FLT_MAX;
				bool bValid = false;

				void Expand(float X, float Y)
				{
					MinX = std::min(MinX, X);
					MinY = std::min(MinY, Y);
					MaxX = std::max(MaxX, X);
					MaxY = std::max(MaxY, Y);
					bValid = true;
				}

				float Width() const
				{
					return bValid ? std::max(MaxX - MinX, 0.0f) : 0.0f;
				}

				float Height() const
				{
					return bValid ? std::max(MaxY - MinY, 0.0f) : 0.0f;
				}
			};

			struct FShadowScreenMetrics
			{
				FShadowScreenRect Rect;

				float CoverageRatio = 0.0f;
				float CoverageScore = 0.0f;

				float ProjectedWidthPx = 0.0f;
				float ProjectedHeightPx = 0.0f;

				uint32 DesiredResolution = 0;
			};

			enum class EShadowAtlasRequestType
			{
				DirectionalCascade,
				Spot
			};

			struct FShadowAtlasPieceRequest
			{
				uint32 PieceIndex = 0;
				uint32 DesiredResolution = 0;
				uint32 MinResolution = 0;

				float Priority = 0.0f;
				float Cost = 0.0f;
				bool bMustAllocate = false;
				bool bSelected = false;
			};

			struct FShadowAtlasRequest
			{
				EShadowAtlasRequestType Type = EShadowAtlasRequestType::Spot;
				const ULightComponent* Light = nullptr;

				uint32 SpotIndex = static_cast<uint32>(-1);
				uint32 CascadeIndex = 0;

				TArray<FShadowAtlasPieceRequest> Pieces;
				FShadowHandleSet* ExistingHandleSet = nullptr;
				FShadowHandleSet* AllocatedHandleSet = nullptr;
				FTexturePoolHandleRequest DesiredHandleRequest;
				FTexturePoolHandleRequest AllocatedHandleRequest;

				float ScreenCoverageScore = 0.0f;
				float LightContributionScore = 0.0f;
				float ProximityScore = 0.0f;
				float CasterReceiverScore = 0.0f;
				float StabilityScore = 0.0f;
				float FragmentationPenalty = 0.0f;

				float FinalPriority = 0.0f;
				float EfficiencyScore = 0.0f;
				float ProjectedWidthPx = 0.0f;
				float ProjectedHeightPx = 0.0f;
				uint32 MaxAllowedResolution = 0;
				uint32 ExistingResolution = 0;

				bool bMustAllocate = false;
				bool bSelected = false;
				bool bAllocationFailed = false;
				const char* RejectionReason = "not selected";
			};

			struct FPointShadowRequest
			{
				uint32 PointIndex = static_cast<uint32>(-1);
				float ScreenCoverageScore = 0.0f;
				float LightContributionScore = 0.0f;
				float ProximityScore = 0.0f;
				float CasterReceiverScore = 0.0f;
				float StabilityScore = 0.0f;
				float FinalPriority = 0.0f;
			};

			constexpr uint32 MaxShadowedPointLights = 4;
			constexpr uint32 ShadowAtlasMinSpotResolution = 256;
			constexpr uint32 ShadowAtlasMaxDirectionalCascades = 4;
			constexpr uint64 ShadowAtlasReleaseGraceFrames = 8;
			constexpr uint64 ShadowAtlasDirectionalReleaseGraceFrames = 16;
			constexpr uint64 ShadowAtlasAllocationFailureCooldownFrames = 12;
			constexpr uint64 ShadowAtlasDownscaleStableFrames = 16;
			constexpr bool bShadowAtlasVerboseLog = false;

			float Clamp01(float Value)
			{
				return FMath::Clamp(Value, 0.0f, 1.0f);
			}

			bool ProjectWorldPointToScreen(
				const FVector & WorldPosition,
				const FMatrix & ViewProjection,
				float ViewportWidth,
				float ViewportHeight,
				float& OutX,
				float& OutY)
			{
				const float X =
					WorldPosition.X * ViewProjection.M[0][0] +
					WorldPosition.Y * ViewProjection.M[1][0] +
					WorldPosition.Z * ViewProjection.M[2][0] +
					ViewProjection.M[3][0];

				const float Y =
					WorldPosition.X * ViewProjection.M[0][1] +
					WorldPosition.Y * ViewProjection.M[1][1] +
					WorldPosition.Z * ViewProjection.M[2][1] +
					ViewProjection.M[3][1];

				const float W =
					WorldPosition.X * ViewProjection.M[0][3] +
					WorldPosition.Y * ViewProjection.M[1][3] +
					WorldPosition.Z * ViewProjection.M[2][3] +
					ViewProjection.M[3][3];

				if (W <= 0.0001f)
				{
					return false;
				}

				const float NdcX = X / W;
				const float NdcY = Y / W;
				OutX = (NdcX * 0.5f + 0.5f) * ViewportWidth;
				OutY = (-NdcY * 0.5f + 0.5f) * ViewportHeight;
				return true;
			}

			FShadowScreenRect ProjectSphereBoundsToScreen(
				const FFrameContext & Frame,
				const FVector & Center,
				float Radius)
			{
				FShadowScreenRect Rect;
				if (Radius <= 0.0f || Frame.ViewportWidth <= 0.0f || Frame.ViewportHeight <= 0.0f)
				{
					return Rect;
				}

				if (FVector::Distance(Frame.CameraPosition, Center) <= Radius)
				{
					Rect.MinX = 0.0f;
					Rect.MinY = 0.0f;
					Rect.MaxX = Frame.ViewportWidth;
					Rect.MaxY = Frame.ViewportHeight;
					Rect.bValid = true;
					return Rect;
				}

				const FMatrix ViewProjection = Frame.View * Frame.Proj;
				const FVector Corners[8] =
				{
					Center + FVector(-Radius, -Radius, -Radius),
					Center + FVector(-Radius, -Radius,  Radius),
					Center + FVector(-Radius,  Radius, -Radius),
					Center + FVector(-Radius,  Radius,  Radius),
					Center + FVector(Radius, -Radius, -Radius),
					Center + FVector(Radius, -Radius,  Radius),
					Center + FVector(Radius,  Radius, -Radius),
					Center + FVector(Radius,  Radius,  Radius)
				};

				for (const FVector& Corner : Corners)
				{
					float ScreenX = 0.0f;
					float ScreenY = 0.0f;
					if (ProjectWorldPointToScreen(Corner, ViewProjection, Frame.ViewportWidth, Frame.ViewportHeight, ScreenX, ScreenY))
					{
						Rect.Expand(ScreenX, ScreenY);
					}
				}

				if (!Rect.bValid)
				{
					return Rect;
				}

				// TODO: Clip sphere bounds against the near plane instead of relying only on valid projected corners.
				Rect.MinX = FMath::Clamp(Rect.MinX, 0.0f, Frame.ViewportWidth);
				Rect.MinY = FMath::Clamp(Rect.MinY, 0.0f, Frame.ViewportHeight);
				Rect.MaxX = FMath::Clamp(Rect.MaxX, 0.0f, Frame.ViewportWidth);
				Rect.MaxY = FMath::Clamp(Rect.MaxY, 0.0f, Frame.ViewportHeight);
				if (Rect.Width() <= 0.0f || Rect.Height() <= 0.0f)
				{
					Rect.bValid = false;
				}
				return Rect;
			}

			uint32 QuantizeShadowResolution(uint32 RequiredPixels)
			{
				if (RequiredPixels > 1024u)
				{
					return 2048;
				}
				if (RequiredPixels > 512u)
				{
					return 1024;
				}
				if (RequiredPixels > 256u)
				{
					return 512;
				}
				return 256;
			}

			uint32 ChooseSpotShadowResolutionFromProjectedRect(
				const FShadowScreenRect & Rect,
				uint32 MaxResolution,
				const FShadowAtlasPrioritySettings& AtlasPriority)
			{
				if (!Rect.bValid || MaxResolution == 0)
				{
					return 0;
				}

				const float ProjectedPixels = std::max(Rect.Width(), Rect.Height());
				const uint32 RequiredPixels = static_cast<uint32>(std::ceil(ProjectedPixels * AtlasPriority.ProjectedResolutionScale));
				const uint32 QuantizedResolution = QuantizeShadowResolution(RequiredPixels);
				return std::min(QuantizedResolution, MaxResolution);
			}

			float ConvertScreenCoverageToPriorityScore(float CoverageRatio)
			{
				constexpr float FullCoverageReference = 0.20f;
				return Clamp01(CoverageRatio / FullCoverageReference);
				return Clamp01(CoverageRatio);
			}

			FShadowScreenMetrics ComputeSpotShadowScreenMetrics(
				const FFrameContext & Frame,
				const FVector & Center,
				float Radius,
				uint32 MaxAllowedResolution,
				const FShadowAtlasPrioritySettings& AtlasPriority)
			{
				FShadowScreenMetrics Metrics = {};
				Metrics.Rect = ProjectSphereBoundsToScreen(Frame, Center, Radius);
				if (!Metrics.Rect.bValid)
				{
					return Metrics;
				}

				Metrics.ProjectedWidthPx = Metrics.Rect.Width();
				Metrics.ProjectedHeightPx = Metrics.Rect.Height();

				const float ScreenArea = std::max(Frame.ViewportWidth * Frame.ViewportHeight, 1.0f);
				const float RectArea = Metrics.ProjectedWidthPx * Metrics.ProjectedHeightPx;
				Metrics.CoverageRatio = Clamp01(RectArea / ScreenArea);
				Metrics.CoverageScore = ConvertScreenCoverageToPriorityScore(Metrics.CoverageRatio);
				Metrics.DesiredResolution = ChooseSpotShadowResolutionFromProjectedRect(Metrics.Rect, MaxAllowedResolution, AtlasPriority);
				return Metrics;
			}

			uint32 GetHandleSetPrimaryResolution(const FShadowHandleSet * HandleSet)
			{
				return HandleSet && !HandleSet->AllocatedSizes.empty()
					? HandleSet->AllocatedSizes[0]
					: 0;
			}

			FTexturePoolHandleRequest MakeHandleRequestFromAllocatedSizes(
				const FShadowHandleSet * HandleSet,
				const FTexturePoolHandleRequest & FallbackRequest)
			{
				if (!HandleSet || HandleSet->AllocatedSizes.empty())
				{
					return FallbackRequest;
				}

				FTexturePoolHandleRequest Request;
				Request.Sizes = HandleSet->AllocatedSizes;
				return Request;
			}

			float ComputeShadowPriority(
				float ScreenCoverageScore,
				float LightContributionScore,
				float ProximityScore,
				float CasterReceiverScore,
				float StabilityScore,
				float FragmentationPenalty,
				const FShadowAtlasPrioritySettings& AtlasPriority)
			{
				return ScreenCoverageScore * AtlasPriority.ScreenCoverageWeight
					+ LightContributionScore * AtlasPriority.LightContributionWeight
					+ ProximityScore * AtlasPriority.ProximityWeight
					+ CasterReceiverScore * AtlasPriority.CasterReceiverWeight
					+ StabilityScore * AtlasPriority.StabilityWeight
					- FragmentationPenalty;
			}

			float ComputeLuminance(const FVector4 & Color)
			{
				return Color.R * 0.2126f + Color.G * 0.7152f + Color.B * 0.0722f;
			}

			float ComputeLightContributionScore(float Intensity, const FVector4 & Color)
			{
				return Clamp01((std::max(Intensity, 0.0f) * ComputeLuminance(Color)) / 8.0f);
			}

			// Legacy distance-based approximation. Do not use for spot atlas request priority;
			// spot atlas metrics must come from ComputeSpotShadowScreenMetrics().
			float EstimateSphereScreenCoverage(const FFrameContext & Frame, const FVector & Center, float Radius)
			{
				if (Radius <= 0.0f || Frame.ViewportWidth <= 0.0f || Frame.ViewportHeight <= 0.0f)
				{
					return 0.0f;
				}

				if (Frame.bIsOrtho)
				{
					const float OrthoWidth = std::max(Frame.OrthoWidth, 1.0f);
					const float NormalizedRadius = Radius / OrthoWidth;
					return Clamp01(NormalizedRadius * NormalizedRadius * 4.0f);
				}

				const float DistanceToCenter = std::max(FVector::Distance(Frame.CameraPosition, Center), 1.0f);
				const float MinViewportExtent = std::max(std::min(Frame.ViewportWidth, Frame.ViewportHeight), 1.0f);
				const float ProjectedRadiusPixels = (Radius * Frame.Proj.M[1][1] / DistanceToCenter) * (Frame.ViewportHeight * 0.5f);
				const float NormalizedRadius = ProjectedRadiusPixels / MinViewportExtent;
				return Clamp01(NormalizedRadius * NormalizedRadius * 4.0f);
			}

			float EstimateInfluenceProximityScore(const FFrameContext & Frame, const FVector & Center, float Radius)
			{
				const float DistanceToInfluence = std::max(FVector::Distance(Frame.CameraPosition, Center) - Radius, 0.0f);
				const float ReferenceDistance = std::max(Frame.FarClip * 0.25f, 1.0f);
				return Clamp01(1.0f - (DistanceToInfluence / ReferenceDistance));
			}

			uint32 HalveResolution(uint32 Resolution, uint32 MinResolution)
			{
				return std::max(Resolution / 2u, MinResolution);
			}

			FTexturePoolHandleRequest MakeDirectionalHandleRequest(uint32 BaseResolution, bool bDownscaleFarCascades)
			{
				FTexturePoolHandleRequest Request;
				for (uint32 CascadeIndex = 0; CascadeIndex < ShadowAtlasMaxDirectionalCascades; ++CascadeIndex)
				{
					uint32 Resolution = std::max(BaseResolution >> CascadeIndex, 1u);
					if (bDownscaleFarCascades && CascadeIndex > 0)
					{
						Resolution = HalveResolution(Resolution, ShadowAtlasMinSpotResolution);
					}
					Request.Sizes.push_back(Resolution);
				}
				return Request;
			}

			void UpdateRequestCost(FShadowAtlasRequest & Request, FTextureAtlasPool & AtlasPool)
			{
				Request.FinalPriority = 0.0f;
				for (FShadowAtlasPieceRequest& Piece : Request.Pieces)
				{
					FTexturePoolHandleRequest PieceCostRequest;
					PieceCostRequest.Sizes.push_back(Piece.DesiredResolution);
					Piece.Cost = AtlasPool.EstimateAllocationCost(PieceCostRequest);
					Request.FinalPriority = std::max(Request.FinalPriority, Piece.Priority);
					Request.bMustAllocate = Request.bMustAllocate || Piece.bMustAllocate;
				}

				const float TotalAtlasCost = std::max(AtlasPool.EstimateAllocationCost(Request.DesiredHandleRequest), 1.0f);
				Request.EfficiencyScore = Request.FinalPriority / TotalAtlasCost;
			}

			void SelectDirectionalPieces(FShadowAtlasRequest & Request, EShadowMethod ShadowMethod)
			{
				if (Request.Pieces.empty())
				{
					return;
				}

				Request.Pieces[0].bSelected = true;
				if (ShadowMethod != EShadowMethod::CSM)
				{
					return;
				}

				const float Thresholds[ShadowAtlasMaxDirectionalCascades] = { 0.0f, 0.58f, 0.42f, 0.28f };
				for (uint32 CascadeIndex = 1; CascadeIndex < std::min<uint32>(static_cast<uint32>(Request.Pieces.size()), ShadowAtlasMaxDirectionalCascades); ++CascadeIndex)
				{
					Request.Pieces[CascadeIndex].bSelected = Request.Pieces[CascadeIndex].Priority >= Thresholds[CascadeIndex];
				}
			}

			uint32 GetContiguousSelectedCascadeCount(const FShadowAtlasRequest & Request)
			{
				uint32 Count = 0;
				for (const FShadowAtlasPieceRequest& Piece : Request.Pieces)
				{
					if (!Piece.bSelected)
					{
						break;
					}
					++Count;
				}
				return Count;
			}

			void BuildShadowAtlasRequests(
				const FFrameContext & Frame,
				const FSceneEnvironment & Env,
				FTextureAtlasPool & AtlasPool,
				const TArray<uint32>&ShadowedSpotIndices,
				bool bShadowDirectional,
				uint64 ShadowAtlasFrameIndex,
				TArray<FShadowAtlasRequest>&OutRequests)
			{
				const FShadowAtlasPrioritySettings& AtlasPriority = Frame.RenderOptions.ShadowAtlasPriority;
				if (bShadowDirectional)
				{
					const UDirectionalLightComponent* DirectionalLight = Env.GetGlobalDirectionalLightOwner();
					if (DirectionalLight)
					{
						const FGlobalDirectionalLightParams& Params = Env.GetGlobalDirectionalLightParams();
						const uint32 BaseResolution = DirectionalLight->GetShadowResolution();
						const bool bUseCSM = Frame.RenderOptions.ShadowMethod == EShadowMethod::CSM;
						const uint32 PieceCount = bUseCSM ? ShadowAtlasMaxDirectionalCascades : 1u;

						FShadowAtlasRequest Request = {};
						Request.Type = EShadowAtlasRequestType::DirectionalCascade;
						Request.Light = DirectionalLight;
						Request.ExistingHandleSet = DirectionalLight->PeekShadowHandleSet();
						const_cast<UDirectionalLightComponent*>(DirectionalLight)->MarkShadowAtlasRequested(ShadowAtlasFrameIndex);
						Request.DesiredHandleRequest = MakeDirectionalHandleRequest(BaseResolution, false);
						Request.LightContributionScore = ComputeLightContributionScore(Params.Intensity, Params.LightColor);
						Request.ProximityScore = 1.0f;
						// TODO: Replace this constant with caster/receiver overlap tests.
						Request.CasterReceiverScore = 1.0f;
						Request.StabilityScore = (Request.ExistingHandleSet && Request.ExistingHandleSet->bIsValid) ? 1.0f : 0.0f;
						Request.FragmentationPenalty = 0.0f;

						const float CascadeCoverage[ShadowAtlasMaxDirectionalCascades] = { 1.0f, 0.72f, 0.42f, 0.22f };
						for (uint32 CascadeIndex = 0; CascadeIndex < PieceCount; ++CascadeIndex)
						{
							FShadowAtlasPieceRequest Piece = {};
							Piece.PieceIndex = CascadeIndex;
							Piece.DesiredResolution = std::max(BaseResolution >> CascadeIndex, 1u);
							Piece.MinResolution = CascadeIndex == 0 ? Piece.DesiredResolution : HalveResolution(Piece.DesiredResolution, ShadowAtlasMinSpotResolution);
							Piece.bMustAllocate = CascadeIndex == 0;
							Piece.Priority = ComputeShadowPriority(
								CascadeCoverage[CascadeIndex],
								Request.LightContributionScore,
								Request.ProximityScore,
								Request.CasterReceiverScore,
								Request.StabilityScore,
								Request.FragmentationPenalty,
								AtlasPriority);
							Request.Pieces.push_back(Piece);
						}

						Request.ScreenCoverageScore = PieceCount > 0 ? CascadeCoverage[0] : 0.0f;
						UpdateRequestCost(Request, AtlasPool);
						OutRequests.push_back(Request);
					}
				}

				for (uint32 SpotIndex : ShadowedSpotIndices)
				{
					const USpotLightComponent* SpotLight = Env.GetSpotLightOwner(SpotIndex);
					if (!SpotLight)
					{
						continue;
					}

					const FSpotLightParams& Params = Env.GetSpotLight(SpotIndex);
					FShadowAtlasRequest Request = {};
					Request.Type = EShadowAtlasRequestType::Spot;
					Request.Light = SpotLight;
					Request.SpotIndex = SpotIndex;
					Request.ExistingHandleSet = SpotLight->PeekShadowHandleSet();
					const_cast<USpotLightComponent*>(SpotLight)->MarkShadowAtlasRequested(ShadowAtlasFrameIndex);
					Request.ExistingResolution = GetHandleSetPrimaryResolution(Request.ExistingHandleSet);
					Request.MaxAllowedResolution = SpotLight->GetShadowResolution();
					const FShadowScreenMetrics Metrics = ComputeSpotShadowScreenMetrics(
						Frame,
						Params.Position,
						Params.AttenuationRadius,
						Request.MaxAllowedResolution,
						AtlasPriority);
					//if (Metrics.DesiredResolution == 0)
					//{
					//	if (bShadowAtlasVerboseLog)
					//	{
					//		UE_LOG("[ShadowAtlas] skipped spot light=%u: projected attenuation sphere is not visible", SpotIndex);
					//	}
					//	continue;
					//}

					Request.ScreenCoverageScore = Metrics.CoverageScore;
					Request.LightContributionScore = ComputeLightContributionScore(Params.Intensity, Params.LightColor);
					Request.ProximityScore = EstimateInfluenceProximityScore(Frame, Params.Position, Params.AttenuationRadius);
					// TODO: Replace this constant with caster/receiver overlap tests.
					Request.CasterReceiverScore = 1.0f;
					Request.StabilityScore = (Request.ExistingHandleSet && Request.ExistingHandleSet->bIsValid) ? 1.0f : 0.0f;
					Request.FragmentationPenalty = 0.0f;
					Request.FinalPriority = ComputeShadowPriority(
						Request.ScreenCoverageScore,
						Request.LightContributionScore,
						Request.ProximityScore,
						Request.CasterReceiverScore,
						Request.StabilityScore,
						Request.FragmentationPenalty,
						AtlasPriority);
					Request.bMustAllocate = Request.ScreenCoverageScore >= AtlasPriority.SpotMustCoverageThreshold
						&& Request.ProximityScore >= AtlasPriority.SpotMustProximityThreshold;

					Request.ProjectedWidthPx = Metrics.ProjectedWidthPx;
					Request.ProjectedHeightPx = Metrics.ProjectedHeightPx;
					const uint32 DesiredResolution = Metrics.DesiredResolution;
					Request.DesiredHandleRequest.Sizes.push_back(DesiredResolution);

					FShadowAtlasPieceRequest Piece = {};
					Piece.PieceIndex = 0;
					Piece.DesiredResolution = DesiredResolution;
					Piece.MinResolution = ShadowAtlasMinSpotResolution;
					Piece.Priority = Request.FinalPriority;
					Piece.bMustAllocate = Request.bMustAllocate;
					Request.Pieces.push_back(Piece);

					UpdateRequestCost(Request, AtlasPool);
					OutRequests.push_back(Request);
				}
			}

			void ReleaseInvalidExistingHandleSets(TArray<FShadowAtlasRequest>&Requests, FTextureAtlasPool & AtlasPool)
			{
				// This only cleans invalid handles for lights that produced a current-frame atlas request.
				// Off-screen stale handle lifetime is handled by ReleaseStaleAtlasShadowHandles().
				for (FShadowAtlasRequest& Request : Requests)
				{
					if (Request.ExistingHandleSet
						&& (!Request.ExistingHandleSet->bIsValid || Request.ExistingHandleSet->GetPool() != &AtlasPool))
					{
						const_cast<ULightComponent*>(Request.Light)->ReleaseShadowHandleSetForRenderer();
						Request.ExistingHandleSet = nullptr;
					}
				}
			}

			void LogShadowAtlasAllocation(const FShadowAtlasRequest & Request, FTextureAtlasPool & AtlasPool)
			{

			}

			bool TryAllocateRequest(FShadowAtlasRequest & Request, FTextureAtlasPool & AtlasPool, uint64 ShadowAtlasFrameIndex)
			{
				if (Request.ExistingHandleSet && Request.ExistingHandleSet->bIsValid)
				{
					if (Request.Type == EShadowAtlasRequestType::DirectionalCascade)
					{
						Request.AllocatedHandleSet = Request.ExistingHandleSet;
						Request.AllocatedHandleRequest = MakeHandleRequestFromAllocatedSizes(Request.ExistingHandleSet, Request.DesiredHandleRequest);
						Request.bSelected = true;
						Request.RejectionReason = "kept existing";
						const_cast<ULightComponent*>(Request.Light)->MarkShadowAtlasSelected(ShadowAtlasFrameIndex);
						return true;
					}

					const uint32 CurrentResolution = GetHandleSetPrimaryResolution(Request.ExistingHandleSet);
					const uint32 DesiredResolution = Request.Pieces.empty()
						? (Request.DesiredHandleRequest.Sizes.empty() ? ShadowAtlasMinSpotResolution : Request.DesiredHandleRequest.Sizes[0])
						: Request.Pieces[0].DesiredResolution;
					Request.ExistingResolution = CurrentResolution;
					ULightComponent* MutableLight = const_cast<ULightComponent*>(Request.Light);

					if (CurrentResolution == DesiredResolution && CurrentResolution > 0)
					{
						MutableLight->ClearShadowAtlasDownscaleCandidate();

						Request.AllocatedHandleSet = Request.ExistingHandleSet;
						Request.AllocatedHandleRequest = MakeHandleRequestFromAllocatedSizes(Request.ExistingHandleSet, Request.DesiredHandleRequest);
						Request.bSelected = true;
						Request.RejectionReason = "kept existing same resolution";
						MutableLight->MarkShadowAtlasSelected(ShadowAtlasFrameIndex);
						return true;
					}

					if (CurrentResolution < DesiredResolution)
					{
						MutableLight->ClearShadowAtlasDownscaleCandidate();

						FTexturePoolHandleRequest UpgradeAttempt;
						UpgradeAttempt.Sizes.push_back(DesiredResolution);
						FShadowHandleSet* UpgradedHandleSet = AtlasPool.TryGetTextureHandleNoResize(UpgradeAttempt);
						if (UpgradedHandleSet)
						{
							Request.AllocatedHandleSet = UpgradedHandleSet;
							Request.AllocatedHandleRequest = UpgradeAttempt;
							if (!Request.Pieces.empty())
							{
								Request.Pieces[0].DesiredResolution = DesiredResolution;
							}
							Request.bSelected = true;
							Request.RejectionReason = "upgraded resolution step";
							MutableLight->SetShadowHandleSetForRenderer(UpgradedHandleSet);
							MutableLight->MarkShadowAtlasSelected(ShadowAtlasFrameIndex);
							LogShadowAtlasAllocation(Request, AtlasPool);
							return true;
						}

						Request.AllocatedHandleSet = Request.ExistingHandleSet;
						Request.AllocatedHandleRequest = MakeHandleRequestFromAllocatedSizes(Request.ExistingHandleSet, Request.DesiredHandleRequest);
						Request.bSelected = true;
						Request.bAllocationFailed = true;
						Request.RejectionReason = "kept lower-res existing after upgrade failed";
						MutableLight->MarkShadowAtlasSelected(ShadowAtlasFrameIndex);
						MutableLight->MarkShadowAtlasAllocationFailed(ShadowAtlasFrameIndex, DesiredResolution);
						return true;
					}

					if (CurrentResolution > DesiredResolution && DesiredResolution >= ShadowAtlasMinSpotResolution)
					{
						const bool bCanDownscale = MutableLight->UpdateShadowAtlasDownscaleCandidate(
							DesiredResolution,
							ShadowAtlasFrameIndex,
							ShadowAtlasDownscaleStableFrames);

						if (!bCanDownscale)
						{
							Request.AllocatedHandleSet = Request.ExistingHandleSet;
							Request.AllocatedHandleRequest = MakeHandleRequestFromAllocatedSizes(Request.ExistingHandleSet, Request.DesiredHandleRequest);
							Request.bSelected = true;
							Request.RejectionReason = "kept larger existing waiting downscale hysteresis";
							MutableLight->MarkShadowAtlasSelected(ShadowAtlasFrameIndex);
							return true;
						}

						MutableLight->ReleaseShadowHandleSetForRenderer();
						Request.ExistingHandleSet = nullptr;

						FTexturePoolHandleRequest DownscaleAttempt;
						DownscaleAttempt.Sizes.push_back(DesiredResolution);
						FShadowHandleSet* DownscaledHandleSet = AtlasPool.TryGetTextureHandleNoResize(DownscaleAttempt);
						if (DownscaledHandleSet)
						{
							Request.AllocatedHandleSet = DownscaledHandleSet;
							Request.AllocatedHandleRequest = DownscaleAttempt;
							if (!Request.Pieces.empty())
							{
								Request.Pieces[0].DesiredResolution = DesiredResolution;
							}
							Request.bSelected = true;
							Request.RejectionReason = "downscaled resolution step";
							MutableLight->SetShadowHandleSetForRenderer(DownscaledHandleSet);
							MutableLight->MarkShadowAtlasSelected(ShadowAtlasFrameIndex);
							MutableLight->ClearShadowAtlasDownscaleCandidate();
							LogShadowAtlasAllocation(Request, AtlasPool);
							return true;
						}

						Request.RejectionReason = "downscale failed after release";
						Request.bAllocationFailed = true;
						MutableLight->MarkShadowAtlasAllocationFailed(ShadowAtlasFrameIndex, DesiredResolution);
						MutableLight->ClearShadowAtlasDownscaleCandidate();
						//if (bShadowAtlasVerboseLog)
						//{
						//	UE_LOG("[ShadowAtlas] spot downscale failed after releasing existing handle desired=%u current=%u", DesiredResolution, CurrentResolution);
						//}
						return false;
					}

					Request.AllocatedHandleSet = Request.ExistingHandleSet;
					Request.AllocatedHandleRequest = MakeHandleRequestFromAllocatedSizes(Request.ExistingHandleSet, Request.DesiredHandleRequest);
					Request.bSelected = true;
					Request.RejectionReason = "kept existing invalid desired resolution";
					MutableLight->MarkShadowAtlasSelected(ShadowAtlasFrameIndex);
					return true;
				}

				if (Request.Type == EShadowAtlasRequestType::DirectionalCascade)
				{
					FTexturePoolHandleRequest Attempts[2] =
					{
						MakeDirectionalHandleRequest(Request.DesiredHandleRequest.Sizes.empty() ? 1024u : Request.DesiredHandleRequest.Sizes[0], false),
						MakeDirectionalHandleRequest(Request.DesiredHandleRequest.Sizes.empty() ? 1024u : Request.DesiredHandleRequest.Sizes[0], true)
					};

					for (const FTexturePoolHandleRequest& Attempt : Attempts)
					{
						FShadowHandleSet* HandleSet = AtlasPool.TryGetTextureHandleNoResize(Attempt);
						if (!HandleSet)
						{
							continue;
						}

						Request.AllocatedHandleSet = HandleSet;
						Request.AllocatedHandleRequest = Attempt;
						Request.bSelected = true;
						Request.RejectionReason = "allocated";
						const_cast<ULightComponent*>(Request.Light)->SetShadowHandleSetForRenderer(HandleSet);
						const_cast<ULightComponent*>(Request.Light)->MarkShadowAtlasSelected(ShadowAtlasFrameIndex);
						LogShadowAtlasAllocation(Request, AtlasPool);
						return true;
					}

					Request.RejectionReason = "no space";
					Request.bAllocationFailed = true;
					const uint32 FailedResolution = Request.DesiredHandleRequest.Sizes.empty() ? 0u : Request.DesiredHandleRequest.Sizes[0];
					const_cast<ULightComponent*>(Request.Light)->MarkShadowAtlasAllocationFailed(ShadowAtlasFrameIndex, FailedResolution);
					return false;
				}

				uint32 Resolution = Request.Pieces.empty() ? ShadowAtlasMinSpotResolution : Request.Pieces[0].DesiredResolution;
				if (!Request.bMustAllocate
					&& const_cast<ULightComponent*>(Request.Light)->ShouldSkipShadowAtlasAllocation(
						ShadowAtlasFrameIndex,
						Resolution,
						ShadowAtlasAllocationFailureCooldownFrames))
				{
					Request.RejectionReason = "cooldown";
					return false;
				}

				while (Resolution >= ShadowAtlasMinSpotResolution)
				{
					FTexturePoolHandleRequest Attempt;
					Attempt.Sizes.push_back(Resolution);

					FShadowHandleSet* HandleSet = AtlasPool.TryGetTextureHandleNoResize(Attempt);
					if (HandleSet)
					{
						Request.AllocatedHandleSet = HandleSet;
						Request.AllocatedHandleRequest = Attempt;
						Request.Pieces[0].DesiredResolution = Resolution;
						Request.bSelected = true;
						Request.RejectionReason = "allocated";
						const_cast<ULightComponent*>(Request.Light)->SetShadowHandleSetForRenderer(HandleSet);
						const_cast<ULightComponent*>(Request.Light)->MarkShadowAtlasSelected(ShadowAtlasFrameIndex);
						LogShadowAtlasAllocation(Request, AtlasPool);
						return true;
					}

					if (Resolution == ShadowAtlasMinSpotResolution)
					{
						break;
					}
					Resolution = HalveResolution(Resolution, ShadowAtlasMinSpotResolution);
				}

				Request.RejectionReason = "no space";
				Request.bAllocationFailed = true;
				const uint32 FailedResolution = Request.Pieces.empty() ? ShadowAtlasMinSpotResolution : Request.Pieces[0].DesiredResolution;
				const_cast<ULightComponent*>(Request.Light)->MarkShadowAtlasAllocationFailed(ShadowAtlasFrameIndex, FailedResolution);
				return false;
			}

			void SelectShadowAtlasRequests(
				TArray<FShadowAtlasRequest>&Requests,
				FTextureAtlasPool & AtlasPool,
				EShadowMethod ShadowMethod,
				uint64 ShadowAtlasFrameIndex,
				const FShadowAtlasPrioritySettings& AtlasPriority)
			{
				for (FShadowAtlasRequest& Request : Requests)
				{
					if (Request.Type == EShadowAtlasRequestType::DirectionalCascade)
					{
						SelectDirectionalPieces(Request, ShadowMethod);
					}
				}

				TArray<uint32> Order;
				Order.reserve(Requests.size());
				for (uint32 Index = 0; Index < static_cast<uint32>(Requests.size()); ++Index)
				{
					Order.push_back(Index);
				}

				std::sort(Order.begin(), Order.end(), [&Requests, &AtlasPool, &AtlasPriority](uint32 A, uint32 B)
					{
						const FShadowAtlasRequest& Left = Requests[A];
						const FShadowAtlasRequest& Right = Requests[B];
						const bool bLeftExisting = Left.ExistingHandleSet && Left.ExistingHandleSet->bIsValid;
						const bool bRightExisting = Right.ExistingHandleSet && Right.ExistingHandleSet->bIsValid;
						if (bLeftExisting != bRightExisting)
						{
							const FShadowAtlasRequest& Existing = bLeftExisting ? Left : Right;
							const FShadowAtlasRequest& NewRequest = bLeftExisting ? Right : Left;
							const bool bNewBeatsHysteresis = NewRequest.FinalPriority > Existing.FinalPriority * AtlasPriority.HysteresisFactor;
							if (!bNewBeatsHysteresis)
							{
								return bLeftExisting;
							}
						}
						if (Left.bMustAllocate != Right.bMustAllocate)
						{
							return Left.bMustAllocate;
						}
						if (Left.bMustAllocate && Right.bMustAllocate)
						{
							if (Left.FinalPriority != Right.FinalPriority)
							{
								return Left.FinalPriority > Right.FinalPriority;
							}
							return AtlasPool.EstimateAllocationCost(Left.DesiredHandleRequest) > AtlasPool.EstimateAllocationCost(Right.DesiredHandleRequest);
						}
						return Left.EfficiencyScore > Right.EfficiencyScore;
					});

				// TODO: Add skyline/best-fit fragmentation estimator.
				// Current grid allocator uses best-area-fit FreeRects; large high-priority requests are still sorted earlier to reduce fragmentation.
				for (uint32 RequestIndex : Order)
				{
					FShadowAtlasRequest& Request = Requests[RequestIndex];
					if (!Request.bMustAllocate && Request.EfficiencyScore <= 0.0f)
					{
						Request.RejectionReason = "low priority";
						continue;
					}

					TryAllocateRequest(Request, AtlasPool, ShadowAtlasFrameIndex);
				}
			}

			uint32 ReleaseStaleAtlasShadowHandles(const FSceneEnvironment & Env, uint64 ShadowAtlasFrameIndex)
			{
				uint32 ReleasedCount = 0;
				for (uint32 SpotIndex = 0; SpotIndex < Env.GetNumSpotLights(); ++SpotIndex)
				{
					USpotLightComponent* SpotLight = const_cast<USpotLightComponent*>(Env.GetSpotLightOwner(SpotIndex));
					if (SpotLight && SpotLight->ShouldReleaseShadowAtlasHandle(ShadowAtlasFrameIndex, ShadowAtlasReleaseGraceFrames))
					{
						SpotLight->ReleaseShadowHandleSetForRenderer();
						++ReleasedCount;
					}
				}

				UDirectionalLightComponent* DirectionalLight = const_cast<UDirectionalLightComponent*>(Env.GetGlobalDirectionalLightOwner());
				if (DirectionalLight && DirectionalLight->ShouldReleaseShadowAtlasHandle(ShadowAtlasFrameIndex, ShadowAtlasDirectionalReleaseGraceFrames))
				{
					DirectionalLight->ReleaseShadowHandleSetForRenderer();
					++ReleasedCount;
				}

				// TODO: Add stale lifetime management for FTextureCubeShadowPool point-light handles separately.
				return ReleasedCount;
			}

			void LogShadowAtlasSelection(
				const TArray<FShadowAtlasRequest>&Requests,
				FTextureAtlasPool & AtlasPool,
				uint32 StaleReleasedCount)
			{
				static uint32 LogFrameCounter = 0;
				if ((LogFrameCounter++ % 120u) != 0u)
				{
					return;
				}

				uint32 SelectedCount = 0;
				uint32 AllocationFailureCount = 0;
				for (const FShadowAtlasRequest& Request : Requests)
				{
					SelectedCount += Request.bSelected ? 1u : 0u;
					AllocationFailureCount += Request.bAllocationFailed ? 1u : 0u;
				}

				//UE_LOG("[ShadowAtlas] Pool=%p candidates=%u selected=%u rejected=%u staleReleased=%u allocFailed=%u freeRects=%u totalFree=%llu largestFree=%llu fragmentation=%.2f",
				//	static_cast<void*>(&AtlasPool),
				//	static_cast<uint32>(Requests.size()),
				//	SelectedCount,
				//	static_cast<uint32>(Requests.size()) - SelectedCount,
				//	StaleReleasedCount,
				//	AllocationFailureCount,
				//	AtlasPool.GetAllocatorFreeRectCount(),
				//	AtlasPool.GetAllocatorTotalFreeArea(),
				//	AtlasPool.GetAllocatorLargestFreeRectArea(),
				//	AtlasPool.GetAllocatorFragmentationRatio());

				if (!bShadowAtlasVerboseLog)
				{
					return;
				}

				for (const FShadowAtlasRequest& Request : Requests)
				{
					const char* TypeName = Request.Type == EShadowAtlasRequestType::DirectionalCascade ? "Directional" : "Spot";
					const uint32 LightIndex = Request.Type == EShadowAtlasRequestType::Spot ? Request.SpotIndex : 0xffffffffu;
					const uint32 AllocatedResolution = Request.AllocatedHandleRequest.Sizes.empty() ? 0u : Request.AllocatedHandleRequest.Sizes[0];
					const uint32 DesiredResolution = Request.DesiredHandleRequest.Sizes.empty() ? 0u : Request.DesiredHandleRequest.Sizes[0];

					//UE_LOG("[ShadowAtlas] type=%s light=%u cascade=%u existing=%u desired=%u allocated=%u max=%u projected=(%.1f x %.1f) priority=%.3f efficiency=%.6f must=%u selected=%u reason=%s",
					//	TypeName,
					//	LightIndex,
					//	Request.CascadeIndex,
					//	Request.ExistingResolution,
					//	DesiredResolution,
					//	AllocatedResolution,
					//	Request.MaxAllowedResolution,
					//	Request.ProjectedWidthPx,
					//	Request.ProjectedHeightPx,
					//	Request.FinalPriority,
					//	Request.EfficiencyScore,
					//	Request.bMustAllocate ? 1u : 0u,
					//	Request.bSelected ? 1u : 0u,
					//	Request.RejectionReason);
				}
			}
		}

		void FRenderer::Create(HWND hWindow)
		{
			Device.Create(hWindow);

			if (Device.GetDevice() == nullptr)
			{
				UE_LOG("Failed to create D3D Device.");
			}

			FShaderManager::Get().Initialize(Device.GetDevice());
			Resources.Create(Device.GetDevice());
			FTextureCubeShadowPool::Get().Initialize(Device.GetDevice(), Device.GetDeviceContext(), 1024);

			TileBasedCulling.Initialize(Device.GetDevice());
			ClusteredLightCuller.Initialize(Device.GetDevice(), Device.GetDeviceContext());

			PassRenderStateTable.Initialize();

			Builder.Create(Device.GetDevice(), Device.GetDeviceContext(), &PassRenderStateTable);
			ShadowPassBuffer.Create(Device.GetDevice(), sizeof(FShadowPassConstants));
			VSMBlurPassBuffer.Create(Device.GetDevice(), sizeof(FVSMBlurPassConstants));

			FGPUProfiler::Get().Initialize(Device.GetDevice(), Device.GetDeviceContext());
		}

		void FRenderer::Release()
		{
			FGPUProfiler::Get().Shutdown();

			ShadowPassBuffer.Release();
			VSMBlurPassBuffer.Release();
			Builder.Release();

			Resources.Release();
			FTextureCubeShadowPool::Get().Release();
			TileBasedCulling.Release();
			ClusteredLightCuller.Release();
			FShaderManager::Get().Release();
			FMaterialManager::Get().Release();
			Device.Release();
		}

		void FRenderer::BeginFrame()
		{
			Device.BeginFrame();
		}

		//ShadowMap을 그리기 위한 ShadowRenderTask생성하는 부분
		void FRenderer::BuildShadowPassData(const FFrameContext & Frame, FScene & Scene, FShadowPassData & OutShadowPassData)
		{
			const FSceneEnvironment& Env = Scene.GetEnvironment();
			FTextureAtlasPool& AtlasPool = Scene.GetShadowAtlasPool();
			const uint64 CurrentShadowAtlasFrame = ShadowAtlasFrameIndex++;
			static const FTextureAtlasPool* LastShadowPassLoggedPool = nullptr;
			//if (LastShadowPassLoggedPool != &AtlasPool)
			//{
			//	LastShadowPassLoggedPool = &AtlasPool;
			//	UE_LOG("[ShadowAtlas] ShadowPass Scene=%s Pool=%p",
			//		GetShadowLogWorldTypeName(Scene.GetDebugWorldType()),
			//		static_cast<void*>(&AtlasPool));
			//}
			OutShadowPassData.BindingData.PointLightShadowIndices.assign(Env.GetNumPointLights(), -1);
			OutShadowPassData.BindingData.SpotLightShadowIndices.assign(Env.GetNumSpotLights(), -1);
			OutShadowPassData.BindingData.ActivePointLightMask.assign(Env.GetNumPointLights(), 0);
			OutShadowPassData.BindingData.ActiveSpotLightMask.assign(Env.GetNumSpotLights(), 0);
			OutShadowPassData.BindingData.DirectionalShadowIndex = -1;

			if (Frame.RenderOptions.ViewMode == EViewMode::Unlit)
			{
				for (uint32 PointIndex = 0; PointIndex < Env.GetNumPointLights(); ++PointIndex)
				{
					UPointLightComponent* PointLight = const_cast<UPointLightComponent*>(Env.GetPointLightOwner(PointIndex));
					if (PointLight)
					{
						PointLight->ReleaseCubeShadowHandleForRenderer();
					}
				}
				ReleaseStaleAtlasShadowHandles(Env, CurrentShadowAtlasFrame);
				return;
			}

			const bool bUseVSM = Frame.RenderOptions.ShadowFilterMode == EShadowFilterMode::VSM;
			AtlasPool.EnsureAtlasMode(Frame.RenderOptions.ShadowFilterMode);
			FTextureCubeShadowPool::Get().EnsureVSMMode(bUseVSM);

			const uint32 AtlasTextureSize = AtlasPool.GetTextureSize();
			const uint32 NumPointLights = Env.GetNumPointLights();
			const bool bShadowDirectional = [&Env]()
				{
					const UDirectionalLightComponent* DirectionalLight = Env.GetGlobalDirectionalLightOwner();
					return DirectionalLight && DirectionalLight->IsCastShadow();
				}();

			//현재는 전체 중 쉐도우 옵션 켜지고 화면에 영향을 주는 놈들만, 
			//추후에 광 범위에 오브젝트가 들어와서 Depth에 변화가 생기는 놈들 까지 검사 추가
			//중요도에 따라서 컷하는 것도 추가
#pragma region SearchUpdateNeededPointLight
			TArray<uint32> ShadowedPointIndices;
			ShadowedPointIndices.reserve(Env.GetNumPointLights());
			TArray<FPointShadowRequest> PointShadowRequests;
			PointShadowRequests.reserve(Env.GetNumPointLights());

			for (uint32 PointIndex = 0; PointIndex < Env.GetNumPointLights(); ++PointIndex)
			{
				const UPointLightComponent* PointLight = Env.GetPointLightOwner(PointIndex);
				if (!PointLight || !PointLight->IsCastShadow())
				{
					continue;
				}

				const FPointLightParams& Params = Env.GetPointLight(PointIndex);
				if (!Frame.bIsOrtho
					&& !Frame.FrustumVolume.IntersectAABB(FShadowUtil::MakeSphereBounds(Params.Position, Params.AttenuationRadius)))
				{
					continue;
				}

				FPointShadowRequest Request = {};
				Request.PointIndex = PointIndex;
				Request.ScreenCoverageScore = EstimateSphereScreenCoverage(Frame, Params.Position, Params.AttenuationRadius);
				Request.LightContributionScore = ComputeLightContributionScore(Params.Intensity, Params.LightColor);
				Request.ProximityScore = EstimateInfluenceProximityScore(Frame, Params.Position, Params.AttenuationRadius);
				Request.CasterReceiverScore = 1.0f;
				Request.StabilityScore = PointLight->PeekCubeShadowHandle().IsValid() ? 1.0f : 0.0f;
				Request.FinalPriority = ComputeShadowPriority(
					Request.ScreenCoverageScore,
					Request.LightContributionScore,
					Request.ProximityScore,
					Request.CasterReceiverScore,
					Request.StabilityScore,
					0.0f,
					FShadowAtlasPrioritySettings{});
				PointShadowRequests.push_back(Request);
			}

			std::sort(PointShadowRequests.begin(), PointShadowRequests.end(),
				[](const FPointShadowRequest& Left, const FPointShadowRequest& Right)
				{
					if (Left.FinalPriority != Right.FinalPriority)
					{
						return Left.FinalPriority > Right.FinalPriority;
					}
					return Left.PointIndex < Right.PointIndex;
				});

			TArray<uint8> bSelectedPointShadow;
			bSelectedPointShadow.assign(Env.GetNumPointLights(), 0);
			const uint32 SelectedPointCount = std::min(MaxShadowedPointLights, static_cast<uint32>(PointShadowRequests.size()));
			for (uint32 RequestIndex = 0; RequestIndex < SelectedPointCount; ++RequestIndex)
			{
				const uint32 PointIndex = PointShadowRequests[RequestIndex].PointIndex;
				if (PointIndex < bSelectedPointShadow.size())
				{
					bSelectedPointShadow[PointIndex] = 1;
					ShadowedPointIndices.push_back(PointIndex);
				}
			}

			for (uint32 PointIndex = 0; PointIndex < Env.GetNumPointLights(); ++PointIndex)
			{
				UPointLightComponent* PointLight = const_cast<UPointLightComponent*>(Env.GetPointLightOwner(PointIndex));
				if (PointLight && (PointIndex >= bSelectedPointShadow.size() || bSelectedPointShadow[PointIndex] == 0))
				{
					PointLight->ReleaseCubeShadowHandleForRenderer();
				}
			}
#pragma endregion

#pragma region SearchUpdateNeededSpotLight
			TArray<uint32> ShadowedSpotIndices;
			ShadowedSpotIndices.reserve(Env.GetNumSpotLights());

			for (uint32 SpotIndex = 0; SpotIndex < Env.GetNumSpotLights(); ++SpotIndex)
			{
				const USpotLightComponent* SpotLight = Env.GetSpotLightOwner(SpotIndex);
				if (!SpotLight || !SpotLight->IsCastShadow())
				{
					continue;
				}

				const FSpotLightParams& Params = Env.GetSpotLight(SpotIndex);
				if (!Frame.bIsOrtho
					&& !Frame.FrustumVolume.IntersectAABB(FShadowUtil::MakeSphereBounds(Params.Position, Params.AttenuationRadius)))
				{
					continue;
				}

				ShadowedSpotIndices.push_back(SpotIndex);
			}
#pragma endregion	

			TArray<FShadowAtlasRequest> AtlasRequests;
			BuildShadowAtlasRequests(Frame, Env, AtlasPool, ShadowedSpotIndices, bShadowDirectional, CurrentShadowAtlasFrame, AtlasRequests);
			ReleaseInvalidExistingHandleSets(AtlasRequests, AtlasPool);
			SelectShadowAtlasRequests(
				AtlasRequests,
				AtlasPool,
				Frame.RenderOptions.ShadowMethod,
				CurrentShadowAtlasFrame,
				Frame.RenderOptions.ShadowAtlasPriority);
			const uint32 StaleReleasedCount = ReleaseStaleAtlasShadowHandles(Env, CurrentShadowAtlasFrame);
			LogShadowAtlasSelection(AtlasRequests, AtlasPool, StaleReleasedCount);

			// Point lights keep the cube-shadow path. Atlas2D requests are allocated only after priority selection.
#pragma region CreateRenderTask

	//PointLightTask 생성 관련
#pragma region PointLightTask
			for (uint32 PointIndex : ShadowedPointIndices)
			{
				const UPointLightComponent* PointLight = Env.GetPointLightOwner(PointIndex);
				const FPointLightParams& Params = Env.GetPointLight(PointIndex);
				if (!PointLight)
				{
					continue;
				}

				FShadowCubeHandle CubeHandle = const_cast<UPointLightComponent*>(PointLight)->AcquireCubeShadowHandleForRenderer();
				if (!CubeHandle.IsValid())
				{
					continue;
				}

				float NearZ = 0.0f;
				float FarZ = 0.0f;
				const FMatrix LightProj = FShadowUtil::MakePointShadowProjection(Params.AttenuationRadius, NearZ, FarZ);
				const D3D11_VIEWPORT CubeViewport = FShadowUtil::MakeFullViewport(FTextureCubeShadowPool::Get().GetResolution(CubeHandle));

				bool bAllFacesValid = true;
				for (uint32 FaceIndex = 0; FaceIndex < FTextureCubeShadowPool::CubeFaceCount; ++FaceIndex)
				{
					if (!FTextureCubeShadowPool::Get().GetFaceDSV(CubeHandle, FaceIndex)
						|| (bUseVSM && !FTextureCubeShadowPool::Get().GetFaceVSMRTV(CubeHandle, FaceIndex)))
					{
						bAllFacesValid = false;
						break;
					}
				}
				if (!bAllFacesValid)
				{
					continue;
				}

				for (uint32 FaceIndex = 0; FaceIndex < FTextureCubeShadowPool::CubeFaceCount; ++FaceIndex)
				{
					const FPointShadowFaceBasis FaceBasis = FTextureCubeShadowPool::GetFaceBasis(FaceIndex);
					const FMatrix LightView = FShadowUtil::MakeAxesViewMatrix(
						Params.Position,
						FaceBasis.Right,
						FaceBasis.Up,
						FaceBasis.Forward);
					const FMatrix LightVP = LightView * LightProj;

					FShadowRenderTask& Task = OutShadowPassData.RenderTasks.emplace_back();
					Task.TargetType = EShadowRenderTargetType::CubeFace;
					Task.LightVP = LightVP;
					Task.ShadowFrustum.UpdateFromMatrix(LightVP);
					Task.Viewport = CubeViewport;
					Task.DSV = FTextureCubeShadowPool::Get().GetFaceDSV(CubeHandle, FaceIndex);
					Task.RTV = bUseVSM ? FTextureCubeShadowPool::Get().GetFaceVSMRTV(CubeHandle, FaceIndex) : nullptr;
					Task.CubeIndex = CubeHandle.CubeIndex;
					Task.CubeTierIndex = CubeHandle.TierIndex;
					Task.CubeFaceIndex = FaceIndex;
					Task.ShadowDepthBias = PointLight->GetShadowBias();
					Task.ShadowSlopeBias = PointLight->GetShadowSlopeBias();
				}

				FShadowInfo Info = {};
				Info.Type = EShadowInfoType::CubeMap;
				Info.ArrayIndex = CubeHandle.CubeIndex;
				Info.LightIndex = PointIndex;
				Info.bIsPSM = 0;
				Info.CubeTierIndex = CubeHandle.TierIndex;
				Info.LightVP = FMatrix::Identity;
				Info.SampleData = FVector4(Params.Position.X, Params.Position.Y, Params.Position.Z, FarZ);
				Info.ShadowParams = FVector4(
					PointLight->GetShadowBias(),
					PointLight->GetShadowSlopeBias(),
					PointLight->GetShadowSharpen(),
					NearZ);

				const int32 ShadowInfoIndex = static_cast<int32>(OutShadowPassData.BindingData.ShadowInfos.size());
				OutShadowPassData.BindingData.ShadowInfos.push_back(Info);
				OutShadowPassData.BindingData.PointLightShadowIndices[PointIndex] = ShadowInfoIndex;
				OutShadowPassData.BindingData.ActivePointLightMask[PointIndex] = 1;
			}
#pragma endregion

			//SpotLightTask 생성 관련
#pragma region SpotLightTask
			for (const FShadowAtlasRequest& AtlasRequest : AtlasRequests)
			{
				if (AtlasRequest.Type != EShadowAtlasRequestType::Spot || !AtlasRequest.bSelected)
				{
					continue;
				}

				const uint32 SpotIndex = AtlasRequest.SpotIndex;
				const USpotLightComponent* SpotLight = Env.GetSpotLightOwner(SpotIndex);
				const FSpotLightParams& Params = Env.GetSpotLight(SpotIndex);
				FShadowHandleSet* HandleSet = AtlasRequest.AllocatedHandleSet;
				if (!HandleSet)
				{
					continue;
				}

				TArray<FAtlasUV> AtlasUVs = AtlasPool.GetAtlasUVArray(HandleSet);
				TArray<ID3D11DepthStencilView*> DSVs = AtlasPool.GetDSVs(HandleSet);
				TArray<ID3D11RenderTargetView*> RTVs = bUseVSM ? AtlasPool.GetRTVs(HandleSet) : TArray<ID3D11RenderTargetView*>();
				const bool bMissingVSMTarget = bUseVSM && (RTVs.empty() || !RTVs[0]);
				if (AtlasUVs.empty() || DSVs.empty() || !DSVs[0] || bMissingVSMTarget)
				{
					continue;
				}

				const float OuterHalfAngle = acosf(FMath::Clamp(Params.OuterConeCos, -1.0f, 1.0f));
				const float NearZ = FMath::Clamp(Params.AttenuationRadius * 0.01f, 0.05f, 5.0f);
				const float FarZ = (Params.AttenuationRadius > NearZ + 1.0f) ? Params.AttenuationRadius : (NearZ + 1.0f);

				FMatrix LightView = FShadowUtil::MakeAxesViewMatrix(
					SpotLight->GetWorldLocation(),
					SpotLight->GetRightVector(),
					SpotLight->GetUpVector(),
					SpotLight->GetForwardVector());
				FMatrix LightProj = FShadowUtil::MakeReversedZPerspective(OuterHalfAngle * 2.0f, 1.0f, NearZ, FarZ);
				FMatrix LightVP = LightView * LightProj;

				FShadowRenderTask& Task = OutShadowPassData.RenderTasks.emplace_back();
				Task.TargetType = EShadowRenderTargetType::Atlas2D;
				Task.LightVP = LightVP;
				Task.ShadowFrustum.UpdateFromMatrix(LightVP);
				Task.Viewport = FShadowUtil::MakeAtlasViewport(AtlasUVs[0], AtlasTextureSize);
				Task.DSV = DSVs[0];
				Task.RTV = bUseVSM ? RTVs[0] : nullptr;
				Task.ShadowDepthBias = SpotLight->GetShadowBias();
				Task.ShadowSlopeBias = SpotLight->GetShadowSlopeBias();
				Task.AtlasSliceIndex = AtlasUVs[0].ArrayIndex;

				FShadowInfo Info = {};
				Info.Type = EShadowInfoType::Atlas2D;
				Info.ArrayIndex = AtlasUVs[0].ArrayIndex;
				Info.LightIndex = NumPointLights + SpotIndex;
				Info.LightVP = LightVP;
				Info.SampleData = FVector4(AtlasUVs[0].u1, AtlasUVs[0].v1, AtlasUVs[0].u2, AtlasUVs[0].v2);
				Info.ShadowParams = FVector4(
					SpotLight->GetShadowBias(),
					SpotLight->GetShadowSlopeBias(),
					SpotLight->GetShadowSharpen(),
					NearZ);

				const int32 ShadowInfoIndex = static_cast<int32>(OutShadowPassData.BindingData.ShadowInfos.size());
				OutShadowPassData.BindingData.ShadowInfos.push_back(Info);
				OutShadowPassData.BindingData.SpotLightShadowIndices[SpotIndex] = ShadowInfoIndex;
				OutShadowPassData.BindingData.ActiveSpotLightMask[SpotIndex] = 1;
			}
#pragma endregion
			//DirectionLightTask 생성 관련
#pragma region DirectionLightTask
	//현재는 반복을 HandleSet의 첫번째 인덱스만 읽고 그놈을 기준으로 Light VP하나 생성하고 한번만 렌더링 하고있어 CSM은 불가,
	// PSM도 아닌 그냥 카메라 시점에서 멀리 떨어져서 HandleSet의 첫번째 해상도를 기준으로 그리는중
			const UDirectionalLightComponent* DirectionalLight = Env.GetGlobalDirectionalLightOwner();
			if (bShadowDirectional && DirectionalLight)
			{
				const FShadowAtlasRequest* DirectionalRequest = nullptr;
				for (const FShadowAtlasRequest& AtlasRequest : AtlasRequests)
				{
					if (AtlasRequest.Type == EShadowAtlasRequestType::DirectionalCascade && AtlasRequest.bSelected)
					{
						DirectionalRequest = &AtlasRequest;
						break;
					}
				}

				FShadowHandleSet* HandleSet = DirectionalRequest ? DirectionalRequest->AllocatedHandleSet : nullptr;
				TArray<FAtlasUV> AtlasUVs = HandleSet ? AtlasPool.GetAtlasUVArray(HandleSet) : TArray<FAtlasUV>();
				TArray<ID3D11DepthStencilView*> DSVs = HandleSet ? AtlasPool.GetDSVs(HandleSet) : TArray<ID3D11DepthStencilView*>();
				TArray<ID3D11RenderTargetView*> RTVs = (bUseVSM && HandleSet) ? AtlasPool.GetRTVs(HandleSet) : TArray<ID3D11RenderTargetView*>();
				OutShadowPassData.BindingData.ShadowMethod = static_cast<uint32>(Frame.RenderOptions.ShadowMethod);

				if (!AtlasUVs.empty() && !DSVs.empty() && DSVs[0] && (!bUseVSM || (!RTVs.empty() && RTVs[0])))
				{
					if (Frame.RenderOptions.ShadowMethod == EShadowMethod::Standard || Frame.RenderOptions.ShadowMethod == EShadowMethod::PSM)
					{
						FMatrix FinalLightVP = FMatrix::Identity;
						FPerspectiveShadowDebugData PerspectiveDebugData;
						float ShadowNearZ = 0.1f;
						uint32 bIsPSM_Flag = 0;
						bool bPSMFlipNegativeW = false;

						if (Frame.RenderOptions.ShadowMethod == EShadowMethod::PSM
							&& FShadowUtil::MakePerspectiveShadowMatrix(Frame, *DirectionalLight, FinalLightVP, ShadowNearZ, bPSMFlipNegativeW, &PerspectiveDebugData))
						{
							bIsPSM_Flag = 1;
						}
						else
						{
							const D3D11_VIEWPORT ShadowViewport = FShadowUtil::MakeAtlasViewport(AtlasUVs[0], AtlasTextureSize);
							FShadowUtil::MakeDirectionalShadowMatrix(
								Frame,
								*DirectionalLight,
								FinalLightVP,
								ShadowNearZ,
								ShadowViewport.Width,
								ShadowViewport.Height);
						}

						const bool bIsPSM = (bIsPSM_Flag != 0);
						const float DirectionalShadowBias = bIsPSM
							? DirectionalLight->GetShadowBias() * 0.02f
							: DirectionalLight->GetShadowBias() * 0.05f;
						const float DirectionalShadowSlopeBias = bIsPSM
							? DirectionalLight->GetShadowSlopeBias() * 0.05f
							: DirectionalLight->GetShadowSlopeBias() * 0.15f;

						FShadowRenderTask& Task = OutShadowPassData.RenderTasks.emplace_back();
						Task.TargetType = EShadowRenderTargetType::Atlas2D;
						Task.LightVP = FinalLightVP;
						Task.bIsPSM = bIsPSM;
						Task.CameraVP = Frame.View * Frame.Proj;
						Task.bCullWithShadowFrustum = !Task.bIsPSM;
						Task.ShadowDepthBias = DirectionalShadowBias;
						Task.ShadowSlopeBias = DirectionalShadowSlopeBias;
						Task.bPSMFlipNegativeW = bPSMFlipNegativeW;
						if (Task.bIsPSM && Frame.RenderOptions.ShowFlags.bPSMDebugLines)
						{
							AddPerspectiveShadowDebug(OutShadowPassData.DebugLines, Frame, PerspectiveDebugData);
						}

						if (Task.bIsPSM)
						{
							Task.ShadowFrustum = Frame.FrustumVolume;
						}
						else
						{
							Task.ShadowFrustum.UpdateFromMatrix(FinalLightVP);
						}

						if (!AtlasUVs.empty() && !DSVs.empty())
						{
							Task.Viewport = FShadowUtil::MakeAtlasViewport(AtlasUVs[0], AtlasTextureSize);
							Task.DSV = DSVs[0];
							Task.RTV = (bUseVSM && !RTVs.empty()) ? RTVs[0] : nullptr;
							Task.AtlasSliceIndex = AtlasUVs[0].ArrayIndex;

							FShadowInfo Info = {};
							Info.Type = EShadowInfoType::Atlas2D;
							Info.ArrayIndex = AtlasUVs[0].ArrayIndex;
							Info.LightIndex = 0xffffffffu;
							Info.bIsPSM = bIsPSM_Flag;
							Info.LightVP = FinalLightVP;
							Info.SampleData = FVector4(AtlasUVs[0].u1, AtlasUVs[0].v1, AtlasUVs[0].u2, AtlasUVs[0].v2);
							Info.ShadowParams = FVector4(DirectionalShadowBias, DirectionalShadowSlopeBias, DirectionalLight->GetShadowSharpen(), ShadowNearZ);

							OutShadowPassData.BindingData.DirectionalShadowIndex =
								static_cast<int32>(OutShadowPassData.BindingData.ShadowInfos.size());
							OutShadowPassData.BindingData.ShadowInfos.push_back(Info);
						}
						else
						{
							// If we can't allocate a shadow map, remove the task we just added
							OutShadowPassData.RenderTasks.pop_back();
						}
					}
					else if (Frame.RenderOptions.ShadowMethod == EShadowMethod::CSM)
					{
						int32 MaxCascades = static_cast<int32>(std::min(AtlasUVs.size(), DSVs.size()));
						if (bUseVSM && !RTVs.empty()) MaxCascades = static_cast<int32>(std::min(static_cast<size_t>(MaxCascades), RTVs.size()));
						const int32 SelectedCascadeCount = DirectionalRequest ? static_cast<int32>(GetContiguousSelectedCascadeCount(*DirectionalRequest)) : 0;
						const int32 NumCascades = std::min(std::min(MaxCascades, 4), SelectedCascadeCount);
						OutShadowPassData.BindingData.NumCascades = NumCascades;
						const int32 BaseShadowInfoIndex = static_cast<int32>(OutShadowPassData.BindingData.ShadowInfos.size());

						if (NumCascades > 0)
						{
							float CascadeRanges[5]; // Near, Split1, Split2, Split3, Far
							for (float& CascadeRange : CascadeRanges)
							{
								CascadeRange = std::min(Frame.FarClip, 200.0f);
							}
							CascadeRanges[0] = Frame.NearClip;
							CascadeRanges[NumCascades] = std::min(Frame.FarClip, 200.0f); // Limit shadow distance

							// Logarithmic split scheme
							float Lambda = 0.6f; // Balance near detail with smoother far cascade resolution
							for (int i = 1; i < NumCascades; ++i)
							{
								float f = (float)i / (float)NumCascades;
								float LogSplit = CascadeRanges[0] * powf(CascadeRanges[NumCascades] / CascadeRanges[0], f);
								float UniSplit = CascadeRanges[0] + (CascadeRanges[NumCascades] - CascadeRanges[0]) * f;
								CascadeRanges[i] = Lambda * LogSplit + (1.0f - Lambda) * UniSplit;
							}

							FMatrix InvView = Frame.View.GetInverse();
							// Extract FOV and Aspect from Projection matrix
							// Proj.M[1][1] = 1/tan(FovY/2), Proj.M[0][0] = Cot/Aspect
							float TanHalfFovY = 1.0f / Frame.Proj.M[1][1];
							float Aspect = Frame.Proj.M[1][1] / Frame.Proj.M[0][0];

							FVector LightDir = DirectionalLight->GetForwardVector();
							FVector LightUp = DirectionalLight->GetUpVector();
							FVector LightRight = DirectionalLight->GetRightVector();
							const float CSMShadowBias = DirectionalLight->GetShadowBias() * 0.03f;
							const float CSMShadowSlopeBias = DirectionalLight->GetShadowSlopeBias() * 0.12f;

							for (int i = 0; i < NumCascades; ++i)
							{
								float zNear = CascadeRanges[i];
								float zFar = CascadeRanges[i + 1];

								// Calculate 8 corners of sub-frustum in View Space
								float yNear = zNear * TanHalfFovY;
								float xNear = yNear * Aspect;
								float yFar = zFar * TanHalfFovY;
								float xFar = yFar * Aspect;

								FVector Corners[8] = {
									{-xNear,  yNear, zNear}, { xNear,  yNear, zNear}, { xNear, -yNear, zNear}, {-xNear, -yNear, zNear},
									{-xFar,   yFar,  zFar }, { xFar,   yFar,  zFar }, { xFar,  -yFar,  zFar }, {-xFar,  -yFar,  zFar }
								};

								// Transform corners to World Space and find center
								FVector Center(0, 0, 0);
								for (int j = 0; j < 8; ++j)
								{
									Corners[j] = InvView.TransformPositionWithW(Corners[j]);
									Center += Corners[j];
								}
								Center /= 8.0f;

								// Find radius for tight bounding sphere
								float Radius = 0.0f;
								for (int j = 0; j < 8; ++j) Radius = std::max(Radius, (Corners[j] - Center).Length());
								Radius = std::ceil(Radius * 16.0f) / 16.0f;

								const D3D11_VIEWPORT CascadeViewport = FShadowUtil::MakeAtlasViewport(AtlasUVs[i], AtlasTextureSize);
								Center = FShadowUtil::SnapCenterToShadowTexels(
									Center,
									LightRight,
									LightUp,
									Radius * 2.0f,
									Radius * 2.0f,
									CascadeViewport.Width,
									CascadeViewport.Height);

								// Create tight Orthographic projection
								FVector Eye = Center - LightDir * Radius * 2.0f;
								FMatrix LightView = FShadowUtil::MakeAxesViewMatrix(Eye, LightRight, LightUp, LightDir);
								FMatrix LightProj = FShadowUtil::MakeReversedZOrthographic(Radius * 2.0f, Radius * 2.0f, 0.1f, Radius * 6.0f);
								FMatrix LightVP = LightView * LightProj;

								const float CascadeBiasScale = 1.0f + static_cast<float>(i) * 0.35f;
								const float CascadeShadowBias = CSMShadowBias * CascadeBiasScale;
								const float CascadeShadowSlopeBias = CSMShadowSlopeBias * CascadeBiasScale;

								FShadowRenderTask& Task = OutShadowPassData.RenderTasks.emplace_back();
								Task.TargetType = EShadowRenderTargetType::Atlas2D;
								Task.LightVP = LightVP;
								Task.bIsPSM = false;
								Task.CameraVP = Frame.View * Frame.Proj;
								Task.ShadowFrustum.UpdateFromMatrix(LightVP);
								Task.Viewport = FShadowUtil::MakeAtlasViewport(AtlasUVs[i], AtlasTextureSize);
								Task.DSV = DSVs[i];
								Task.RTV = (bUseVSM && i < RTVs.size()) ? RTVs[i] : nullptr;
								Task.ShadowDepthBias = CascadeShadowBias;
								Task.ShadowSlopeBias = CascadeShadowSlopeBias;
								Task.AtlasSliceIndex = AtlasUVs[i].ArrayIndex;

								FShadowInfo Info = {};
								Info.Type = EShadowInfoType::Atlas2D;
								Info.ArrayIndex = AtlasUVs[i].ArrayIndex;
								Info.LightIndex = 0xffffffffu;
								Info.bIsPSM = 0;
								Info.LightVP = LightVP;
								Info.SampleData = FVector4(AtlasUVs[i].u1, AtlasUVs[i].v1, AtlasUVs[i].u2, AtlasUVs[i].v2);
								Info.ShadowParams = FVector4(CascadeShadowBias, CascadeShadowSlopeBias, DirectionalLight->GetShadowSharpen(), 0.1f);

								OutShadowPassData.BindingData.CascadeMatrices[i] = LightVP;
								OutShadowPassData.BindingData.ShadowInfos.push_back(Info);
							}

							// Fill cascade splits for shader
							OutShadowPassData.BindingData.CascadeSplits = FVector4(CascadeRanges[1], CascadeRanges[2], CascadeRanges[3], CascadeRanges[4]);
							OutShadowPassData.BindingData.DirectionalShadowIndex = BaseShadowInfoIndex;
						}
					}
				}
			}
#pragma endregion
#pragma endregion

		}

		//생성된 ShadowTask들에 대해서 렌더링해서 ShadowMap 생성하는 과정. VSM 아직 불가
		void FRenderer::RenderShadowPass(const FFrameContext & Frame, FScene & Scene, const FShadowPassData & ShadowPassData)
		{
			if (ShadowPassData.RenderTasks.empty())
			{
				return;
			}

			ID3D11DeviceContext* Ctx = Device.GetDeviceContext();
			const bool bUseVSM = Frame.RenderOptions.ShadowFilterMode == EShadowFilterMode::VSM;

			FShader* ShadowDepthShader = FShaderManager::Get().GetOrCreate(EShaderPath::ShadowDepth);
			FShader* ShadowClearShader = FShaderManager::Get().GetOrCreate(EShaderPath::ShadowClear);
			FShader* ShadowDepthShaderVSM = bUseVSM
				? FShaderManager::Get().GetOrCreate(FShaderKey(EShaderPath::ShadowDepth, EShadowPassDefines::VSM))
				: nullptr;
			FShader* ShadowClearShaderVSM = bUseVSM
				? FShaderManager::Get().GetOrCreate(FShaderKey(EShaderPath::ShadowClear, EShadowPassDefines::VSM))
				: nullptr;
			if (!ShadowDepthShader || !ShadowClearShader || (bUseVSM && (!ShadowDepthShaderVSM || !ShadowClearShaderVSM)))
			{
				return;
			}

			Resources.UnbindShadowResources(Device);
			Resources.SetRasterizerState(Device, ERasterizerState::SolidBackCull);

			ID3D11Buffer* ShadowPassCBHandle = ShadowPassBuffer.GetBuffer();
			ID3D11Device* D3DDevice = Device.GetDevice();
			auto CreateShadowRasterizerState = [D3DDevice](const FShadowRenderTask& Task) -> ID3D11RasterizerState*
				{
					if (!D3DDevice)
					{
						return nullptr;
					}

					constexpr float DepthBiasScale = 100000.0f;
					const float ClampedDepthBias = FMath::Clamp(Task.ShadowDepthBias, 0.0f, 0.05f);
					const float ClampedSlopeBias = FMath::Clamp(Task.ShadowSlopeBias, 0.0f, 10.0f);

					D3D11_RASTERIZER_DESC RasterizerDesc = {};
					RasterizerDesc.FillMode = D3D11_FILL_SOLID;
					RasterizerDesc.CullMode = Task.bIsPSM ? D3D11_CULL_NONE : D3D11_CULL_BACK;
					RasterizerDesc.FrontCounterClockwise = FALSE;
					RasterizerDesc.DepthBias = -static_cast<INT>(ClampedDepthBias * DepthBiasScale);
					RasterizerDesc.DepthBiasClamp = 0.0f;
					RasterizerDesc.SlopeScaledDepthBias = -ClampedSlopeBias;
					RasterizerDesc.DepthClipEnable = Task.bIsPSM ? FALSE : TRUE;
					RasterizerDesc.ScissorEnable = FALSE;
					RasterizerDesc.MultisampleEnable = FALSE;
					RasterizerDesc.AntialiasedLineEnable = FALSE;

					ID3D11RasterizerState* RasterizerState = nullptr;
					if (FAILED(D3DDevice->CreateRasterizerState(&RasterizerDesc, &RasterizerState)))
					{
						return nullptr;
					}
					return RasterizerState;
				};

			for (const FShadowRenderTask& Task : ShadowPassData.RenderTasks)
			{
				ID3D11DepthStencilView* TaskDSV = Task.DSV.Get();
				ID3D11RenderTargetView* TaskRTV = Task.RTV.Get();
				if (!TaskDSV)
				{
					continue;
				}

				const bool bWriteMoments = bUseVSM && TaskRTV != nullptr;
				FShader* ActiveShadowClearShader = bWriteMoments ? ShadowClearShaderVSM : ShadowClearShader;
				FShader* ActiveShadowDepthShader = bWriteMoments ? ShadowDepthShaderVSM : ShadowDepthShader;

				Resources.SetBlendState(Device, bWriteMoments ? EBlendState::Opaque : EBlendState::NoColor);
				Resources.SetRasterizerState(Device, ERasterizerState::SolidBackCull);

				if (TaskRTV)
				{
					Ctx->OMSetRenderTargets(1, &TaskRTV, TaskDSV);
				}
				else
				{
					Ctx->OMSetRenderTargets(0, nullptr, TaskDSV);
				}
				Ctx->RSSetViewports(1, &Task.Viewport);

				Resources.SetDepthStencilState(Device, EDepthStencilState::ShadowClear);
				ActiveShadowClearShader->Bind(Ctx);
				if (!bWriteMoments)
				{
					Ctx->PSSetShader(nullptr, nullptr, 0);
				}
				Ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				Ctx->Draw(3, 0);

				Resources.SetDepthStencilState(Device, EDepthStencilState::ShadowDepth);
				ActiveShadowDepthShader->Bind(Ctx);
				if (!bWriteMoments)
				{
					Ctx->PSSetShader(nullptr, nullptr, 0);
				}

				ID3D11RasterizerState* ShadowRasterizerState = CreateShadowRasterizerState(Task);
				if (ShadowRasterizerState)
				{
					Ctx->RSSetState(ShadowRasterizerState);
				}

				FShadowPassConstants ShadowPassConstants = {};
				ShadowPassConstants.LightVP = Task.LightVP;
				ShadowPassConstants.CameraVP = Task.CameraVP;
				ShadowPassConstants.bIsPSM = Task.bIsPSM ? 1u : 0u;
				ShadowPassConstants.bPSMFlipNegativeW = Task.bPSMFlipNegativeW ? 1u : 0u;
				ShadowPassBuffer.Update(Ctx, &ShadowPassConstants, sizeof(FShadowPassConstants));
				Ctx->VSSetConstantBuffers(ECBSlot::PerShader0, 1, &ShadowPassCBHandle);

				ID3D11Buffer* CurrentVB = nullptr;
				ID3D11Buffer* CurrentIB = nullptr;
				uint32 CurrentStride = 0;
				FConstantBuffer* CurrentPerObjectCB = nullptr;

				//단순 반복 Frustum컬링 후 DrawCall
				for (FPrimitiveSceneProxy* Proxy : Scene.GetAllProxies())
				{
					if (!IsOpaqueShadowCaster(Proxy)
						|| (Task.bCullWithShadowFrustum && !Task.ShadowFrustum.IntersectAABB(Proxy->GetCachedBounds())))
					{
						continue;
					}

					FConstantBuffer* PerObjectCB = Builder.GetPerObjectCBForShadowPass(*Proxy);
					if (PerObjectCB && Proxy->NeedsPerObjectCBUpload())
					{
						PerObjectCB->Update(Ctx, &Proxy->GetPerObjectConstants(), sizeof(FPerObjectConstants));
						Proxy->ClearPerObjectCBDirty();
					}

					if (PerObjectCB && PerObjectCB != CurrentPerObjectCB)
					{
						ID3D11Buffer* RawPerObjectCB = PerObjectCB->GetBuffer();
						Ctx->VSSetConstantBuffers(ECBSlot::PerObject, 1, &RawPerObjectCB);
						CurrentPerObjectCB = PerObjectCB;
					}

					ID3D11Buffer* VB = Proxy->GetMeshBuffer()->GetVertexBuffer().GetBuffer();
					ID3D11Buffer* IB = Proxy->GetMeshBuffer()->GetIndexBuffer().GetBuffer();
					uint32 Stride = Proxy->GetMeshBuffer()->GetVertexBuffer().GetStride();
					if (!VB || !IB)
					{
						continue;
					}

					if (VB != CurrentVB || Stride != CurrentStride)
					{
						UINT Offset = 0;
						Ctx->IASetVertexBuffers(0, 1, &VB, &Stride, &Offset);
						CurrentVB = VB;
						CurrentStride = Stride;
					}

					if (IB != CurrentIB)
					{
						Ctx->IASetIndexBuffer(IB, DXGI_FORMAT_R32_UINT, 0);
						CurrentIB = IB;
					}

					Ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					for (const FMeshSectionDraw& Section : Proxy->GetSectionDraws())
					{
						if (Section.IndexCount == 0)
						{
							continue;
						}

						Ctx->DrawIndexed(Section.IndexCount, Section.FirstIndex, 0);
					}
				}

				if (ShadowRasterizerState)
				{
					ShadowRasterizerState->Release();
				}
				Resources.ResetRenderStateCache();
			}

			if (bUseVSM)
			{
				RenderVSMBlurPass(Scene.GetShadowAtlasPool(), ShadowPassData);
			}

			Resources.ResetRenderStateCache();

			if (Frame.ViewportRTV)
			{
				Ctx->OMSetRenderTargets(1, &Frame.ViewportRTV, Frame.ViewportDSV);
			}
			else if (Frame.ViewportDSV)
			{
				Ctx->OMSetRenderTargets(0, nullptr, Frame.ViewportDSV);
			}
			else
			{
				Ctx->OMSetRenderTargets(0, nullptr, nullptr);
			}
			D3D11_VIEWPORT MainViewport = {};
			MainViewport.Width = Frame.ViewportWidth;
			MainViewport.Height = Frame.ViewportHeight;
			MainViewport.MinDepth = 0.0f;
			MainViewport.MaxDepth = 1.0f;
			Ctx->RSSetViewports(1, &MainViewport);
		}

		void FRenderer::RenderVSMBlurPass(FTextureAtlasPool & AtlasPool, const FShadowPassData & ShadowPassData)
		{
			struct FPreparedVSMBlurRegion
			{
				uint32 TextureSize = 0;
				uint32 SliceIndex = static_cast<uint32>(-1);
				D3D11_BOX Box = {};
				ID3D11ShaderResourceView* SourceSRV = nullptr;
				ID3D11ShaderResourceView* TempSRV = nullptr;
				ID3D11RenderTargetView* TempRTV = nullptr;
				ID3D11RenderTargetView* FilteredRTV = nullptr;
			};

			ID3D11DeviceContext* Ctx = Device.GetDeviceContext();
			ID3D11ShaderResourceView* RawSRV = AtlasPool.GetRawSRV();
			ID3D11ShaderResourceView* TempSRV = AtlasPool.GetTempSRV();
			FShader* BlurShader = FShaderManager::Get().GetOrCreate(EShaderPath::Gaussianblur);
			ID3D11Buffer* BlurPassCBHandle = VSMBlurPassBuffer.GetBuffer();

			if (!Ctx || !BlurShader || !BlurShader->IsValid() || !BlurPassCBHandle)
			{
				return;
			}

			TArray<FPreparedVSMBlurRegion> PreparedRegions;
			PreparedRegions.reserve(ShadowPassData.RenderTasks.size());

			const uint32 AtlasTextureSize = AtlasPool.GetTextureSize();
			const uint32 AtlasLayerCount = AtlasPool.GetAllocatedLayerCount();
			for (const FShadowRenderTask& Task : ShadowPassData.RenderTasks)
			{
				if (Task.TargetType != EShadowRenderTargetType::Atlas2D
					|| !Task.RTV.Get()
					|| Task.AtlasSliceIndex == static_cast<uint32>(-1))
				{
					continue;
				}

				bool bValidRegion = false;
				const D3D11_BOX BlurBox = MakeViewportBlurBox(Task.Viewport, AtlasTextureSize, bValidRegion);
				if (!bValidRegion || Task.AtlasSliceIndex >= AtlasLayerCount)
				{
					continue;
				}

				FPreparedVSMBlurRegion Region = {};
				Region.TextureSize = AtlasTextureSize;
				Region.SliceIndex = Task.AtlasSliceIndex;
				Region.Box = BlurBox;
				Region.SourceSRV = RawSRV;
				Region.TempSRV = TempSRV;
				Region.TempRTV = AtlasPool.GetTempRTV(Task.AtlasSliceIndex);
				Region.FilteredRTV = AtlasPool.GetFilteredRTV(Task.AtlasSliceIndex);
				if (!Region.SourceSRV || !Region.TempSRV || !Region.TempRTV || !Region.FilteredRTV)
				{
					continue;
				}

				PreparedRegions.push_back(Region);
			}

			for (const FShadowRenderTask& Task : ShadowPassData.RenderTasks)
			{
				if (Task.TargetType != EShadowRenderTargetType::CubeFace
					|| !Task.RTV
					|| Task.CubeIndex == static_cast<uint32>(-1)
					|| Task.CubeTierIndex == static_cast<uint32>(-1)
					|| Task.CubeFaceIndex >= FTextureCubeShadowPool::CubeFaceCount)
				{
					continue;
				}

				FShadowCubeHandle CubeHandle;
				CubeHandle.CubeIndex = Task.CubeIndex;
				CubeHandle.TierIndex = Task.CubeTierIndex;

				const uint32 CubeTextureSize = FTextureCubeShadowPool::Get().GetResolution(CubeHandle);
				if (CubeTextureSize == 0)
				{
					continue;
				}

				FPreparedVSMBlurRegion Region = {};
				Region.TextureSize = CubeTextureSize;
				Region.SliceIndex = Task.CubeIndex * FTextureCubeShadowPool::CubeFaceCount + Task.CubeFaceIndex;
				Region.Box.left = 0;
				Region.Box.top = 0;
				Region.Box.front = 0;
				Region.Box.right = CubeTextureSize;
				Region.Box.bottom = CubeTextureSize;
				Region.Box.back = 1;
				Region.SourceSRV = FTextureCubeShadowPool::Get().GetFilteredVSMArraySRV(Task.CubeTierIndex);
				Region.TempSRV = FTextureCubeShadowPool::Get().GetTempVSMArraySRV(Task.CubeTierIndex);
				Region.TempRTV = FTextureCubeShadowPool::Get().GetTempFaceVSMRTV(CubeHandle, Task.CubeFaceIndex);
				Region.FilteredRTV = FTextureCubeShadowPool::Get().GetFilteredFaceVSMRTV(CubeHandle, Task.CubeFaceIndex);
				if (!Region.SourceSRV || !Region.TempSRV || !Region.TempRTV || !Region.FilteredRTV)
				{
					continue;
				}

				PreparedRegions.push_back(Region);
			}

			if (PreparedRegions.empty())
			{
				return;
			}

			constexpr UINT BlurSRVSlot = 0;
			ID3D11ShaderResourceView* NullSRV = nullptr;
			ID3D11Buffer* NullVB = nullptr;
			const UINT NullStride = 0;
			const UINT NullOffset = 0;

			Resources.SetBlendState(Device, EBlendState::Opaque);
			Resources.SetDepthStencilState(Device, EDepthStencilState::NoDepth);
			Resources.SetRasterizerState(Device, ERasterizerState::SolidNoCull);

			Ctx->IASetInputLayout(nullptr);
			Ctx->IASetVertexBuffers(0, 1, &NullVB, &NullStride, &NullOffset);
			Ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
			Ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			BlurShader->Bind(Ctx);
			Ctx->PSSetConstantBuffers(ECBSlot::PerShader0, 1, &BlurPassCBHandle);

			for (const FPreparedVSMBlurRegion& Region : PreparedRegions)
			{
				if (!Region.SourceSRV || !Region.TempSRV || !Region.TempRTV || !Region.FilteredRTV || Region.TextureSize == 0)
				{
					continue;
				}

				D3D11_VIEWPORT BlurViewport = {};
				BlurViewport.TopLeftX = static_cast<float>(Region.Box.left);
				BlurViewport.TopLeftY = static_cast<float>(Region.Box.top);
				BlurViewport.Width = static_cast<float>(Region.Box.right - Region.Box.left);
				BlurViewport.Height = static_cast<float>(Region.Box.bottom - Region.Box.top);
				BlurViewport.MinDepth = 0.0f;
				BlurViewport.MaxDepth = 1.0f;

				D3D11_RECT BlurScissor = {};
				BlurScissor.left = static_cast<LONG>(Region.Box.left);
				BlurScissor.top = static_cast<LONG>(Region.Box.top);
				BlurScissor.right = static_cast<LONG>(Region.Box.right);
				BlurScissor.bottom = static_cast<LONG>(Region.Box.bottom);

				Ctx->RSSetViewports(1, &BlurViewport);
				Ctx->RSSetScissorRects(1, &BlurScissor);
				FVSMBlurPassConstants BlurConstants = {};
				BlurConstants.SourceUVRect[0] = static_cast<float>(Region.Box.left) / static_cast<float>(Region.TextureSize);
				BlurConstants.SourceUVRect[1] = static_cast<float>(Region.Box.top) / static_cast<float>(Region.TextureSize);
				BlurConstants.SourceUVRect[2] = static_cast<float>(Region.Box.right) / static_cast<float>(Region.TextureSize);
				BlurConstants.SourceUVRect[3] = static_cast<float>(Region.Box.bottom) / static_cast<float>(Region.TextureSize);
				BlurConstants.InvTextureSize[0] = 1.0f / static_cast<float>(Region.TextureSize);
				BlurConstants.InvTextureSize[1] = 1.0f / static_cast<float>(Region.TextureSize);
				BlurConstants.SourceSlice = Region.SliceIndex;

				BlurConstants.BlurDirection[0] = 1.0f;
				BlurConstants.BlurDirection[1] = 0.0f;
				VSMBlurPassBuffer.Update(Ctx, &BlurConstants, sizeof(FVSMBlurPassConstants));
				Ctx->OMSetRenderTargets(1, &Region.TempRTV, nullptr);
				Ctx->PSSetShaderResources(BlurSRVSlot, 1, &Region.SourceSRV);
				Ctx->Draw(3, 0);
				Ctx->PSSetShaderResources(BlurSRVSlot, 1, &NullSRV);

				BlurConstants.BlurDirection[0] = 0.0f;
				BlurConstants.BlurDirection[1] = 1.0f;
				VSMBlurPassBuffer.Update(Ctx, &BlurConstants, sizeof(FVSMBlurPassConstants));
				Ctx->OMSetRenderTargets(1, &Region.FilteredRTV, nullptr);
				Ctx->PSSetShaderResources(BlurSRVSlot, 1, &Region.TempSRV);
				Ctx->Draw(3, 0);
				Ctx->PSSetShaderResources(BlurSRVSlot, 1, &NullSRV);
			}

			Ctx->OMSetRenderTargets(0, nullptr, nullptr);
		}

		// ============================================================
		// Render — 정렬 + GPU 제출
		// BeginCollect + Collector + BuildDynamicCommands 이후에 호출.
		// ============================================================
		void FRenderer::Render(const FFrameContext & Frame, FScene & Scene)
		{
			FDrawCallStats::Reset();
			Scene.InitializeShadowAtlas(Device.GetDevice(), Device.GetDeviceContext(), 4096);

			{
				SCOPE_STAT_CAT("UpdateFrameBuffer", "4_ExecutePass");
				Resources.UpdateFrameBuffer(Device, Frame);
			}

			Resources.BindSystemSamplers(Device);

			Resources.UnbindShadowResources(Device);

			FShadowPassData ShadowPassData;
			BuildShadowPassData(Frame, Scene, ShadowPassData);
			if (!ShadowPassData.DebugLines.empty())
			{
				Builder.BuildLateEditorLineCommands(Frame.RenderOptions.ViewMode, ShadowPassData.DebugLines);
			}

			{
				SCOPE_STAT_CAT("ShadowPass", "4_ExecutePass");
				RenderShadowPass(Frame, Scene, ShadowPassData);
			}

			{
				SCOPE_STAT_CAT("UpdateLightBuffer", "4_ExecutePass");

				FClusterCullingState& ClusterState = ClusteredLightCuller.GetCullingState();
				ClusterState.NearZ = Frame.NearClip;
				ClusterState.FarZ = Frame.FarClip;
				ClusterState.ScreenWidth = static_cast<uint32>(Frame.ViewportWidth);
				ClusterState.ScreenHeight = static_cast<uint32>(Frame.ViewportHeight);

				Resources.UpdateLightBuffer(Device, Scene, Frame, &ClusterState, &ShadowPassData.BindingData);
			}

			Resources.BindShadowResources(Device, Scene.GetShadowAtlasPool());

			FDrawCommandList& CommandList = Builder.GetCommandList();
			CommandList.Sort();

			FStateCache Cache;
			Cache.Reset();
			Cache.RTV = Frame.ViewportRTV;
			Cache.DSV = Frame.ViewportDSV;

			TArray<FPassEvent> PrePassEvents;
			TArray<FPassEvent> PostPassEvents;
			PassEventBuilder.Build(Device, Frame, Cache, this, PrePassEvents, PostPassEvents);

			for (uint32 i = 0; i < (uint32)ERenderPass::MAX; ++i)
			{
				ERenderPass CurPass = static_cast<ERenderPass>(i);

				for (auto& PrePassEvent : PrePassEvents)
				{
					PrePassEvent.TryExecute(CurPass);
				}

				uint32 Start, End;
				CommandList.GetPassRange(CurPass, Start, End);
				if (Start < End)
				{
					const char* PassName = GetRenderPassName(CurPass);
					SCOPE_STAT_CAT(PassName, "4_ExecutePass");
					GPU_SCOPE_STAT(PassName);
					CommandList.SubmitRange(Start, End, Device, Resources, Cache);
				}

				for (auto& PostPassEvent : PostPassEvents)
				{
					PostPassEvent.TryExecute(CurPass);
				}
			}

			CleanupPassState(Cache);
		}

		// ============================================================
		// CleanupPassState — 패스 루프 종료 후 시스템 텍스처 언바인딩 + 캐시 정리
		// ============================================================
		void FRenderer::CleanupPassState(FStateCache & Cache)
		{
			Resources.UnbindShadowResources(Device);
			Resources.UnbindSystemTextures(Device);
			Resources.UnbindTileCullingBuffers(Device);
			UnbindClusterCullingResources();

			Cache.Cleanup(Device.GetDeviceContext());
			Builder.GetCommandList().Reset();
		}

		void FRenderer::DispatchClusterCullingResources()
		{
			if (!ClusteredLightCuller.IsInitialized())
			{
				return;
			}

			Resources.UnbindTileCullingBuffers(Device);
			UnbindClusterCullingResources();

			{
				GPU_SCOPE_STAT_CAT("Cluster Culling Dispatch", "Culling Dispatch");
				ClusteredLightCuller.DispatchLightCullingCS(Resources.ForwardLights.LightBufferSRV);
			}

			BindClusterCullingResources();
		}

		void FRenderer::BindClusterCullingResources()
		{
			ID3D11DeviceContext* Ctx = Device.GetDeviceContext();
			ID3D11ShaderResourceView* LightIndexList = ClusteredLightCuller.GetLightIndexListSRV();
			ID3D11ShaderResourceView* LightGridList = ClusteredLightCuller.GetLightGridSRV();
			Ctx->VSSetShaderResources(ELightTexSlot::ClusterLightIndexList, 1, &LightIndexList);
			Ctx->VSSetShaderResources(ELightTexSlot::ClusterLightGrid, 1, &LightGridList);
			Ctx->PSSetShaderResources(ELightTexSlot::ClusterLightIndexList, 1, &LightIndexList);
			Ctx->PSSetShaderResources(ELightTexSlot::ClusterLightGrid, 1, &LightGridList);
		}

		void FRenderer::UnbindClusterCullingResources()
		{
			ID3D11DeviceContext* Ctx = Device.GetDeviceContext();
			ID3D11ShaderResourceView* NullSRVs[2] = {};
			Ctx->VSSetShaderResources(ELightTexSlot::ClusterLightIndexList, 2, NullSRVs);
			Ctx->PSSetShaderResources(ELightTexSlot::ClusterLightIndexList, 2, NullSRVs);
			Ctx->CSSetShaderResources(ELightTexSlot::ClusterLightIndexList, 2, NullSRVs);
		}

		void FRenderer::EndFrame()
		{
			Device.Present();
		}
