#include "ParticleEditorWidget.h"

#include "Component/ParticleSystemComponent.h"
#include "Editor/EditorEngine.h"
#include "Editor/UI/ContentBrowser/ContentItem.h"
#include "Editor/UI/EditorTextureManager.h"
#include "GameFramework/AActor.h"
#include "Input/InputSystem.h"
#include "GameFramework/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Object/ObjectFactory.h"
#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/ParticleModule.h"
#include "Particle/ParticleSpriteEmitter.h"
#include "Particle/ParticleSystem.h"
#include "Particle/ParticleSystemManager.h"
#include "Particle/BeamModule/ParticleModuleBeamNoise.h"
#include "Particle/BeamModule/ParticleModuleBeamSource.h"
#include "Particle/BeamModule/ParticleModuleBeamTarget.h"
#include "Particle/TypeData/ParticleModuleTypeDataBeam2.h"
#include "Particle/TypeData/ParticleModuleTypeDataRibbon.h"
#include "Platform/Paths.h"
#include "Runtime/Engine.h"
#include "Settings/EditorSettings.h"
#include "Slate/SlateApplication.h"
#include "UI/Toolbar/ViewportToolbar.h"
#include "Viewport/Viewport.h"
#include "Mesh/MeshManager.h"
#include "Mesh/StaticMesh.h"
#include "Render/Shader/ShaderManager.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <string>
#include <vector>

namespace
{
	static uint32 GNextParticleEditorInstanceId = 0;

	constexpr const char* DefaultParticleMeshPath =
		"Asset/Mesh/BasicShape/Sphere_Lowpoly_StaticMesh.uasset";
	constexpr const char* DefaultParticleMeshMaterialPath =
		"Asset/Materials/Editor/DefaultParticleMesh.mat";
	constexpr const char* DefaultParticleRibbonMaterialPath =
		"Asset/Materials/Editor/DefaultParticleRibbon.mat";

	const char* GScreenAlignmentNames[] =
	{
		"PSA Square",
		"PSA Rectangle",
		"PSA Velocity",
		"PSA Away From Center",
		"PSA Type Specific",
		"PSA Facing Camera Position"
	};

	const char* GSortModeNames[] =
	{
		"None",
		"View Projection Depth",
		"Distance To View",
		"Age Oldest First",
		"Age Newest First"
	};

	const char* GParticleAlphaSourceNames[] =
	{
		"Texture Alpha",
		"Texture Luminance"
	};

	const char* GDistributionModeNames[] =
	{
		"Constant",
		"Uniform",
		"Constant Curve",
		"Uniform Curve"
	};

	enum class EDistributionTrackRole
	{
		Constant,
		Min,
		Max,
	};

	struct FCurveEditorTrack
	{
		std::string Label;
		FParticleDistributionFloat* Distribution = nullptr;
		EDistributionTrackRole Role = EDistributionTrackRole::Constant;
		ImU32 Color = IM_COL32_WHITE;
	};

	const char* GBeamMethodNames[] =
	{
		"Distance",
		"Target",
		"Branch"
	};

	const char* GBeamTaperMethodNames[] =
	{
		"None",
		"Full",
		"Partial"
	};

	const char* GBeamSourceTargetMethodNames[] =
	{
		"Default",
		"User Set",
		"Emitter",
		"Particle",
		"Actor"
	};

	const char* GCollisionChannelNames[] =
	{
		"World Static",
		"World Dynamic",
		"Pawn",
		"Projectile",
		"Trigger",
		"Foot IK"
	};

	const char* GParticleCollisionResponseNames[] =
	{
		"Bounce",
		"Stop",
		"Kill"
	};

	const char* GParticleEventTypeNames[] =
	{
		"Any",
		"Spawn",
		"Death",
		"Collision",
		"Burst",
		"Blueprint"
	};

	const char* GTrailRenderAxisNames[] =
	{
		"Camera Up",
		"Source Up",
		"World Up"
	};

	FString GetParticleEditorIconPath(const wchar_t* FileName)
	{
		return FPaths::ToUtf8(FPaths::Combine(
			FPaths::AssetDir(),
			L"Editor/Icons/ParticleEditor",
			FileName));
	}

	ImU32 GetModuleRowColor(bool bSelected, int32 ModuleIndex)
	{
		if (bSelected)
		{
			return IM_COL32(245, 215, 42, 255);
		}

		static constexpr ImU32 Colors[] =
		{
			IM_COL32(198, 92, 96, 255),
			IM_COL32(45, 47, 58, 255),
			IM_COL32(45, 47, 58, 255),
			IM_COL32(45, 47, 58, 255),
			IM_COL32(45, 47, 58, 255),
			IM_COL32(45, 47, 58, 255)
		};
		const int32 ColorIndex = std::clamp(ModuleIndex, 0, static_cast<int32>(IM_ARRAYSIZE(Colors)) - 1);
		return Colors[ColorIndex];
	}

	FString GetMaterialPath(UMaterialInterface* MaterialInterface)
	{
		UMaterial* Material = MaterialInterface ? MaterialInterface->GetMaterial() : nullptr;
		return Material ? Material->GetAssetPathFileName() : FString();
	}

	UStaticMesh* LoadEditorStaticMesh(const FString& MeshPath)
	{
		if (MeshPath.empty())
		{
			return nullptr;
		}

		if (UStaticMesh* CachedMesh = FMeshManager::FindStaticMesh(MeshPath))
		{
			return CachedMesh;
		}

		ID3D11Device* Device = GEngine
			? GEngine->GetRenderer().GetFD3DDevice().GetDevice()
			: nullptr;

		return Device ? FMeshManager::LoadStaticMesh(MeshPath, Device) : nullptr;
	}

	UMaterial* GetDefaultParticleMeshMaterial()
	{
		return FMaterialManager::Get().GetOrCreateMaterial(DefaultParticleMeshMaterialPath);
	}

	UMaterial* GetDefaultParticleRibbonMaterial()
	{
		return FMaterialManager::Get().GetOrCreateMaterial(DefaultParticleRibbonMaterialPath);
	}

	bool IsCompatibleParticleRibbonMaterial(UMaterialInterface* MaterialInterface)
	{
		const UMaterial* Material = MaterialInterface ? MaterialInterface->GetMaterial() : nullptr;
		if (!Material)
		{
			return false;
		}

		const EBlendState BlendState = Material->GetBlendState();
		if (BlendState != EBlendState::AlphaBlend && BlendState != EBlendState::Additive)
		{
			return false;
		}

		FShader* Shader = Material->GetShader();
		return Shader == FShaderManager::Get().GetOrCreate(EShaderPath::ParticleRibbon);
	}

	bool EnsureParticleMeshEmitterDefaults(UParticleModuleRequired* Required)
	{
		if (!Required)
		{
			return false;
		}

		bool bChanged = false;
		if (!Required->Material)
		{
			Required->Material = GetDefaultParticleMeshMaterial();
			bChanged = Required->Material != nullptr;
		}
		if (Required->ScreenAlignment != PSA_TypeSpecific)
		{
			Required->ScreenAlignment = PSA_TypeSpecific;
			bChanged = true;
		}
		return bChanged;
	}

	bool EnsureParticleRibbonEmitterDefaults(UParticleModuleRequired* Required)
	{
		if (!Required)
		{
			return false;
		}

		bool bChanged = false;
		if (Required->ScreenAlignment != PSA_TypeSpecific)
		{
			Required->ScreenAlignment = PSA_TypeSpecific;
			bChanged = true;
		}
		if (!IsCompatibleParticleRibbonMaterial(Required->Material))
		{
			Required->Material = GetDefaultParticleRibbonMaterial();
			bChanged = Required->Material != nullptr;
		}
		return bChanged;
	}

	bool ExpandSpriteSizeToMeshVolume(UParticleModuleSize* Size)
	{
		if (!Size)
		{
			return false;
		}

		auto ExpandIfSpriteDefault = [](FVector& Value) -> bool
		{
			const float TargetUniformSize = (std::max)(Value.X, Value.Y);
			if (TargetUniformSize <= 1.0f || std::abs(Value.Z - 1.0f) > 0.001f)
			{
				return false;
			}

			Value.Z = TargetUniformSize;
			return true;
		};

		bool bChanged = false;
		bChanged |= ExpandIfSpriteDefault(Size->StartSize);
		bChanged |= ExpandIfSpriteDefault(Size->StartSizeMin);
		bChanged |= ExpandIfSpriteDefault(Size->StartSizeMax);
		return bChanged;
	}

	bool EnsureParticleMeshSizeDefaults(UParticleLODLevel* LOD)
	{
		if (!LOD)
		{
			return false;
		}

		bool bChanged = false;
		for (UParticleModule* Module : LOD->Modules)
		{
			bChanged |= ExpandSpriteSizeToMeshVolume(Cast<UParticleModuleSize>(Module));
		}
		return bChanged;
	}

	UMaterial* AcceptMaterialDrop()
	{
		if (!ImGui::BeginDragDropTarget())
		{
			return nullptr;
		}

		UMaterial* Material = nullptr;
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("MaterialContentItem"))
		{
			const FContentItem* Item = static_cast<const FContentItem*>(Payload->Data);
			if (Item)
			{
				const FString MaterialPath = FPaths::MakeProjectRelative(
					FPaths::ToUtf8(Item->Path.generic_wstring()));
				Material = FMaterialManager::Get().GetOrCreateMaterial(MaterialPath);
			}
		}

		ImGui::EndDragDropTarget();
		return Material;
	}

	bool IsValidAssetFileStem(const FString& Name)
	{
		if (Name.empty())
		{
			return false;
		}

		static constexpr const char* InvalidChars = "<>:\"/\\|?*";
		for (unsigned char Ch : Name)
		{
			if (Ch < 32 || std::strchr(InvalidChars, Ch))
			{
				return false;
			}
		}
		return true;
	}

	void DrawHorizontalSplitter(float& TopHeight, float MinTopHeight, float MinBottomHeight, float AvailableHeight, const char* Id)
	{
		const float SplitterHeight = 6.0f;
		ImGui::InvisibleButton(Id, ImVec2(-1.0f, SplitterHeight));
		if (ImGui::IsItemHovered())
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
		}
		if (ImGui::IsItemActive())
		{
			TopHeight += ImGui::GetIO().MouseDelta.y;
		}

		const float MaxTopHeight = (std::max)(MinTopHeight, AvailableHeight - SplitterHeight - MinBottomHeight);
		TopHeight = std::clamp(TopHeight, MinTopHeight, MaxTopHeight);
	}

	bool DrawParticleToolbarButton(const char* Id, const wchar_t* IconFileName, const char* Label, bool bDisabled)
	{
		ID3D11ShaderResourceView* Icon = FEditorTextureManager::Get().GetOrLoadIcon(GetParticleEditorIconPath(IconFileName));

		const ImGuiStyle& Style = ImGui::GetStyle();
		constexpr float IconSize = 18.0f;
		const float IconTextSpacing = Icon ? 5.0f : 0.0f;
		const ImVec2 TextSize = ImGui::CalcTextSize(Label);
		const ImVec2 ButtonSize(
			Style.FramePadding.x * 2.0f + (Icon ? IconSize : 0.0f) + IconTextSpacing + TextSize.x,
			Style.FramePadding.y * 2.0f + (std::max)(IconSize, TextSize.y));

		ImGui::PushID(Id);
		ImGui::BeginDisabled(bDisabled);
		const bool bClicked = ImGui::Button("##Button", ButtonSize);

		const ImVec2 ButtonMin = ImGui::GetItemRectMin();
		const ImVec2 ButtonMax = ImGui::GetItemRectMax();
		const float ContentWidth = (Icon ? IconSize : 0.0f) + IconTextSpacing + TextSize.x;
		float CursorX = ButtonMin.x + (ButtonMax.x - ButtonMin.x - ContentWidth) * 0.5f;
		const float CenterY = ButtonMin.y + (ButtonMax.y - ButtonMin.y) * 0.5f;

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		if (Icon)
		{
			const ImVec2 IconMin(CursorX, CenterY - IconSize * 0.5f);
			const ImVec2 IconMax(IconMin.x + IconSize, IconMin.y + IconSize);
			DrawList->AddImage(
				reinterpret_cast<ImTextureID>(Icon),
				IconMin,
				IconMax,
				ImVec2(0.0f, 0.0f),
				ImVec2(1.0f, 1.0f),
				ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, bDisabled ? Style.DisabledAlpha : 1.0f)));
			CursorX += IconSize + IconTextSpacing;
		}

		DrawList->AddText(
			ImVec2(CursorX, CenterY - TextSize.y * 0.5f),
			ImGui::GetColorU32(ImGuiCol_Text),
			Label);
		ImGui::EndDisabled();

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("%s", Label);
		}
		ImGui::PopID();

		return bClicked && !bDisabled;
	}

	void EnsureCurveHasDefaultKeys(FFloatCurve& Curve, float StartValue, float EndValue)
	{
		if (!Curve.Keys.empty())
		{
			return;
		}

		Curve.Reset();
		Curve.DefaultValue = EndValue;
		Curve.AddKey(0.0f, StartValue);
		Curve.AddKey(1.0f, EndValue);
		Curve.SortKeys();
		Curve.AutoSetTangents();
	}

	void SetFloatDistributionMode(FParticleDistributionFloat& Distribution, EParticleDistributionMode Mode)
	{
		if (Distribution.Mode == Mode)
		{
			return;
		}

		const float StartValue = Distribution.Evaluate(0.0f);
		const float EndValue = Distribution.Evaluate(1.0f);
		const float MinValue = (std::min)(Distribution.Min, Distribution.Max);
		const float MaxValue = (std::max)(Distribution.Min, Distribution.Max);

		switch (Mode)
		{
		case EParticleDistributionMode::Constant:
			Distribution.SetConstant(EndValue);
			break;
		case EParticleDistributionMode::Uniform:
			Distribution.SetUniform(MinValue, MaxValue);
			break;
		case EParticleDistributionMode::ConstantCurve:
			Distribution.SetConstantCurve(0.0f, StartValue, 1.0f, EndValue);
			break;
		case EParticleDistributionMode::UniformCurve:
			Distribution.SetUniformCurve(0.0f, MinValue, MaxValue, 1.0f, MinValue, MaxValue);
			break;
		default:
			break;
		}
	}

	bool RenderFloatDistributionControls(const char* Label, FParticleDistributionFloat& Distribution, float Speed, float MinValue, float MaxValue)
	{
		bool bChanged = false;
		ImGui::PushID(Label);
		if (ImGui::TreeNode(Label))
		{
			int Mode = static_cast<int>(Distribution.Mode);
			if (ImGui::Combo("Distribution", &Mode, GDistributionModeNames, IM_ARRAYSIZE(GDistributionModeNames)))
			{
				Mode = std::clamp(Mode, 0, static_cast<int>(EParticleDistributionMode::UniformCurve));
				SetFloatDistributionMode(Distribution, static_cast<EParticleDistributionMode>(Mode));
				bChanged = true;
			}

			if (Distribution.Mode == EParticleDistributionMode::Constant)
			{
				float Value = Distribution.Constant;
				if (ImGui::DragFloat("Constant", &Value, Speed, MinValue, MaxValue))
				{
					Distribution.SetConstant(Value);
					bChanged = true;
				}
			}
			else if (Distribution.Mode == EParticleDistributionMode::Uniform)
			{
				float Range[2] = { Distribution.Min, Distribution.Max };
				if (ImGui::DragFloat2("Uniform Min/Max", Range, Speed, MinValue, MaxValue))
				{
					Distribution.SetUniform(Range[0], Range[1]);
					bChanged = true;
				}
			}
			else
			{
				ImGui::TextDisabled("Edit keys in the Curve Editor below.");
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
		return bChanged;
	}

	bool RenderVectorDistributionControls(const char* Label, FParticleDistributionVector& Distribution, float Speed, float MinValue, float MaxValue)
	{
		bool bChanged = false;
		ImGui::PushID(Label);
		if (ImGui::TreeNode(Label))
		{
			bChanged |= RenderFloatDistributionControls("X", Distribution.X, Speed, MinValue, MaxValue);
			bChanged |= RenderFloatDistributionControls("Y", Distribution.Y, Speed, MinValue, MaxValue);
			bChanged |= RenderFloatDistributionControls("Z", Distribution.Z, Speed, MinValue, MaxValue);
			ImGui::TreePop();
		}
		ImGui::PopID();
		return bChanged;
	}

	void AddFloatDistributionTracks(std::vector<FCurveEditorTrack>& Tracks, const char* Label, FParticleDistributionFloat& Distribution, ImU32 Color)
	{
		if (Distribution.Mode == EParticleDistributionMode::Uniform || Distribution.Mode == EParticleDistributionMode::UniformCurve)
		{
			Tracks.push_back({ std::string(Label) + " Min", &Distribution, EDistributionTrackRole::Min, Color });
			Tracks.push_back({ std::string(Label) + " Max", &Distribution, EDistributionTrackRole::Max, IM_COL32(255, 220, 80, 255) });
			return;
		}

		Tracks.push_back({ Label, &Distribution, EDistributionTrackRole::Constant, Color });
	}

	float EvaluateTrackValue(const FCurveEditorTrack& Track, float Time)
	{
		if (!Track.Distribution)
		{
			return 0.0f;
		}

		const FParticleDistributionFloat& Distribution = *Track.Distribution;
		if (Distribution.Mode == EParticleDistributionMode::Uniform || Distribution.Mode == EParticleDistributionMode::UniformCurve)
		{
			const bool bMaxTrack = Track.Role == EDistributionTrackRole::Max;
			const FFloatCurve* Curve = Distribution.GetCurve(bMaxTrack);
			if (Distribution.Mode == EParticleDistributionMode::UniformCurve && Curve)
			{
				return Curve->Evaluate(Time);
			}
			return bMaxTrack ? Distribution.Max : Distribution.Min;
		}

		if (Distribution.Mode == EParticleDistributionMode::ConstantCurve)
		{
			return Distribution.ConstantCurve.Evaluate(Time);
		}
		return Distribution.Constant;
	}

	void SetTrackConstantValue(FCurveEditorTrack& Track, float Value)
	{
		if (!Track.Distribution)
		{
			return;
		}

		FParticleDistributionFloat& Distribution = *Track.Distribution;
		if (Distribution.Mode == EParticleDistributionMode::Uniform)
		{
			if (Track.Role == EDistributionTrackRole::Max)
			{
				Distribution.SetUniform(Distribution.Min, Value);
			}
			else
			{
				Distribution.SetUniform(Value, Distribution.Max);
			}
			return;
		}

		if (Distribution.Mode == EParticleDistributionMode::Constant)
		{
			Distribution.SetConstant(Value);
		}
	}

	int32 FindNearestCurveKeyIndex(const FFloatCurve& Curve, float Time, float Value)
	{
		int32 BestIndex = -1;
		float BestDistance = FLT_MAX;
		for (int32 KeyIndex = 0; KeyIndex < static_cast<int32>(Curve.Keys.size()); ++KeyIndex)
		{
			const FCurveKey& Key = Curve.Keys[KeyIndex];
			const float TimeDelta = Key.Time - Time;
			const float ValueDelta = Key.Value - Value;
			const float Distance = TimeDelta * TimeDelta + ValueDelta * ValueDelta;
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				BestIndex = KeyIndex;
			}
		}
		return BestIndex;
	}

	float CalculateNiceTickStep(float Range, float PixelSpan, float MinPixelSpacing)
	{
		if (Range <= 0.0f || PixelSpan <= 0.0f)
		{
			return 1.0f;
		}

		const float TargetTickCount = (std::max)(1.0f, PixelSpan / (std::max)(MinPixelSpacing, 1.0f));
		const float RawStep = Range / TargetTickCount;
		const float Exponent = std::floor(std::log10((std::max)(RawStep, 1e-6f)));
		const float Magnitude = std::pow(10.0f, Exponent);
		const float Normalized = RawStep / Magnitude;

		float NiceNormalized = 10.0f;
		if (Normalized <= 1.0f)
		{
			NiceNormalized = 1.0f;
		}
		else if (Normalized <= 2.0f)
		{
			NiceNormalized = 2.0f;
		}
		else if (Normalized <= 5.0f)
		{
			NiceNormalized = 5.0f;
		}

		return NiceNormalized * Magnitude;
	}

	int32 GetTickLabelPrecision(float Step)
	{
		if (Step <= 0.0f)
		{
			return 2;
		}

		const int32 Precision = static_cast<int32>(std::ceil(-std::log10(Step))) + 1;
		return std::clamp(Precision, 0, 4);
	}

	FFloatCurve* GetEditableTrackCurve(FCurveEditorTrack& Track)
	{
		if (!Track.Distribution)
		{
			return nullptr;
		}

		FParticleDistributionFloat& Distribution = *Track.Distribution;
		if (Distribution.Mode == EParticleDistributionMode::Constant)
		{
			Distribution.SetConstantCurve(0.0f, Distribution.Constant, 1.0f, Distribution.Constant);
			Track.Role = EDistributionTrackRole::Constant;
		}
		else if (Distribution.Mode == EParticleDistributionMode::Uniform)
		{
			Distribution.SetUniformCurve(0.0f, Distribution.Min, Distribution.Max, 1.0f, Distribution.Min, Distribution.Max);
		}

		if (Distribution.Mode == EParticleDistributionMode::UniformCurve)
		{
			const bool bMaxTrack = Track.Role == EDistributionTrackRole::Max;
			return Distribution.GetCurve(bMaxTrack);
		}
		return Distribution.GetCurve(false);
	}

	FFloatCurve* GetTrackCurveIfAlreadyCurve(FCurveEditorTrack& Track)
	{
		if (!Track.Distribution)
		{
			return nullptr;
		}

		FParticleDistributionFloat& Distribution = *Track.Distribution;
		if (Distribution.Mode == EParticleDistributionMode::UniformCurve)
		{
			return Distribution.GetCurve(Track.Role == EDistributionTrackRole::Max);
		}
		if (Distribution.Mode == EParticleDistributionMode::ConstantCurve)
		{
			return Distribution.GetCurve(false);
		}
		return nullptr;
	}

	void AddVectorDistributionTracks(std::vector<FCurveEditorTrack>& Tracks, const char* Label, FParticleDistributionVector& Distribution)
	{
		AddFloatDistributionTracks(Tracks, (std::string(Label) + " X").c_str(), Distribution.X, IM_COL32(230, 80, 80, 255));
		AddFloatDistributionTracks(Tracks, (std::string(Label) + " Y").c_str(), Distribution.Y, IM_COL32(80, 220, 120, 255));
		AddFloatDistributionTracks(Tracks, (std::string(Label) + " Z").c_str(), Distribution.Z, IM_COL32(100, 150, 255, 255));
	}

	void CollectModuleCurveTracks(UParticleModule* Module, std::vector<FCurveEditorTrack>& Tracks)
	{
		if (UParticleModuleSpawn* Spawn = Cast<UParticleModuleSpawn>(Module))
		{
			AddFloatDistributionTracks(Tracks, "Spawn Rate", Spawn->RateDistribution, IM_COL32(120, 220, 255, 255));
		}
		else if (UParticleModuleLifetime* Lifetime = Cast<UParticleModuleLifetime>(Module))
		{
			AddFloatDistributionTracks(Tracks, "Lifetime", Lifetime->LifetimeDistribution, IM_COL32(255, 180, 90, 255));
		}
		else if (UParticleModuleSize* Size = Cast<UParticleModuleSize>(Module))
		{
			AddVectorDistributionTracks(Tracks, "Start Size", Size->StartSizeDistribution);
		}
		else if (UParticleModuleVelocity* Velocity = Cast<UParticleModuleVelocity>(Module))
		{
			AddVectorDistributionTracks(Tracks, "Start Velocity", Velocity->StartVelocityDistribution);
		}
		else if (UParticleModuleLocation* Location = Cast<UParticleModuleLocation>(Module))
		{
			AddVectorDistributionTracks(Tracks, "Start Location", Location->StartLocationDistribution);
		}
		else if (UParticleModuleInitialRotation* Rotation = Cast<UParticleModuleInitialRotation>(Module))
		{
			AddVectorDistributionTracks(Tracks, "Start Rotation", Rotation->StartRotationDistribution);
		}
		else if (UParticleModuleInitialRotationRate* RotationRate = Cast<UParticleModuleInitialRotationRate>(Module))
		{
			AddVectorDistributionTracks(Tracks, "Start Rotation Rate", RotationRate->StartRotationRateDistribution);
		}
		else if (UParticleModuleAcceleration* Acceleration = Cast<UParticleModuleAcceleration>(Module))
		{
			AddVectorDistributionTracks(Tracks, "Acceleration", Acceleration->AccelerationDistribution);
		}
		else if (UParticleModuleOrbit* Orbit = Cast<UParticleModuleOrbit>(Module))
		{
			AddVectorDistributionTracks(Tracks, "Orbit Offset", Orbit->OffsetDistribution);
			AddVectorDistributionTracks(Tracks, "Orbit Rotation", Orbit->RotationDistribution);
			AddVectorDistributionTracks(Tracks, "Orbit Rotation Rate", Orbit->RotationRateDistribution);
		}
		else if (UParticleModuleColor* Color = Cast<UParticleModuleColor>(Module))
		{
			AddVectorDistributionTracks(Tracks, "Start Color", Color->StartColorDistribution);
			AddFloatDistributionTracks(Tracks, "Start Alpha", Color->StartAlphaDistribution, IM_COL32(230, 230, 230, 255));
		}
		else if (UParticleModuleColorOverLife* ColorOverLife = Cast<UParticleModuleColorOverLife>(Module))
		{
			AddVectorDistributionTracks(Tracks, "Color Over Life", ColorOverLife->ColorOverLifeDistribution);
			AddFloatDistributionTracks(Tracks, "Alpha Over Life", ColorOverLife->AlphaOverLifeDistribution, IM_COL32(230, 230, 230, 255));
		}
		else if (UParticleModuleColorScaleOverLife* ColorScale = Cast<UParticleModuleColorScaleOverLife>(Module))
		{
			AddVectorDistributionTracks(Tracks, "Color Scale", ColorScale->ColorScaleOverLifeDistribution);
			AddFloatDistributionTracks(Tracks, "Alpha Scale", ColorScale->AlphaScaleOverLifeDistribution, IM_COL32(230, 230, 230, 255));
		}
	}

	void SyncModuleLegacyFromDistributions(UParticleModule* Module)
	{
		if (UParticleModuleSpawn* Spawn = Cast<UParticleModuleSpawn>(Module))
		{
			Spawn->Rate = (std::max)(0.0f, Spawn->RateDistribution.GetMaxValue());
		}
		else if (UParticleModuleLifetime* Lifetime = Cast<UParticleModuleLifetime>(Module))
		{
			Lifetime->LifetimeMin = Lifetime->LifetimeDistribution.Evaluate(0.0f);
			Lifetime->LifetimeMax = Lifetime->LifetimeDistribution.GetMaxValue();
			Lifetime->Lifetime = Lifetime->LifetimeMax;
		}
		else if (UParticleModuleSize* Size = Cast<UParticleModuleSize>(Module))
		{
			Size->StartSizeMin = Size->StartSizeDistribution.Evaluate(0.0f);
			Size->StartSizeMax = Size->StartSizeDistribution.GetMaxValue();
			Size->StartSize = Size->StartSizeMax;
		}
		else if (UParticleModuleVelocity* Velocity = Cast<UParticleModuleVelocity>(Module))
		{
			Velocity->StartVelocityMin = Velocity->StartVelocityDistribution.Evaluate(0.0f);
			Velocity->StartVelocityMax = Velocity->StartVelocityDistribution.GetMaxValue();
			Velocity->StartVelocity = Velocity->StartVelocityMax;
		}
		else if (UParticleModuleLocation* Location = Cast<UParticleModuleLocation>(Module))
		{
			Location->StartLocationMin = Location->StartLocationDistribution.Evaluate(0.0f);
			Location->StartLocationMax = Location->StartLocationDistribution.GetMaxValue();
			Location->StartLocation = Location->StartLocationMax;
		}
		else if (UParticleModuleInitialRotation* Rotation = Cast<UParticleModuleInitialRotation>(Module))
		{
			Rotation->StartRotationDegreesMin = Rotation->StartRotationDistribution.Evaluate(0.0f);
			Rotation->StartRotationDegreesMax = Rotation->StartRotationDistribution.GetMaxValue();
			Rotation->StartRotationDegrees = Rotation->StartRotationDegreesMax;
		}
		else if (UParticleModuleInitialRotationRate* RotationRate = Cast<UParticleModuleInitialRotationRate>(Module))
		{
			RotationRate->StartRotationRateDegreesMin = RotationRate->StartRotationRateDistribution.Evaluate(0.0f);
			RotationRate->StartRotationRateDegreesMax = RotationRate->StartRotationRateDistribution.GetMaxValue();
			RotationRate->StartRotationRateDegrees = RotationRate->StartRotationRateDegreesMax;
		}
		else if (UParticleModuleAcceleration* Acceleration = Cast<UParticleModuleAcceleration>(Module))
		{
			Acceleration->Acceleration = Acceleration->AccelerationDistribution.Evaluate(1.0f);
		}
		else if (UParticleModuleOrbit* Orbit = Cast<UParticleModuleOrbit>(Module))
		{
			Orbit->Offset = Orbit->OffsetDistribution.Evaluate(0.0f);
			Orbit->RotationDegrees = Orbit->RotationDistribution.Evaluate(0.0f);
			Orbit->RotationRateDegrees = Orbit->RotationRateDistribution.Evaluate(0.0f);
		}
		else if (UParticleModuleColor* Color = Cast<UParticleModuleColor>(Module))
		{
			Color->StartColorMin = Color->StartColorDistribution.Evaluate(0.0f);
			Color->StartColorMax = Color->StartColorDistribution.GetMaxValue();
			Color->StartColor = Color->StartColorMax;
			Color->StartAlphaMin = Color->StartAlphaDistribution.Evaluate(0.0f);
			Color->StartAlphaMax = Color->StartAlphaDistribution.GetMaxValue();
			Color->StartAlpha = Color->StartAlphaMax;
		}
		else if (UParticleModuleColorOverLife* ColorOverLife = Cast<UParticleModuleColorOverLife>(Module))
		{
			ColorOverLife->ColorOverLife = ColorOverLife->ColorOverLifeDistribution.Evaluate(1.0f);
			ColorOverLife->AlphaOverLife = std::clamp(ColorOverLife->AlphaOverLifeDistribution.Evaluate(1.0f), 0.0f, 1.0f);
		}
		else if (UParticleModuleColorScaleOverLife* ColorScale = Cast<UParticleModuleColorScaleOverLife>(Module))
		{
			ColorScale->ColorScaleOverLife = ColorScale->ColorScaleOverLifeDistribution.Evaluate(1.0f);
			ColorScale->AlphaScaleOverLife = (std::max)(0.0f, ColorScale->AlphaScaleOverLifeDistribution.Evaluate(1.0f));
		}
	}

	void DestroyEmitterTree(UParticleEmitter* Emitter)
	{
		if (!Emitter)
		{
			return;
		}

		for (UParticleLODLevel* LOD : Emitter->LODLevels)
		{
			if (!LOD)
			{
				continue;
			}

			for (UParticleModule* Module : LOD->Modules)
			{
				if (Module)
				{
					if (Module == LOD->TypeDataModule)
					{
						LOD->TypeDataModule = nullptr;
					}
					GUObjectArray.DestroyObject(Module);
				}
			}
			LOD->Modules.clear();

			if (LOD->RequiredModule)
			{
				GUObjectArray.DestroyObject(LOD->RequiredModule);
				LOD->RequiredModule = nullptr;
			}
			if (LOD->SpawnModule)
			{
				GUObjectArray.DestroyObject(LOD->SpawnModule);
				LOD->SpawnModule = nullptr;
			}
			if (LOD->TypeDataModule)
			{
				GUObjectArray.DestroyObject(LOD->TypeDataModule);
				LOD->TypeDataModule = nullptr;
			}

			GUObjectArray.DestroyObject(LOD);
		}
		Emitter->LODLevels.clear();

		GUObjectArray.DestroyObject(Emitter);
	}

	template <typename TModule>
	TModule* FindEnabledBeamModule(UParticleLODLevel* LOD)
	{
		if (!LOD)
		{
			return nullptr;
		}

		for (UParticleModule* Module : LOD->Modules)
		{
			TModule* TypedModule = Cast<TModule>(Module);
			if (TypedModule && TypedModule->bEnabled)
			{
				return TypedModule;
			}
		}
		return nullptr;
	}
}

FParticleEditorWidget::FParticleEditorWidget()
	: InstanceId(GNextParticleEditorInstanceId++)
{
	const FString Id = std::to_string(InstanceId);
	PreviewWorldHandle = FName("ParticleEditorPreview_" + Id);
	WindowIdSuffix = "###ParticleEditor_" + Id;
}

bool FParticleEditorWidget::CanEdit(UObject* Object) const
{
	return Object && Object->IsA<UParticleSystem>();
}

bool FParticleEditorWidget::IsEditingObject(UObject* Object) const
{
	return FAssetEditorWidget::IsEditingObject(Object);
}

void FParticleEditorWidget::Open(UObject* Object)
{
	FAssetEditorWidget::Open(Object);
	if (!IsOpen())
	{
		return;
	}

	EditingParticleSystem = Cast<UParticleSystem>(EditedObject);
	if (!EditingParticleSystem)
	{
		Close();
		return;
	}

	SelectedEmitterIndex = 0;
	SelectedLODIndex = 0;
	SelectedModule = nullptr;
	bParticleSystemSelected = false;
	SyncAssetNameBuffer();
	EnsureDefaultSystem();
	SyncEmitterNameBuffer();
	InitializePreviewWorld();
}

void FParticleEditorWidget::Close()
{
	FAssetEditorWidget::Close();
	ReleasePreviewWorld();
	EditingParticleSystem = nullptr;
	PreviewParticleComponent = nullptr;
	PreviewActor = nullptr;
	SelectedLODIndex = 0;
	SelectedModule = nullptr;
	bParticleSystemSelected = false;
	AssetNameBuffer[0] = '\0';
	EmitterNameBuffer[0] = '\0';
	EmitterNameBufferIndex = -1;
	EmitterNameBufferEmitter = nullptr;
}

void FParticleEditorWidget::Tick(float DeltaTime)
{
	if (ViewportClient.IsRenderable())
	{
		ViewportClient.Tick(DeltaTime);
	}

	if (PreviewActor)
	{
		PreviewActor->bTickInEditor = bSimulating;
	}

	if (bSimulating)
	{
		if (UWorld* PreviewWorld = ViewportClient.GetPreviewWorld())
		{
			PreviewWorld->Tick(DeltaTime, LEVELTICK_ViewportsOnly);
		}
	}
}

void FParticleEditorWidget::CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const
{
	if (IsOpen())
	{
		OutClients.push_back(const_cast<FStaticMeshEditorViewportClient*>(&ViewportClient));
	}
}

void FParticleEditorWidget::EnsureDefaultSystem()
{
	if (!EditingParticleSystem || !EditingParticleSystem->Emitters.empty())
	{
		if (EditingParticleSystem)
		{
			EditingParticleSystem->NormalizeLODData();
			SelectedLODIndex = ClampLODIndex(SelectedLODIndex);
		}
		if (!SelectedModule)
		{
			SelectedModule = GetSelectedRequiredModule();
		}
		return;
	}

	if (UParticleEmitter* Emitter = CreateDefaultEmitter("Particle Emitter"))
	{
		EditingParticleSystem->Emitters.push_back(Emitter);
		SelectedEmitterIndex = 0;
		EditingParticleSystem->NormalizeLODData();
		SelectedLODIndex = ClampLODIndex(SelectedLODIndex);
		SelectedModule = GetSelectedRequiredModule();
		bParticleSystemSelected = false;
	}
}

UParticleEmitter* FParticleEditorWidget::CreateDefaultEmitter(const FString& EmitterName)
{
	if (!EditingParticleSystem)
	{
		return nullptr;
	}

	UParticleSpriteEmitter* Emitter = GUObjectArray.CreateObject<UParticleSpriteEmitter>(EditingParticleSystem);
	Emitter->SetEmitterName(FName(EmitterName));
	Emitter->InitialAllocationCount = 128;
	Emitter->PeakActiveParticles = 128;

	UParticleLODLevel* LOD = GUObjectArray.CreateObject<UParticleLODLevel>(Emitter);
	LOD->bEnabled = true;
	LOD->SetLevelIndex(0);

	UParticleModuleRequired* Required = GUObjectArray.CreateObject<UParticleModuleRequired>(LOD);
	Required->Material = FMaterialManager::Get().GetOrCreateMaterial("Asset/Particle/Materials/M_Fire_B.mat");
	Required->ScreenAlignment = PSA_FacingCameraPosition;
	Required->SubImages_Horizontal = 1;
	Required->SubImages_Vertical = 1;
	Required->AlphaSource = 1;
	Required->AlphaThreshold = 0.08f;
	Required->AlphaPower = 1.0f;
	Required->ColorIntensity = 1.6f;
	LOD->RequiredModule = Required;

	UParticleModuleSpawn* Spawn = GUObjectArray.CreateObject<UParticleModuleSpawn>(LOD);
	Spawn->Rate = 20.0f;
	Spawn->RateDistribution.SetConstant(Spawn->Rate);
	LOD->SpawnModule = Spawn;

	if (UParticleModule* Lifetime = CreateModule(EAddableModuleType::Lifetime, LOD))
	{
		LOD->Modules.push_back(Lifetime);
	}
	if (UParticleModule* Size = CreateModule(EAddableModuleType::Size, LOD))
	{
		LOD->Modules.push_back(Size);
	}
	if (UParticleModule* Velocity = CreateModule(EAddableModuleType::Velocity, LOD))
	{
		LOD->Modules.push_back(Velocity);
	}
	if (UParticleModule* Location = CreateModule(EAddableModuleType::Location, LOD))
	{
		LOD->Modules.push_back(Location);
	}
	if (UParticleModule* Color = CreateModule(EAddableModuleType::Color, LOD))
	{
		LOD->Modules.push_back(Color);
	}
	if (UParticleModule* ColorOverLife = CreateModule(EAddableModuleType::ColorOverLife, LOD))
	{
		LOD->Modules.push_back(ColorOverLife);
	}

	LOD->ClassifyModulesByRole();
	Emitter->LODLevels.push_back(LOD);
	Emitter->ClassifyModulesByRole();
	return Emitter;
}

UParticleModule* FParticleEditorWidget::CreateModule(EAddableModuleType ModuleType, UObject* Outer)
{
	if (!Outer)
	{
		return nullptr;
	}

	switch (ModuleType)
	{
	case EAddableModuleType::Lifetime:
	{
		UParticleModuleLifetime* Lifetime = GUObjectArray.CreateObject<UParticleModuleLifetime>(Outer);
		Lifetime->bEnabled = true;
		Lifetime->Lifetime = 1.0f;
		Lifetime->LifetimeMin = Lifetime->Lifetime;
		Lifetime->LifetimeMax = Lifetime->Lifetime;
		Lifetime->LifetimeDistribution.SetUniform(Lifetime->LifetimeMin, Lifetime->LifetimeMax);
		return Lifetime;
	}
	case EAddableModuleType::Size:
	{
		UParticleModuleSize* Size = GUObjectArray.CreateObject<UParticleModuleSize>(Outer);
		Size->bEnabled = true;
		Size->StartSize = FVector(12.0f, 12.0f, 1.0f);
		Size->StartSizeMin = Size->StartSize;
		Size->StartSizeMax = Size->StartSize;
		Size->StartSizeDistribution.SetUniform(Size->StartSizeMin, Size->StartSizeMax);
		return Size;
	}
	case EAddableModuleType::Velocity:
	{
		UParticleModuleVelocity* Velocity = GUObjectArray.CreateObject<UParticleModuleVelocity>(Outer);
		Velocity->bEnabled = true;
		Velocity->StartVelocity = FVector(0.0f, 0.0f, 35.0f);
		Velocity->StartVelocityMin = Velocity->StartVelocity;
		Velocity->StartVelocityMax = Velocity->StartVelocity;
		Velocity->StartVelocityDistribution.SetUniform(Velocity->StartVelocityMin, Velocity->StartVelocityMax);
		return Velocity;
	}
	case EAddableModuleType::InitialRotation:
	{
		UParticleModuleInitialRotation* Rotation = GUObjectArray.CreateObject<UParticleModuleInitialRotation>(Outer);
		Rotation->bEnabled = true;
		Rotation->StartRotationDegrees = FVector(0.0f, 0.0f, 360.0f);
		Rotation->StartRotationDegreesMin = FVector::ZeroVector;
		Rotation->StartRotationDegreesMax = FVector(0.0f, 0.0f, 360.0f);
		Rotation->StartRotationDistribution.SetUniform(Rotation->StartRotationDegreesMin, Rotation->StartRotationDegreesMax);
		return Rotation;
	}
	case EAddableModuleType::InitialRotationRate:
	{
		UParticleModuleInitialRotationRate* RotationRate = GUObjectArray.CreateObject<UParticleModuleInitialRotationRate>(Outer);
		RotationRate->bEnabled = true;
		RotationRate->StartRotationRateDegrees = FVector(0.0f, 0.0f, 90.0f);
		RotationRate->StartRotationRateDegreesMin = FVector(0.0f, 0.0f, -90.0f);
		RotationRate->StartRotationRateDegreesMax = FVector(0.0f, 0.0f, 90.0f);
		RotationRate->StartRotationRateDistribution.SetUniform(RotationRate->StartRotationRateDegreesMin, RotationRate->StartRotationRateDegreesMax);
		return RotationRate;
	}
	case EAddableModuleType::Acceleration:
	{
		UParticleModuleAcceleration* Acceleration = GUObjectArray.CreateObject<UParticleModuleAcceleration>(Outer);
		Acceleration->bEnabled = true;
		Acceleration->Acceleration = FVector(0.0f, 0.0f, -35.0f);
		Acceleration->AccelerationDistribution.SetConstant(Acceleration->Acceleration);
		return Acceleration;
	}
	case EAddableModuleType::Orbit:
	{
		UParticleModuleOrbit* Orbit = GUObjectArray.CreateObject<UParticleModuleOrbit>(Outer);
		Orbit->bEnabled = true;
		Orbit->Offset = FVector(50.0f, 0.0f, 0.0f);
		Orbit->RotationDegrees = FVector::ZeroVector;
		Orbit->RotationRateDegrees = FVector(0.0f, 0.0f, 90.0f);
		Orbit->OffsetDistribution.SetConstant(Orbit->Offset);
		Orbit->RotationDistribution.SetConstant(Orbit->RotationDegrees);
		Orbit->RotationRateDistribution.SetConstant(Orbit->RotationRateDegrees);
		return Orbit;
	}
	case EAddableModuleType::Location:
	{
		UParticleModuleLocation* Location = GUObjectArray.CreateObject<UParticleModuleLocation>(Outer);
		Location->bEnabled = true;
		Location->StartLocation = FVector::ZeroVector;
		Location->StartLocationMin = Location->StartLocation;
		Location->StartLocationMax = Location->StartLocation;
		Location->StartLocationDistribution.SetUniform(Location->StartLocationMin, Location->StartLocationMax);
		return Location;
	}
	case EAddableModuleType::Color:
	{
		UParticleModuleColor* Color = GUObjectArray.CreateObject<UParticleModuleColor>(Outer);
		Color->bEnabled = true;
		Color->StartColor = FVector(1.0f, 1.0f, 1.0f);
		Color->StartColorMin = Color->StartColor;
		Color->StartColorMax = Color->StartColor;
		Color->StartAlpha = 1.0f;
		Color->StartAlphaMin = Color->StartAlpha;
		Color->StartAlphaMax = Color->StartAlpha;
		Color->StartColorDistribution.SetUniform(Color->StartColorMin, Color->StartColorMax);
		Color->StartAlphaDistribution.SetUniform(Color->StartAlphaMin, Color->StartAlphaMax);
		return Color;
	}
	case EAddableModuleType::ColorOverLife:
	{
		UParticleModuleColorOverLife* ColorOverLife = GUObjectArray.CreateObject<UParticleModuleColorOverLife>(Outer);
		ColorOverLife->bEnabled = true;
		ColorOverLife->ColorOverLife = FVector(1.0f, 1.0f, 1.0f);
		ColorOverLife->AlphaOverLife = 0.0f;
		ColorOverLife->ColorOverLifeDistribution.SetConstant(ColorOverLife->ColorOverLife);
		ColorOverLife->AlphaOverLifeDistribution.SetConstant(ColorOverLife->AlphaOverLife);
		return ColorOverLife;
	}
	case EAddableModuleType::ColorScaleOverLife:
	{
		UParticleModuleColorScaleOverLife* ColorScale = GUObjectArray.CreateObject<UParticleModuleColorScaleOverLife>(Outer);
		ColorScale->bEnabled = true;
		ColorScale->ColorScaleOverLife = FVector(1.0f, 1.0f, 1.0f);
		ColorScale->AlphaScaleOverLife = 1.0f;
		ColorScale->ColorScaleOverLifeDistribution.SetConstant(ColorScale->ColorScaleOverLife);
		ColorScale->AlphaScaleOverLifeDistribution.SetConstant(ColorScale->AlphaScaleOverLife);
		return ColorScale;
	}
	case EAddableModuleType::BeamSource:
	{
		UParticleModuleBeamSource* Source = GUObjectArray.CreateObject<UParticleModuleBeamSource>(Outer);
		Source->bEnabled = true;
		Source->SourceMethod = PEB2STM_UserSet;
		Source->Source = FVector(-200.0f, 0.0f, 0.0f);
		Source->SourceTangent = FVector(0.0f, 0.0f, 40.0f);
		return Source;
	}
	case EAddableModuleType::BeamTarget:
	{
		UParticleModuleBeamTarget* Target = GUObjectArray.CreateObject<UParticleModuleBeamTarget>(Outer);
		Target->bEnabled = true;
		Target->TargetMethod = PEB2STM_UserSet;
		Target->Target = FVector(200.0f, 0.0f, 0.0f);
		Target->TargetTangent = FVector(0.0f, 0.0f, -40.0f);
		return Target;
	}
	case EAddableModuleType::BeamNoise:
	{
		UParticleModuleBeamNoise* Noise = GUObjectArray.CreateObject<UParticleModuleBeamNoise>(Outer);
		Noise->bEnabled = true;
		Noise->Frequency = 10;
		Noise->FrequencyDistance = 0.0f;
		Noise->NoiseRange = FVector(0.0f, 30.0f, 30.0f);
		Noise->NoiseSpeed = 10.0f;
		Noise->NoiseLockTime = 0.0f;
		Noise->bTargetNoise = false;
		return Noise;
	}
	case EAddableModuleType::Collision:
	{
		UParticleModuleCollision* Collision = GUObjectArray.CreateObject<UParticleModuleCollision>(Outer);
		Collision->bEnabled = true;
		return Collision;
	}
	case EAddableModuleType::EventGenerator:
	{
		UParticleModuleEventGenerator* EventGenerator = GUObjectArray.CreateObject<UParticleModuleEventGenerator>(Outer);
		EventGenerator->bEnabled = true;
		FParticleEvent_GenerateInfo CollisionEvent;
		CollisionEvent.Type = EPET_Collision;
		CollisionEvent.CustomName = FName("Collision");
		EventGenerator->Events.push_back(CollisionEvent);
		return EventGenerator;
	}
	case EAddableModuleType::EventReceiverSpawn:
	{
		UParticleModuleEventReceiverSpawn* EventReceiver = GUObjectArray.CreateObject<UParticleModuleEventReceiverSpawn>(Outer);
		EventReceiver->bEnabled = true;
		EventReceiver->EventGeneratorType = EPET_Collision;
		EventReceiver->EventName = FName("Collision");
		EventReceiver->SpawnCount = 1;
		EventReceiver->bSpawnOnlyOnEvent = true;
		return EventReceiver;
	}
	}

	return nullptr;
}

UParticleModule* FParticleEditorWidget::CreateTypeDataModule(EEmitterTypeData TypeData, UObject* Outer)
{
	if (!Outer)
	{
		return nullptr;
	}

	switch (TypeData)
	{
	case EEmitterTypeData::Sprite:
		return nullptr;
	case EEmitterTypeData::Mesh:
	{
		UParticleModuleTypeDataMesh* Mesh = GUObjectArray.CreateObject<UParticleModuleTypeDataMesh>(Outer);
		Mesh->bEnabled = true;
		Mesh->MeshPath = DefaultParticleMeshPath;
		Mesh->Mesh = LoadEditorStaticMesh(Mesh->MeshPath);
		return Mesh;
	}
	case EEmitterTypeData::Beam:
	{
		UParticleModuleTypeDataBeam2* Beam = GUObjectArray.CreateObject<UParticleModuleTypeDataBeam2>(Outer);
		Beam->bEnabled = true;
		Beam->BeamMethod = PEB2M_Target;
		Beam->Speed = 0.0f;
		Beam->InterpolationPoints = 20;
		Beam->Sheets = 5;
		Beam->MaxBeamCount = 5;
		Beam->SourcePoint = FVector(-200.0f, 0.0f, 0.0f);
		Beam->TargetPoint = FVector(200.0f, 0.0f, 0.0f);
		return Beam;
	}
	case EEmitterTypeData::Ribbon:
	{
		UParticleModuleTypeDataRibbon* Ribbon = GUObjectArray.CreateObject<UParticleModuleTypeDataRibbon>(Outer);
		Ribbon->bEnabled = true;
		Ribbon->bRenderGeometry = true;
		Ribbon->SheetsPerTrail = 1;
		Ribbon->MaxTrailCount = 1;
		Ribbon->MaxParticleInTrailCount = 64;
		Ribbon->Width = 12.0f;
		Ribbon->Color = FVector::OneVector;
		Ribbon->Alpha = 1.0f;
		return Ribbon;
	}
	}

	return nullptr;
}

void FParticleEditorWidget::AddModuleToEmitter(int32 EmitterIndex, EAddableModuleType ModuleType)
{
	if (!EditingParticleSystem || EmitterIndex < 0 || EmitterIndex >= static_cast<int32>(EditingParticleSystem->Emitters.size()))
	{
		return;
	}

	UParticleEmitter* Emitter = EditingParticleSystem->Emitters[EmitterIndex];
	UParticleLODLevel* LOD = GetSelectedLODLevel(Emitter);
	if (!LOD)
	{
		return;
	}

	UParticleModule* NewModule = CreateModule(ModuleType, LOD);
	if (!NewModule)
	{
		return;
	}

	LOD->Modules.push_back(NewModule);
	SelectedEmitterIndex = EmitterIndex;
	SelectedModule = NewModule;
	bParticleSystemSelected = false;
	ApplyEmitterEdit();
}

void FParticleEditorWidget::SetEmitterTypeData(int32 EmitterIndex, EEmitterTypeData TypeData)
{
	if (!EditingParticleSystem || EmitterIndex < 0 || EmitterIndex >= static_cast<int32>(EditingParticleSystem->Emitters.size()))
	{
		return;
	}

	UParticleEmitter* Emitter = EditingParticleSystem->Emitters[EmitterIndex];
	if (!Emitter)
	{
		return;
	}

	bool bChanged = false;
	for (UParticleLODLevel* LOD : Emitter->LODLevels)
	{
		if (!LOD)
		{
			continue;
		}

		const bool bAlreadySprite = !LOD->TypeDataModule;
		const bool bAlreadyMesh = LOD->TypeDataModule && LOD->TypeDataModule->IsA<UParticleModuleTypeDataMesh>();
		const bool bAlreadyBeam = LOD->TypeDataModule && LOD->TypeDataModule->IsA<UParticleModuleTypeDataBeam2>();
		const bool bAlreadyRibbon = LOD->TypeDataModule && LOD->TypeDataModule->IsA<UParticleModuleTypeDataRibbon>();
		if ((TypeData == EEmitterTypeData::Sprite && bAlreadySprite) ||
			(TypeData == EEmitterTypeData::Mesh && bAlreadyMesh) ||
			(TypeData == EEmitterTypeData::Beam && bAlreadyBeam) ||
			(TypeData == EEmitterTypeData::Ribbon && bAlreadyRibbon))
		{
			if (TypeData == EEmitterTypeData::Mesh)
			{
				bChanged |= EnsureParticleMeshEmitterDefaults(LOD->RequiredModule);
				bChanged |= EnsureParticleMeshSizeDefaults(LOD);
			}
			else if (TypeData == EEmitterTypeData::Ribbon)
			{
				bChanged |= EnsureParticleRibbonEmitterDefaults(LOD->RequiredModule);
			}
			continue;
		}

		for (auto It = LOD->Modules.begin(); It != LOD->Modules.end();)
		{
			UParticleModule* Module = *It;
			if (Module && Module->IsA<UParticleModuleTypeDataBase>())
			{
				if (SelectedModule == Module)
				{
					SelectedModule = nullptr;
				}
				GUObjectArray.DestroyObject(Module);
				It = LOD->Modules.erase(It);
				continue;
			}
			++It;
		}
		LOD->TypeDataModule = nullptr;

		if (UParticleModule* NewTypeData = CreateTypeDataModule(TypeData, LOD))
		{
			LOD->TypeDataModule = Cast<UParticleModuleTypeDataBase>(NewTypeData);
			LOD->Modules.push_back(NewTypeData);
		}

		if (TypeData == EEmitterTypeData::Mesh)
		{
			bChanged |= EnsureParticleMeshEmitterDefaults(LOD->RequiredModule);
			bChanged |= EnsureParticleMeshSizeDefaults(LOD);
		}
		else if (TypeData == EEmitterTypeData::Beam)
		{
			if (LOD->RequiredModule)
			{
				LOD->RequiredModule->Material = FMaterialManager::Get().GetOrCreateMaterial(
					"Asset/Particle/Materials/M_Beam.mat");
				LOD->RequiredModule->ScreenAlignment = PSA_TypeSpecific;
			}
			if (LOD->SpawnModule)
			{
				LOD->SpawnModule->Rate = 1.0f;
			}
		}
		else if (TypeData == EEmitterTypeData::Ribbon && LOD->RequiredModule)
		{
			bChanged |= EnsureParticleRibbonEmitterDefaults(LOD->RequiredModule);
		}

		bChanged = true;
	}

	if (!bChanged)
	{
		return;
	}

	SelectedEmitterIndex = EmitterIndex;
	UParticleLODLevel* SelectedLOD = GetSelectedLODLevel(Emitter);
	SelectedModule = SelectedLOD && SelectedLOD->TypeDataModule
		? static_cast<UParticleModule*>(SelectedLOD->TypeDataModule)
		: static_cast<UParticleModule*>(SelectedLOD ? SelectedLOD->RequiredModule : nullptr);
	bParticleSystemSelected = false;
	ApplyEmitterEdit();
	ResetPreviewCameraToParticleBounds();
}

void FParticleEditorWidget::MoveEmitterToIndex(int32 SourceEmitterIndex, int32 TargetInsertIndex)
{
	if (!EditingParticleSystem)
	{
		return;
	}

	const int32 EmitterCount = static_cast<int32>(EditingParticleSystem->Emitters.size());
	if (SourceEmitterIndex < 0 || SourceEmitterIndex >= EmitterCount)
	{
		return;
	}

	TargetInsertIndex = std::clamp(TargetInsertIndex, 0, EmitterCount);
	if (TargetInsertIndex == SourceEmitterIndex || TargetInsertIndex == SourceEmitterIndex + 1)
	{
		return;
	}

	UParticleEmitter* SelectedEmitter = GetSelectedEmitter();
	UParticleEmitter* MovedEmitter = EditingParticleSystem->Emitters[SourceEmitterIndex];
	EditingParticleSystem->Emitters.erase(EditingParticleSystem->Emitters.begin() + SourceEmitterIndex);
	if (SourceEmitterIndex < TargetInsertIndex)
	{
		--TargetInsertIndex;
	}
	EditingParticleSystem->Emitters.insert(EditingParticleSystem->Emitters.begin() + TargetInsertIndex, MovedEmitter);

	if (SelectedEmitter)
	{
		auto SelectedIt = std::find(EditingParticleSystem->Emitters.begin(), EditingParticleSystem->Emitters.end(), SelectedEmitter);
		if (SelectedIt != EditingParticleSystem->Emitters.end())
		{
			SelectedEmitterIndex = static_cast<int32>(std::distance(EditingParticleSystem->Emitters.begin(), SelectedIt));
		}
	}
	else
	{
		SelectedEmitterIndex = std::clamp(TargetInsertIndex, 0, static_cast<int32>(EditingParticleSystem->Emitters.size()) - 1);
	}
	bParticleSystemSelected = false;
	RestartPreviewSystem();
	MarkDirty();
}

void FParticleEditorWidget::MoveModuleToEmitterAtIndex(int32 SourceEmitterIndex, UParticleModule* Module, int32 TargetEmitterIndex, int32 TargetInsertIndex)
{
	if (!EditingParticleSystem || !Module ||
		SourceEmitterIndex < 0 || SourceEmitterIndex >= static_cast<int32>(EditingParticleSystem->Emitters.size()) ||
		TargetEmitterIndex < 0 || TargetEmitterIndex >= static_cast<int32>(EditingParticleSystem->Emitters.size()))
	{
		return;
	}

	UParticleEmitter* SourceEmitter = EditingParticleSystem->Emitters[SourceEmitterIndex];
	UParticleEmitter* TargetEmitter = EditingParticleSystem->Emitters[TargetEmitterIndex];
	UParticleLODLevel* SourceLOD = GetSelectedLODLevel(SourceEmitter);
	UParticleLODLevel* TargetLOD = GetSelectedLODLevel(TargetEmitter);
	if (!SourceEmitter || !TargetEmitter || !SourceLOD || !TargetLOD || Module->IsA<UParticleModuleTypeDataBase>())
	{
		return;
	}

	auto ModuleIt = std::find(SourceLOD->Modules.begin(), SourceLOD->Modules.end(), Module);
	if (ModuleIt == SourceLOD->Modules.end())
	{
		return;
	}

	const bool bBeamModule = Module->IsA<UParticleModuleBeamBase>();
	const bool bTargetIsBeam = TargetLOD->TypeDataModule && TargetLOD->TypeDataModule->IsA<UParticleModuleTypeDataBeam2>();
	if (bBeamModule && !bTargetIsBeam)
	{
		return;
	}

	const int32 SourceModuleIndex = static_cast<int32>(std::distance(SourceLOD->Modules.begin(), ModuleIt));
	TargetInsertIndex = std::clamp(TargetInsertIndex, 0, static_cast<int32>(TargetLOD->Modules.size()));

	if (SourceLOD == TargetLOD)
	{
		if (TargetInsertIndex == SourceModuleIndex || TargetInsertIndex == SourceModuleIndex + 1)
		{
			return;
		}

		UParticleModule* MovedModule = Module;
		SourceLOD->Modules.erase(ModuleIt);
		if (SourceModuleIndex < TargetInsertIndex)
		{
			--TargetInsertIndex;
		}
		SourceLOD->Modules.insert(SourceLOD->Modules.begin() + TargetInsertIndex, MovedModule);

		SourceEmitter->ClassifyModulesByRole();
		SelectedEmitterIndex = TargetEmitterIndex;
		SelectedModule = MovedModule;
		bParticleSystemSelected = false;
		RestartPreviewSystem();
		MarkDirty();
		return;
	}

	UParticleModule* MovedModule = Module->CloneForLOD(TargetLOD);
	if (!MovedModule)
	{
		return;
	}

	TargetLOD->Modules.insert(TargetLOD->Modules.begin() + TargetInsertIndex, MovedModule);
	SourceLOD->Modules.erase(ModuleIt);
	GUObjectArray.DestroyObject(Module);

	SourceEmitter->ClassifyModulesByRole();
	TargetEmitter->ClassifyModulesByRole();
	SelectedEmitterIndex = TargetEmitterIndex;
	SelectedModule = MovedModule;
	bParticleSystemSelected = false;
	RestartPreviewSystem();
	MarkDirty();
}

void FParticleEditorWidget::DeleteModuleFromEmitter(int32 EmitterIndex, UParticleModule* Module)
{
	if (!EditingParticleSystem || !Module || EmitterIndex < 0 || EmitterIndex >= static_cast<int32>(EditingParticleSystem->Emitters.size()))
	{
		return;
	}

	UParticleEmitter* Emitter = EditingParticleSystem->Emitters[EmitterIndex];
	UParticleLODLevel* LOD = GetSelectedLODLevel(Emitter);
	if (!LOD)
	{
		return;
	}

	auto ModuleIt = std::find(LOD->Modules.begin(), LOD->Modules.end(), Module);
	if (ModuleIt == LOD->Modules.end())
	{
		return;
	}

	LOD->Modules.erase(ModuleIt);
	if (Module == LOD->TypeDataModule)
	{
		LOD->TypeDataModule = nullptr;
	}
	if (SelectedModule == Module)
	{
		SelectedEmitterIndex = EmitterIndex;
		SelectedModule = LOD->RequiredModule;
		bParticleSystemSelected = false;
		if (!SelectedModule)
		{
			SelectedModule = LOD->SpawnModule;
		}
	}

	GUObjectArray.DestroyObject(Module);
	ApplyEmitterEdit();
}

void FParticleEditorWidget::DeleteEmitter(int32 EmitterIndex)
{
	if (!EditingParticleSystem || EmitterIndex < 0 || EmitterIndex >= static_cast<int32>(EditingParticleSystem->Emitters.size()))
	{
		return;
	}

	UParticleEmitter* EmitterToDelete = EditingParticleSystem->Emitters[EmitterIndex];
	EditingParticleSystem->Emitters.erase(EditingParticleSystem->Emitters.begin() + EmitterIndex);

	SelectedModule = nullptr;
	bParticleSystemSelected = false;
	if (EditingParticleSystem->Emitters.empty())
	{
		SelectedEmitterIndex = 0;
	}
	else
	{
		SelectedEmitterIndex = std::clamp(EmitterIndex, 0, static_cast<int32>(EditingParticleSystem->Emitters.size()) - 1);
		SelectedModule = GetSelectedRequiredModule();
	}

	DestroyEmitterTree(EmitterToDelete);
	RestartPreviewSystem();
	MarkDirty();
}

void FParticleEditorWidget::InitializePreviewWorld()
{
	if (!GEngine || !EditingParticleSystem)
	{
		return;
	}

	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();

	FWorldContext& WorldContext = GEngine->CreateWorldContext(EWorldType::EditorPreview, PreviewWorldHandle);
	WorldContext.World->SetWorldType(EWorldType::EditorPreview);
	WorldContext.World->InitWorld();

	PreviewActor = WorldContext.World->SpawnActor<AActor>();
	PreviewActor->bTickInEditor = true;
	PreviewActor->bNeedsTick = true;
	PreviewActor->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));

	PreviewParticleComponent = PreviewActor->AddComponent<UParticleSystemComponent>();
	PreviewActor->SetRootComponent(PreviewParticleComponent);
	PreviewParticleComponent->SetForcedLODLevel(SelectedLODIndex);
	PreviewParticleComponent->SetTemplate(EditingParticleSystem);

	ViewportClient.Initialize(Device, 640, 480);
	ViewportClient.SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	ViewportClient.SetPreviewWorld(WorldContext.World);
	ViewportClient.SetPreviewActor(PreviewActor);
	ViewportClient.SetPreviewMeshComponent(nullptr);
	ResetPreviewCameraToParticleBounds();

	WorldContext.World->SetEditorPOVProvider(&ViewportClient);
	FSlateApplication::Get().RegisterViewport(&ParticleViewportWindow, &ViewportClient);
}

void FParticleEditorWidget::ReleasePreviewWorld()
{
	if (UWorld* PreviewWorld = ViewportClient.GetPreviewWorld())
	{
		FScene& PreviewScene = PreviewWorld->GetScene();
		GEngine->GetRenderer().GetResources().ReleaseShadowResourcesForScene(&PreviewScene);

		if (PreviewWorldHandle.IsValid())
		{
			GEngine->DestroyWorldContext(PreviewWorldHandle);
		}
	}

	FSlateApplication::Get().UnregisterViewport(&ViewportClient);
	ViewportClient.Release();
}

void FParticleEditorWidget::RestartPreviewSystem()
{
	if (!PreviewParticleComponent)
	{
		return;
	}

	PreviewParticleComponent->SetForcedLODLevel(SelectedLODIndex);
	PreviewParticleComponent->ResetParticles(true);
	PreviewParticleComponent->InitializeSystem();
}

FBoundingBox FParticleEditorWidget::CalculatePreviewBounds() const
{
	FBoundingBox Bounds;
	if (!EditingParticleSystem)
	{
		return FBoundingBox(FVector(-0.5f, -0.5f, -0.5f), FVector(0.5f, 0.5f, 0.5f));
	}

	auto ExpandWithPadding = [&Bounds](const FVector& Point, float Padding)
	{
		const FVector Pad(Padding, Padding, Padding);
		Bounds.Expand(Point - Pad);
		Bounds.Expand(Point + Pad);
	};

	for (UParticleEmitter* Emitter : EditingParticleSystem->Emitters)
	{
		UParticleLODLevel* LOD = GetSelectedLODLevel(Emitter);
		if (!LOD)
		{
			continue;
		}

		if (UParticleModuleTypeDataBeam2* Beam = Cast<UParticleModuleTypeDataBeam2>(LOD->TypeDataModule))
		{
			FVector Source = Beam->SourcePoint;
			FVector Target = Beam->TargetPoint;
			if (UParticleModuleBeamSource* SourceModule = FindEnabledBeamModule<UParticleModuleBeamSource>(LOD))
			{
				Source = SourceModule->Source;
			}
			if (Beam->BeamMethod == PEB2M_Distance)
			{
				Target = Source + FVector(Beam->Distance, 0.0f, 0.0f);
			}
			if (UParticleModuleBeamTarget* TargetModule = FindEnabledBeamModule<UParticleModuleBeamTarget>(LOD))
			{
				Target = TargetModule->Target;
			}

			const float Padding = (std::max)(Beam->Width, 4.0f);
			ExpandWithPadding(Source, Padding);
			ExpandWithPadding(Target, Padding);
			continue;
		}

		FVector MinLocation = FVector::ZeroVector;
		FVector MaxLocation = FVector::ZeroVector;
		float MaxSize = 16.0f;
		for (UParticleModule* Module : LOD->Modules)
		{
			if (UParticleModuleLocation* Location = Cast<UParticleModuleLocation>(Module))
			{
				MinLocation = Location->StartLocationMin;
				MaxLocation = Location->StartLocationMax;
			}
			else if (UParticleModuleSize* Size = Cast<UParticleModuleSize>(Module))
			{
				MaxSize = (std::max)(MaxSize, Size->StartSizeMax.X);
				MaxSize = (std::max)(MaxSize, Size->StartSizeMax.Y);
				MaxSize = (std::max)(MaxSize, Size->StartSizeMax.Z);
			}
		}

		ExpandWithPadding(MinLocation, MaxSize);
		ExpandWithPadding(MaxLocation, MaxSize);
	}

	if (!Bounds.IsValid())
	{
		return FBoundingBox(FVector(-16.0f, -16.0f, -16.0f), FVector(16.0f, 16.0f, 16.0f));
	}
	return Bounds;
}

void FParticleEditorWidget::ResetPreviewCameraToParticleBounds()
{
	ViewportClient.SetPreviewBoundsOverride(CalculatePreviewBounds());
	ViewportClient.ResetCameraToPreviewBounds();
}

void FParticleEditorWidget::ApplyEmitterEdit()
{
	if (UParticleEmitter* Emitter = GetSelectedEmitter())
	{
		Emitter->ClassifyModulesByRole();
	}

	RestartPreviewSystem();
	MarkDirty();
}

void FParticleEditorWidget::SyncAssetNameBuffer()
{
	const FString Name = EditingParticleSystem ? EditingParticleSystem->GetName() : FString();
	std::snprintf(AssetNameBuffer, sizeof(AssetNameBuffer), "%s", Name.c_str());
}

void FParticleEditorWidget::CommitAssetNameEdit()
{
	if (!EditingParticleSystem)
	{
		return;
	}

	FString NewName = AssetNameBuffer;
	const size_t First = NewName.find_first_not_of(" \t\r\n");
	if (First == FString::npos)
	{
		SyncAssetNameBuffer();
		return;
	}

	const size_t Last = NewName.find_last_not_of(" \t\r\n");
	NewName = NewName.substr(First, Last - First + 1);

	if (!IsValidAssetFileStem(NewName))
	{
		SyncAssetNameBuffer();
		return;
	}

	if (NewName == EditingParticleSystem->GetName())
	{
		SyncAssetNameBuffer();
		return;
	}

	if (FParticleSystemManager::Get().Rename(EditingParticleSystem, NewName))
	{
		SyncAssetNameBuffer();
		ClearDirty();
		if (EditorEngine)
		{
			EditorEngine->RefreshContentBrowser();
		}
	}
	else
	{
		SyncAssetNameBuffer();
	}
}

void FParticleEditorWidget::SyncEmitterNameBuffer()
{
	if (!EditingParticleSystem || EditingParticleSystem->Emitters.empty())
	{
		EmitterNameBuffer[0] = '\0';
		EmitterNameBufferIndex = -1;
		EmitterNameBufferEmitter = nullptr;
		return;
	}

	const int32 ClampedIndex = std::clamp(SelectedEmitterIndex, 0,
		static_cast<int32>(EditingParticleSystem->Emitters.size()) - 1);
	UParticleEmitter* Emitter = EditingParticleSystem->Emitters[ClampedIndex];
	const FString Name = Emitter ? GetEmitterDisplayName(Emitter, ClampedIndex) : FString();
	std::snprintf(EmitterNameBuffer, sizeof(EmitterNameBuffer), "%s", Name.c_str());
	EmitterNameBufferIndex = ClampedIndex;
	EmitterNameBufferEmitter = Emitter;
}

bool FParticleEditorWidget::IsEmitterNameAvailable(const FString& Name, int32 IgnoreEmitterIndex) const
{
	if (!EditingParticleSystem)
	{
		return false;
	}

	for (int32 Index = 0; Index < static_cast<int32>(EditingParticleSystem->Emitters.size()); ++Index)
	{
		if (Index == IgnoreEmitterIndex)
		{
			continue;
		}

		UParticleEmitter* Emitter = EditingParticleSystem->Emitters[Index];
		if (Emitter && Emitter->GetEmitterName().ToString() == Name)
		{
			return false;
		}
	}
	return true;
}

void FParticleEditorWidget::UpdateEmitterNameReferences(const FName& OldName, const FName& NewName)
{
	if (!EditingParticleSystem || !OldName.IsValid() || OldName == FName::None || OldName == NewName)
	{
		return;
	}

	auto RenameIfMatched = [&](FName& Name)
	{
		if (Name == OldName)
		{
			Name = NewName;
		}
	};

	for (UParticleEmitter* Emitter : EditingParticleSystem->Emitters)
	{
		if (!Emitter)
		{
			continue;
		}

		for (UParticleLODLevel* LOD : Emitter->LODLevels)
		{
			if (!LOD)
			{
				continue;
			}

			if (UParticleModuleTypeDataRibbon* Ribbon = Cast<UParticleModuleTypeDataRibbon>(LOD->TypeDataModule))
			{
				RenameIfMatched(Ribbon->SourceEmitterName);
			}
			else if (UParticleModuleTypeDataBeam2* Beam = Cast<UParticleModuleTypeDataBeam2>(LOD->TypeDataModule))
			{
				RenameIfMatched(Beam->BranchParentName);
				for (FBeamTargetData& Target : Beam->TargetData)
				{
					RenameIfMatched(Target.TargetName);
				}
			}

			for (UParticleModule* Module : LOD->Modules)
			{
				if (UParticleModuleBeamSource* BeamSource = Cast<UParticleModuleBeamSource>(Module))
				{
					RenameIfMatched(BeamSource->SourceName);
				}
				else if (UParticleModuleBeamTarget* BeamTarget = Cast<UParticleModuleBeamTarget>(Module))
				{
					RenameIfMatched(BeamTarget->TargetName);
				}
			}
		}
	}
}

bool FParticleEditorWidget::CommitEmitterNameEdit()
{
	if (!EditingParticleSystem || EditingParticleSystem->Emitters.empty())
	{
		SyncEmitterNameBuffer();
		return false;
	}

	const int32 ClampedIndex = std::clamp(SelectedEmitterIndex, 0,
		static_cast<int32>(EditingParticleSystem->Emitters.size()) - 1);
	UParticleEmitter* Emitter = EditingParticleSystem->Emitters[ClampedIndex];
	if (!Emitter)
	{
		SyncEmitterNameBuffer();
		return false;
	}

	FString NewName = EmitterNameBuffer;
	const size_t First = NewName.find_first_not_of(" \t\r\n");
	if (First == FString::npos)
	{
		SyncEmitterNameBuffer();
		return false;
	}

	const size_t Last = NewName.find_last_not_of(" \t\r\n");
	NewName = NewName.substr(First, Last - First + 1);
	if (NewName == "None" || !IsEmitterNameAvailable(NewName, ClampedIndex))
	{
		SyncEmitterNameBuffer();
		return false;
	}

	const FName OldName = Emitter->GetEmitterName();
	const FName NewFName(NewName);
	if (OldName == NewFName)
	{
		SyncEmitterNameBuffer();
		return false;
	}

	Emitter->SetEmitterName(NewFName);
	UpdateEmitterNameReferences(OldName, NewFName);
	SyncEmitterNameBuffer();
	return true;
}

int32 FParticleEditorWidget::GetLODCount() const
{
	return EditingParticleSystem ? EditingParticleSystem->GetLODCount() : 1;
}

int32 FParticleEditorWidget::ClampLODIndex(int32 LODIndex) const
{
	return std::clamp(LODIndex, 0, (std::max)(0, GetLODCount() - 1));
}

void FParticleEditorWidget::SetSelectedLODIndex(int32 LODIndex)
{
	SelectedLODIndex = ClampLODIndex(LODIndex);
	if (!bParticleSystemSelected)
	{
		SelectedModule = GetSelectedRequiredModule();
	}
	ApplySelectedLODToPreview(true);
}

UParticleLODLevel* FParticleEditorWidget::GetSelectedLODLevel(UParticleEmitter* Emitter) const
{
	if (!Emitter)
	{
		return nullptr;
	}

	return Emitter->GetLODLevel(ClampLODIndex(SelectedLODIndex));
}

void FParticleEditorWidget::AddLOD()
{
	if (!EditingParticleSystem)
	{
		return;
	}

	const int32 NewLODIndex = EditingParticleSystem->CreateLOD();
	SetSelectedLODIndex(NewLODIndex);
	MarkDirty();
}

void FParticleEditorWidget::DeleteSelectedLOD()
{
	if (!EditingParticleSystem || SelectedLODIndex <= 0)
	{
		return;
	}

	const int32 OldLODIndex = SelectedLODIndex;
	if (!EditingParticleSystem->RemoveLOD(SelectedLODIndex))
	{
		return;
	}

	SetSelectedLODIndex(OldLODIndex - 1);
	MarkDirty();
}

void FParticleEditorWidget::ApplySelectedLODToPreview(bool bRestart)
{
	if (!PreviewParticleComponent)
	{
		return;
	}

	PreviewParticleComponent->SetForcedLODLevel(SelectedLODIndex);
	if (bRestart)
	{
		RestartPreviewSystem();
	}
}

UParticleEmitter* FParticleEditorWidget::GetSelectedEmitter() const
{
	if (!EditingParticleSystem || EditingParticleSystem->Emitters.empty())
	{
		return nullptr;
	}

	const int32 ClampedIndex = std::clamp(SelectedEmitterIndex, 0,
		static_cast<int32>(EditingParticleSystem->Emitters.size()) - 1);
	return EditingParticleSystem->Emitters[ClampedIndex];
}

UParticleModuleRequired* FParticleEditorWidget::GetSelectedRequiredModule() const
{
	UParticleEmitter* Emitter = GetSelectedEmitter();
	if (!Emitter)
	{
		return nullptr;
	}

	UParticleLODLevel* LOD = GetSelectedLODLevel(Emitter);
	return LOD ? LOD->RequiredModule : nullptr;
}

UParticleModule* FParticleEditorWidget::GetSelectedModule() const
{
	UParticleEmitter* Emitter = GetSelectedEmitter();
	if (!Emitter)
	{
		return nullptr;
	}

	UParticleLODLevel* LOD = GetSelectedLODLevel(Emitter);
	if (!LOD)
	{
		return nullptr;
	}

	if (SelectedModule == LOD->RequiredModule || SelectedModule == LOD->SpawnModule)
	{
		return SelectedModule;
	}

	for (UParticleModule* Module : LOD->Modules)
	{
		if (SelectedModule == Module)
		{
			return SelectedModule;
		}
	}

	if (LOD->RequiredModule)
	{
		return LOD->RequiredModule;
	}
	return LOD->SpawnModule;
}

FString FParticleEditorWidget::GetEmitterDisplayName(UParticleEmitter* Emitter, int32 Index) const
{
	if (Emitter && Emitter->GetEmitterName().IsValid())
	{
		return Emitter->GetEmitterName().ToString();
	}

	char Buffer[32] = {};
	std::snprintf(Buffer, sizeof(Buffer), "Emitter %d", Index);
	return FString(Buffer);
}

FString FParticleEditorWidget::GetModuleDisplayName(UParticleModule* Module) const
{
	if (!Module)
	{
		return "None";
	}
	if (Module->IsA<UParticleModuleRequired>())
	{
		return "Required";
	}
	if (Module->IsA<UParticleModuleSpawn>())
	{
		return "Spawn";
	}
	if (Module->IsA<UParticleModuleLifetime>())
	{
		return "Lifetime";
	}
	if (Module->IsA<UParticleModuleSize>())
	{
		return "Initial Size";
	}
	if (Module->IsA<UParticleModuleVelocity>())
	{
		return "Initial Velocity";
	}
	if (Module->IsA<UParticleModuleInitialRotation>())
	{
		return "Initial Rotation";
	}
	if (Module->IsA<UParticleModuleInitialRotationRate>())
	{
		return "Initial Rotation Rate";
	}
	if (Module->IsA<UParticleModuleAcceleration>())
	{
		return "Acceleration";
	}
	if (Module->IsA<UParticleModuleOrbit>())
	{
		return "Orbit";
	}
	if (Module->IsA<UParticleModuleLocation>())
	{
		return "Initial Location";
	}
	if (Module->IsA<UParticleModuleColor>())
	{
		return "Initial Color";
	}
	if (Module->IsA<UParticleModuleColorOverLife>())
	{
		return "Color Over Life";
	}
	if (Module->IsA<UParticleModuleColorScaleOverLife>())
	{
		return "Color Scale Over Life";
	}
	if (Module->IsA<UParticleModuleBeamSource>())
	{
		return "Beam Source";
	}
	if (Module->IsA<UParticleModuleBeamTarget>())
	{
		return "Beam Target";
	}
	if (Module->IsA<UParticleModuleBeamNoise>())
	{
		return "Beam Noise";
	}
	if (Module->IsA<UParticleModuleTypeDataBeam2>())
	{
		return "Beam";
	}
	if (Module->IsA<UParticleModuleTypeDataMesh>())
	{
		return "Mesh";
	}
	if (Module->IsA<UParticleModuleCollision>())
	{
		return "Collision";
	}
	if (Module->IsA<UParticleModuleEventGenerator>())
	{
		return "Event Generator";
	}
	if (Module->IsA<UParticleModuleEventReceiverSpawn>())
	{
		return "Event Receiver Spawn";
	}
	if (Module->IsA<UParticleModuleTypeDataRibbon>())
	{
		return "Ribbon";
	}
	if (Module->IsA<UParticleModuleTypeDataBase>())
	{
		return "TypeData";
	}
	return Module->GetClass()->GetName();
}

FString FParticleEditorWidget::GetTypeDataDisplayName(UParticleLODLevel* LOD) const
{
	if (!LOD || !LOD->TypeDataModule)
	{
		return "Sprite";
	}
	if (LOD->TypeDataModule->IsA<UParticleModuleTypeDataBeam2>())
	{
		return "Beam";
	}
	if (LOD->TypeDataModule->IsA<UParticleModuleTypeDataMesh>())
	{
		return "Mesh";
	}
	if (LOD->TypeDataModule->IsA<UParticleModuleTypeDataRibbon>())
	{
		return "Ribbon";
	}
	return GetModuleDisplayName(LOD->TypeDataModule);
}

void FParticleEditorWidget::Render(float DeltaTime)
{
	(void)DeltaTime;

	if (!IsOpen() || !EditingParticleSystem)
	{
		return;
	}

	bool bWindowOpen = true;
	FString VisibleTitle = "Particle System Editor";
	const FString AssetName = EditingParticleSystem->GetName();
	if (!AssetName.empty())
	{
		VisibleTitle += " - ";
		VisibleTitle += AssetName;
	}
	if (IsDirty())
	{
		VisibleTitle += " *";
	}

	ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_None;
	if (ViewportClient.IsMouseOverViewport())
	{
		WindowFlags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
	}

	ImGui::SetNextWindowSize(ImVec2(1180.0f, 720.0f), ImGuiCond_Once);
	FString WindowTitle = VisibleTitle + WindowIdSuffix;
	if (ConsumeFocusRequest())
	{
		ImGui::SetNextWindowFocus();
	}

	if (!ImGui::Begin(WindowTitle.c_str(), &bWindowOpen, WindowFlags))
	{
		ImGui::End();
		if (!bWindowOpen)
		{
			Close();
		}
		return;
	}

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
	{
		FSlateApplication::Get().BringViewportToFront(&ViewportClient);
	}

	RenderToolbar();
	ImGui::Separator();
	RenderEditorLayout();

	ImGui::End();

	if (!bWindowOpen)
	{
		Close();
	}
}

void FParticleEditorWidget::RenderEditorLayout()
{
	static float TopHeight = 310.0f;

	const float SplitterHeight = 6.0f;
	const ImVec2 Available = ImGui::GetContentRegionAvail();
	const float MinTopHeight = 180.0f;
	const float MinBottomHeight = 180.0f;
	TopHeight = std::clamp(TopHeight, MinTopHeight, (std::max)(MinTopHeight, Available.y - SplitterHeight - MinBottomHeight));

	if (!ImGui::BeginTable(
		"ParticleEditorLayout",
		2,
		ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
	{
		return;
	}

	ImGui::TableSetupColumn("PreviewAndDetails", ImGuiTableColumnFlags_WidthStretch, 0.34f);
	ImGui::TableSetupColumn("EmittersAndCurve", ImGuiTableColumnFlags_WidthStretch, 0.66f);
	ImGui::TableNextRow();

	ImGui::TableSetColumnIndex(0);
	ImGui::BeginChild("ParticlePreviewPane", ImVec2(0.0f, TopHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	RenderPreviewViewport(ImGui::GetContentRegionAvail());
	ImGui::EndChild();
	DrawHorizontalSplitter(TopHeight, MinTopHeight, MinBottomHeight, Available.y, "##ParticleHorizontalSplitterLeft");
	ImGui::BeginChild("ParticleDetailsPane", ImVec2(0.0f, 0.0f), true);
	if (RenderDetailsPanel())
	{
		ApplyEmitterEdit();
	}
	ImGui::EndChild();

	ImGui::TableSetColumnIndex(1);
	ImGui::BeginChild("ParticleEmitterPane", ImVec2(0.0f, TopHeight), true);
	RenderEmitterList();
	ImGui::EndChild();
	DrawHorizontalSplitter(TopHeight, MinTopHeight, MinBottomHeight, Available.y, "##ParticleHorizontalSplitterRight");
	ImGui::BeginChild("ParticleCurvePane", ImVec2(0.0f, 0.0f), true);
	if (RenderCurvePanel())
	{
		ApplyEmitterEdit();
	}
	ImGui::EndChild();

	ImGui::EndTable();
}

void FParticleEditorWidget::RenderToolbar()
{
	if (ImGui::Button("Save"))
	{
		CommitAssetNameEdit();
		if (CommitEmitterNameEdit())
		{
			ApplyEmitterEdit();
		}
		if (FParticleSystemManager::Get().Save(EditingParticleSystem))
		{
			ClearDirty();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button(bSimulating ? "Pause" : "Play"))
	{
		bSimulating = !bSimulating;
	}
	ImGui::SameLine();
	if (ImGui::Button("Restart Sim"))
	{
		RestartPreviewSystem();
	}
	ImGui::SameLine();
	if (ImGui::Button("Frame Camera"))
	{
		ResetPreviewCameraToParticleBounds();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();

	const int32 LODCount = GetLODCount();
	if (DrawParticleToolbarButton("BackgroundColor", L"icon_Cascade_Color_40x.png", "Background Color", false))
	{
		ImGui::OpenPopup("##ParticleBackgroundColorPopup");
	}
	if (ImGui::BeginPopup("##ParticleBackgroundColorPopup"))
	{
		const float* CurrentClearColor = ViewportClient.GetClearColor();
		float EditableClearColor[4] =
		{
			CurrentClearColor[0],
			CurrentClearColor[1],
			CurrentClearColor[2],
			CurrentClearColor[3]
		};

		if (ImGui::ColorEdit4("Color", EditableClearColor, ImGuiColorEditFlags_AlphaBar))
		{
			ViewportClient.SetClearColor(
				EditableClearColor[0],
				EditableClearColor[1],
				EditableClearColor[2],
				EditableClearColor[3]);
		}
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	if (DrawParticleToolbarButton("LowerLOD", L"Cascade_LowerLOD_512x.png", "Lower LOD", SelectedLODIndex <= 0))
	{
		SetSelectedLODIndex(SelectedLODIndex - 1);
	}
	ImGui::SameLine();
	if (DrawParticleToolbarButton("AddLODLeft", L"Cascade_AddLOD1_512x.png", "Add LOD", false))
	{
		AddLOD();
	}
	ImGui::SameLine();
	ImGui::TextUnformatted("LOD :");
	ImGui::SameLine();
	int32 EditableLODIndex = SelectedLODIndex;
	ImGui::SetNextItemWidth(42.0f);
	if (ImGui::InputInt("##ParticleSelectedLOD", &EditableLODIndex, 0, 0))
	{
		SetSelectedLODIndex(EditableLODIndex);
	}
	ImGui::SameLine();
	if (DrawParticleToolbarButton("AddLODRight", L"Cascade_AddLOD2_512x.png", "Add LOD", false))
	{
		AddLOD();
	}
	ImGui::SameLine();
	if (DrawParticleToolbarButton("HigherLOD", L"Cascade_HigherLOD_512x.png", "Higher LOD", SelectedLODIndex >= LODCount - 1))
	{
		SetSelectedLODIndex(SelectedLODIndex + 1);
	}
	ImGui::SameLine();
	if (DrawParticleToolbarButton("DeleteLOD", L"Cascade_DeleteLOD_512x.png", "Delete LOD", SelectedLODIndex <= 0 || LODCount <= 1))
	{
		DeleteSelectedLOD();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	ImGui::TextUnformatted("Name:");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(180.0f);
	if (ImGui::InputText("##ParticleSystemName", AssetNameBuffer, sizeof(AssetNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
	{
		CommitAssetNameEdit();
	}
	if (ImGui::IsItemDeactivatedAfterEdit())
	{
		CommitAssetNameEdit();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	ImGui::TextDisabled("%zu emitters", EditingParticleSystem ? EditingParticleSystem->Emitters.size() : 0);
}

void FParticleEditorWidget::RenderPreviewViewport(const ImVec2& Size)
{
	ImGui::BeginGroup();
	{
		ImVec2 ViewportPos = ImGui::GetCursorScreenPos();
		ViewportClient.SetViewportRect(ViewportPos.x, ViewportPos.y, Size.x, Size.y);

		FViewport* VP = ViewportClient.GetViewport();
		if (VP && Size.x > 0.0f && Size.y > 0.0f)
		{
			VP->RequestResize(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y));
			ParticleViewportWindow.SetRect(FRect(ViewportPos.x, ViewportPos.y, Size.x, Size.y));

			if (VP->GetSRV())
			{
				ImGui::Image((ImTextureID)VP->GetSRV(), Size);
				const float ImGuiWheel = ImGui::GetIO().MouseWheel;
				if (ImGui::IsItemHovered() && ImGuiWheel != 0.0f && InputSystem::Get().GetScrollNotches() == 0.0f)
				{
					ViewportClient.QueueScrollInput(ImGuiWheel);
				}
			}

			constexpr float ToolbarHeight = 28.0f;
			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			DrawList->AddRectFilled(
				ViewportPos,
				ImVec2(ViewportPos.x + Size.x, ViewportPos.y + ToolbarHeight),
				IM_COL32(40, 40, 40, 255));

			FViewportToolbarContext Context;
			Context.Renderer = &GEngine->GetRenderer();
			Context.Settings = &FEditorSettings::Get().MeshEditorViewportSettings;
			Context.RenderOptions = &ViewportClient.GetRenderOptions();
			Context.ToolbarLeft = ViewportPos.x;
			Context.ToolbarTop = ViewportPos.y;
			Context.ToolbarWidth = Size.x;
			Context.bReservePlayStopSpace = false;
			Context.bShowAddActor = false;
			Context.bShowGizmoControls = false;

			FViewportToolbar::Render(Context);
		}
	}
	ImGui::EndGroup();
}

void FParticleEditorWidget::RenderEmitterList()
{
	ImGui::TextUnformatted("Emitters");
	ImGui::SameLine();
	if (ImGui::Button("+ Emitter"))
	{
		const int32 NewIndex = EditingParticleSystem ? static_cast<int32>(EditingParticleSystem->Emitters.size()) : 0;
		char NameBuffer[48] = {};
		std::snprintf(NameBuffer, sizeof(NameBuffer), "Particle Emitter %d", NewIndex + 1);
		if (UParticleEmitter* NewEmitter = CreateDefaultEmitter(NameBuffer))
		{
			EditingParticleSystem->Emitters.push_back(NewEmitter);
			EditingParticleSystem->NormalizeLODData();
			SelectedEmitterIndex = NewIndex;
			SelectedModule = GetSelectedRequiredModule();
			bParticleSystemSelected = false;
			RestartPreviewSystem();
			MarkDirty();
		}
	}

	ImGui::BeginChild("EmitterList", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);

	if (!EditingParticleSystem || EditingParticleSystem->Emitters.empty())
	{
		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
			ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
			!ImGui::IsAnyItemHovered())
		{
			bParticleSystemSelected = true;
			SelectedModule = nullptr;
		}
		ImGui::TextDisabled("No emitters.");
		ImGui::EndChild();
		return;
	}

	int32 EmitterToDelete = -1;
	int32 EmitterToAddModule = -1;
	int32 EmitterToDeleteModule = -1;
	int32 EmitterToSetTypeData = -1;
	int32 EmitterDragSource = -1;
	int32 EmitterDragInsertIndex = -1;
	int32 ModuleDragSourceEmitter = -1;
	int32 ModuleDragTargetEmitter = -1;
	int32 ModuleDragInsertIndex = -1;
	UParticleModule* ModuleToDelete = nullptr;
	UParticleModule* ModuleDragToMove = nullptr;
	EAddableModuleType ModuleTypeToAdd = EAddableModuleType::Lifetime;
	EEmitterTypeData TypeDataToSet = EEmitterTypeData::Sprite;
	struct FEmitterDragPayload
	{
		int32 SourceEmitterIndex = -1;
	};
	struct FModuleDragPayload
	{
		int32 SourceEmitterIndex = -1;
		UParticleModule* Module = nullptr;
	};
	auto QueueAddModule = [&](int32 EmitterIndex, EAddableModuleType ModuleType)
	{
		EmitterToAddModule = EmitterIndex;
		ModuleTypeToAdd = ModuleType;
	};
	auto QueueSetTypeData = [&](int32 EmitterIndex, EEmitterTypeData TypeData)
	{
		EmitterToSetTypeData = EmitterIndex;
		TypeDataToSet = TypeData;
	};
	auto QueueEmitterDrop = [&](int32 SourceEmitterIndex, int32 TargetInsertIndex)
	{
		EmitterDragSource = SourceEmitterIndex;
		EmitterDragInsertIndex = TargetInsertIndex;
	};
	auto QueueModuleDrop = [&](int32 SourceEmitterIndex, UParticleModule* Module, int32 TargetEmitterIndex, int32 TargetInsertIndex)
	{
		ModuleDragSourceEmitter = SourceEmitterIndex;
		ModuleDragToMove = Module;
		ModuleDragTargetEmitter = TargetEmitterIndex;
		ModuleDragInsertIndex = TargetInsertIndex;
	};
	auto CanMoveModuleToEmitter = [&](UParticleModule* Module, int32 TargetEmitterIndex) -> bool
	{
		if (!EditingParticleSystem || !Module || TargetEmitterIndex < 0 ||
			TargetEmitterIndex >= static_cast<int32>(EditingParticleSystem->Emitters.size()) ||
			Module->IsA<UParticleModuleTypeDataBase>())
		{
			return false;
		}

		if (!Module->IsA<UParticleModuleBeamBase>())
		{
			return true;
		}

		UParticleLODLevel* TargetLOD = GetSelectedLODLevel(EditingParticleSystem->Emitters[TargetEmitterIndex]);
		return TargetLOD && TargetLOD->TypeDataModule && TargetLOD->TypeDataModule->IsA<UParticleModuleTypeDataBeam2>();
	};
	auto DrawTypeDataContextMenu = [&](int32 EmitterIndex, UParticleLODLevel* LOD)
	{
		const bool bIsSprite = !LOD || !LOD->TypeDataModule;
		const bool bIsMesh = LOD && LOD->TypeDataModule && LOD->TypeDataModule->IsA<UParticleModuleTypeDataMesh>();
		const bool bIsBeam = LOD && LOD->TypeDataModule && LOD->TypeDataModule->IsA<UParticleModuleTypeDataBeam2>();
		const bool bIsRibbon = LOD && LOD->TypeDataModule && LOD->TypeDataModule->IsA<UParticleModuleTypeDataRibbon>();

		if (ImGui::MenuItem("Sprite", nullptr, bIsSprite))
		{
			QueueSetTypeData(EmitterIndex, EEmitterTypeData::Sprite);
		}
		if (ImGui::MenuItem("Mesh", nullptr, bIsMesh))
		{
			QueueSetTypeData(EmitterIndex, EEmitterTypeData::Mesh);
		}
		if (ImGui::MenuItem("Beam", nullptr, bIsBeam))
		{
			QueueSetTypeData(EmitterIndex, EEmitterTypeData::Beam);
		}
		if (ImGui::MenuItem("Ribbon", nullptr, bIsRibbon))
		{
			QueueSetTypeData(EmitterIndex, EEmitterTypeData::Ribbon);
		}
	};
	auto DrawEmitterContextMenu = [&](int32 EmitterIndex)
	{
		if (ImGui::BeginMenu("Add Module"))
		{
			if (ImGui::MenuItem("Lifetime"))
			{
				QueueAddModule(EmitterIndex, EAddableModuleType::Lifetime);
			}
			if (ImGui::MenuItem("Initial Size"))
			{
				QueueAddModule(EmitterIndex, EAddableModuleType::Size);
			}
			if (ImGui::MenuItem("Initial Velocity"))
			{
				QueueAddModule(EmitterIndex, EAddableModuleType::Velocity);
			}
			if (ImGui::MenuItem("Initial Rotation"))
			{
				QueueAddModule(EmitterIndex, EAddableModuleType::InitialRotation);
			}
			if (ImGui::MenuItem("Initial Rotation Rate"))
			{
				QueueAddModule(EmitterIndex, EAddableModuleType::InitialRotationRate);
			}
			if (ImGui::MenuItem("Acceleration"))
			{
				QueueAddModule(EmitterIndex, EAddableModuleType::Acceleration);
			}
			if (ImGui::MenuItem("Orbit"))
			{
				QueueAddModule(EmitterIndex, EAddableModuleType::Orbit);
			}
			if (ImGui::MenuItem("Initial Location"))
			{
				QueueAddModule(EmitterIndex, EAddableModuleType::Location);
			}
			if (ImGui::MenuItem("Initial Color"))
			{
				QueueAddModule(EmitterIndex, EAddableModuleType::Color);
			}
			if (ImGui::MenuItem("Color Over Life"))
			{
				QueueAddModule(EmitterIndex, EAddableModuleType::ColorOverLife);
			}
			if (ImGui::MenuItem("Color Scale Over Life"))
			{
				QueueAddModule(EmitterIndex, EAddableModuleType::ColorScaleOverLife);
			}
			UParticleEmitter* MenuEmitter = EditingParticleSystem
				&& EmitterIndex >= 0
				&& EmitterIndex < static_cast<int32>(EditingParticleSystem->Emitters.size())
					? EditingParticleSystem->Emitters[EmitterIndex]
					: nullptr;
			UParticleLODLevel* MenuLOD = GetSelectedLODLevel(MenuEmitter);
			const bool bCanAddBeamModule = MenuLOD
				&& MenuLOD->TypeDataModule
				&& MenuLOD->TypeDataModule->IsA<UParticleModuleTypeDataBeam2>();
			ImGui::Separator();
			if (ImGui::BeginMenu("Beam", bCanAddBeamModule))
			{
				if (ImGui::MenuItem("Source"))
				{
					QueueAddModule(EmitterIndex, EAddableModuleType::BeamSource);
				}
				if (ImGui::MenuItem("Target"))
				{
					QueueAddModule(EmitterIndex, EAddableModuleType::BeamTarget);
				}
				if (ImGui::MenuItem("Noise"))
				{
					QueueAddModule(EmitterIndex, EAddableModuleType::BeamNoise);
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Collision"))
			{
				QueueAddModule(EmitterIndex, EAddableModuleType::Collision);
			}
			if (ImGui::MenuItem("Event Generator"))
			{
				QueueAddModule(EmitterIndex, EAddableModuleType::EventGenerator);
			}
			if (ImGui::MenuItem("Event Receiver Spawn"))
			{
				QueueAddModule(EmitterIndex, EAddableModuleType::EventReceiverSpawn);
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Type Data"))
		{
			UParticleLODLevel* LOD = nullptr;
			if (EditingParticleSystem && EmitterIndex >= 0 && EmitterIndex < static_cast<int32>(EditingParticleSystem->Emitters.size()))
			{
				LOD = GetSelectedLODLevel(EditingParticleSystem->Emitters[EmitterIndex]);
			}

			DrawTypeDataContextMenu(EmitterIndex, LOD);
			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("Delete Emitter"))
		{
			EmitterToDelete = EmitterIndex;
		}
	};
	auto DrawModuleContextMenu = [&](int32 EmitterIndex, UParticleLODLevel* LOD, UParticleModule* Module)
	{
		DrawEmitterContextMenu(EmitterIndex);

		const bool bCanDeleteModule = LOD && Module &&
			std::find(LOD->Modules.begin(), LOD->Modules.end(), Module) != LOD->Modules.end();
		ImGui::Separator();
		if (ImGui::MenuItem("Delete Module", nullptr, false, bCanDeleteModule))
		{
			EmitterToDeleteModule = EmitterIndex;
			ModuleToDelete = Module;
		}
	};

	for (int32 Index = 0; Index < static_cast<int32>(EditingParticleSystem->Emitters.size()); ++Index)
	{
		UParticleEmitter* Emitter = EditingParticleSystem->Emitters[Index];
		UParticleLODLevel* LOD = GetSelectedLODLevel(Emitter);
		if (!Emitter || !LOD)
		{
			continue;
		}

		ImGui::PushID(Index);
		ImGui::BeginGroup();
		ImGui::BeginChild("EmitterColumn", ImVec2(190.0f, 0.0f), true);

		const bool bEmitterSelected = !bParticleSystemSelected && Index == SelectedEmitterIndex;
		const FString Label = GetEmitterDisplayName(Emitter, Index);
		ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(255, 124, 0, 255));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(255, 146, 42, 255));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(255, 124, 0, 255));
		if (ImGui::Selectable(Label.c_str(), bEmitterSelected, 0, ImVec2(0.0f, 28.0f)))
		{
			SelectedEmitterIndex = Index;
			SelectedModule = LOD->RequiredModule;
			bParticleSystemSelected = false;
		}
		ImGui::PopStyleColor(3);
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
		{
			const FEmitterDragPayload Payload{ Index };
			ImGui::SetDragDropPayload("PARTICLE_EMITTER", &Payload, sizeof(Payload));
			ImGui::TextUnformatted(Label.c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("PARTICLE_EMITTER"))
			{
				const FEmitterDragPayload* Drag = static_cast<const FEmitterDragPayload*>(Payload->Data);
				const ImVec2 Min = ImGui::GetItemRectMin();
				const ImVec2 Max = ImGui::GetItemRectMax();
				const bool bInsertAfter = ImGui::GetIO().MousePos.x > (Min.x + Max.x) * 0.5f;
				QueueEmitterDrop(Drag->SourceEmitterIndex, Index + (bInsertAfter ? 1 : 0));
			}
			if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("PARTICLE_MODULE"))
			{
				const FModuleDragPayload* Drag = static_cast<const FModuleDragPayload*>(Payload->Data);
				if (Drag && Drag->Module && CanMoveModuleToEmitter(Drag->Module, Index))
				{
					QueueModuleDrop(Drag->SourceEmitterIndex, Drag->Module, Index, static_cast<int32>(LOD->Modules.size()));
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (ImGui::BeginPopupContextItem("EmitterHeaderContext"))
		{
			SelectedEmitterIndex = Index;
			SelectedModule = LOD->RequiredModule;
			bParticleSystemSelected = false;
			DrawEmitterContextMenu(Index);
			ImGui::EndPopup();
		}

		auto DrawModuleRow = [&](UParticleModule* Module, int32 ModuleIndex)
		{
			if (!Module)
			{
				return;
			}

			ImGui::PushID(Module);
			auto ModuleIt = std::find(LOD->Modules.begin(), LOD->Modules.end(), Module);
			const bool bMovableModule = ModuleIt != LOD->Modules.end() && !Module->IsA<UParticleModuleTypeDataBase>();
			const int32 ActualModuleIndex = bMovableModule
				? static_cast<int32>(std::distance(LOD->Modules.begin(), ModuleIt))
				: -1;

			const bool bSelected = !bParticleSystemSelected && Index == SelectedEmitterIndex && Module == GetSelectedModule();
			const ImU32 RowColor = GetModuleRowColor(bSelected, ModuleIndex);
			ImGui::PushStyleColor(ImGuiCol_Header, RowColor);
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, bSelected ? RowColor : IM_COL32(78, 80, 92, 255));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, RowColor);
			const FString ModuleName = GetModuleDisplayName(Module);
			if (ImGui::Selectable(ModuleName.c_str(), bSelected, 0, ImVec2(0.0f, 24.0f)))
			{
				SelectedEmitterIndex = Index;
				SelectedModule = Module;
				bParticleSystemSelected = false;
			}
			ImGui::PopStyleColor(3);
			if (bMovableModule && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
			{
				const FModuleDragPayload Payload{ Index, Module };
				ImGui::SetDragDropPayload("PARTICLE_MODULE", &Payload, sizeof(Payload));
				ImGui::TextUnformatted(ModuleName.c_str());
				ImGui::EndDragDropSource();
			}
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("PARTICLE_MODULE"))
				{
					const FModuleDragPayload* Drag = static_cast<const FModuleDragPayload*>(Payload->Data);
					if (Drag && Drag->Module && CanMoveModuleToEmitter(Drag->Module, Index))
					{
						int32 TargetInsertIndex = 0;
						if (ActualModuleIndex >= 0)
						{
							const ImVec2 Min = ImGui::GetItemRectMin();
							const ImVec2 Max = ImGui::GetItemRectMax();
							const bool bInsertAfter = ImGui::GetIO().MousePos.y > (Min.y + Max.y) * 0.5f;
							TargetInsertIndex = ActualModuleIndex + (bInsertAfter ? 1 : 0);
						}
						QueueModuleDrop(Drag->SourceEmitterIndex, Drag->Module, Index, TargetInsertIndex);
					}
				}
				ImGui::EndDragDropTarget();
			}
			if (ImGui::BeginPopupContextItem("ModuleRowContext"))
			{
				SelectedEmitterIndex = Index;
				SelectedModule = Module;
				bParticleSystemSelected = false;
				DrawModuleContextMenu(Index, LOD, Module);
				ImGui::EndPopup();
			}
			ImGui::PopID();
		};

		const bool bTypeDataSelected = !bParticleSystemSelected && Index == SelectedEmitterIndex && LOD->TypeDataModule && SelectedModule == LOD->TypeDataModule;
		ImGui::PushID("TypeData");
		ImGui::PushStyleColor(ImGuiCol_Header, bTypeDataSelected ? IM_COL32(245, 215, 42, 255) : IM_COL32(34, 36, 43, 255));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, bTypeDataSelected ? IM_COL32(245, 215, 42, 255) : IM_COL32(58, 61, 72, 255));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(74, 78, 92, 255));
		const FString TypeDataLabel = GetTypeDataDisplayName(LOD);
		if (ImGui::Selectable(TypeDataLabel.c_str(), bTypeDataSelected, 0, ImVec2(0.0f, 24.0f)))
		{
			SelectedEmitterIndex = Index;
			SelectedModule = LOD->TypeDataModule
				? static_cast<UParticleModule*>(LOD->TypeDataModule)
				: static_cast<UParticleModule*>(LOD->RequiredModule);
			bParticleSystemSelected = false;
		}
		ImGui::PopStyleColor(3);
		if (ImGui::BeginPopupContextItem("TypeDataContext"))
		{
			SelectedEmitterIndex = Index;
			SelectedModule = LOD->TypeDataModule
				? static_cast<UParticleModule*>(LOD->TypeDataModule)
				: static_cast<UParticleModule*>(LOD->RequiredModule);
			bParticleSystemSelected = false;
			DrawTypeDataContextMenu(Index, LOD);
			ImGui::EndPopup();
		}
		ImGui::PopID();

		int32 ModuleIndex = 0;
		DrawModuleRow(LOD->RequiredModule, ModuleIndex++);
		DrawModuleRow(LOD->SpawnModule, ModuleIndex++);
		for (UParticleModule* Module : LOD->Modules)
		{
			if (Module && Module->IsA<UParticleModuleTypeDataBase>())
			{
				continue;
			}
			DrawModuleRow(Module, ModuleIndex++);
		}

		if (ImGui::BeginPopupContextWindow("EmitterColumnContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			SelectedEmitterIndex = Index;
			SelectedModule = GetSelectedModule();
			bParticleSystemSelected = false;
			DrawEmitterContextMenu(Index);
			ImGui::EndPopup();
		}

		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
			ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
			!ImGui::IsAnyItemHovered())
		{
			bParticleSystemSelected = true;
			SelectedModule = nullptr;
		}

		ImGui::EndChild();
		ImGui::EndGroup();
		ImGui::PopID();

		if (Index + 1 < static_cast<int32>(EditingParticleSystem->Emitters.size()))
		{
			ImGui::SameLine();
		}
	}

	if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
		ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
		!ImGui::IsAnyItemHovered())
	{
		bParticleSystemSelected = true;
		SelectedModule = nullptr;
	}

	ImGui::EndChild();

	if (EmitterToDelete >= 0)
	{
		DeleteEmitter(EmitterToDelete);
	}
	else if (EmitterToDeleteModule >= 0)
	{
		DeleteModuleFromEmitter(EmitterToDeleteModule, ModuleToDelete);
	}
	else if (EmitterDragSource >= 0)
	{
		MoveEmitterToIndex(EmitterDragSource, EmitterDragInsertIndex);
	}
	else if (ModuleDragSourceEmitter >= 0)
	{
		MoveModuleToEmitterAtIndex(ModuleDragSourceEmitter, ModuleDragToMove, ModuleDragTargetEmitter, ModuleDragInsertIndex);
	}
	else if (EmitterToAddModule >= 0)
	{
		AddModuleToEmitter(EmitterToAddModule, ModuleTypeToAdd);
	}
	else if (EmitterToSetTypeData >= 0)
	{
		SetEmitterTypeData(EmitterToSetTypeData, TypeDataToSet);
	}
}

bool FParticleEditorWidget::RenderCurvePanel()
{
	ImGui::TextUnformatted("Curve Editor");
	ImGui::Separator();

	UParticleModule* Module = GetSelectedModule();
	std::vector<FCurveEditorTrack> Tracks;
	CollectModuleCurveTracks(Module, Tracks);

	if (!Module || Tracks.empty())
	{
		ImGui::TextDisabled("Select a module with editable distributions.");
		return false;
	}

	static int32 SelectedTrack = 0;
	static int32 SelectedKey = -1;
	static UParticleModule* ViewModule = nullptr;
	static float ViewTimeMin = 0.0f;
	static float ViewTimeMax = 1.0f;
	static float ViewValueMin = 0.0f;
	static float ViewValueMax = 1.0f;
	SelectedTrack = std::clamp(SelectedTrack, 0, static_cast<int32>(Tracks.size()) - 1);
	if (!Tracks[SelectedTrack].Distribution)
	{
		return false;
	}

	bool bChanged = false;
	bool bFitViewRequested = false;
	if (ImGui::Button("Add Key"))
	{
		FFloatCurve* SelectedCurve = GetEditableTrackCurve(Tracks[SelectedTrack]);
		if (!SelectedCurve)
		{
			return false;
		}
		const float NewTime = std::clamp((ViewTimeMin + ViewTimeMax) * 0.5f, 0.0f, 1.0f);
		const float NewValue = SelectedCurve->Evaluate(NewTime);
		SelectedCurve->AddKey(NewTime, NewValue);
		SelectedCurve->SortKeys();
		SelectedCurve->AutoSetTangents();
		SelectedKey = FindNearestCurveKeyIndex(*SelectedCurve, NewTime, NewValue);
		bChanged = true;
	}
	ImGui::SameLine();
	FFloatCurve* SelectedCurveForDelete = GetTrackCurveIfAlreadyCurve(Tracks[SelectedTrack]);
	ImGui::BeginDisabled(!SelectedCurveForDelete || SelectedKey < 0 || SelectedKey >= static_cast<int32>(SelectedCurveForDelete->Keys.size()) || SelectedCurveForDelete->Keys.size() <= 1);
	if (ImGui::Button("Delete Key"))
	{
		if (SelectedCurveForDelete)
		{
			SelectedCurveForDelete->Keys.erase(SelectedCurveForDelete->Keys.begin() + SelectedKey);
			SelectedCurveForDelete->AutoSetTangents();
			SelectedKey = std::clamp(SelectedKey, 0, static_cast<int32>(SelectedCurveForDelete->Keys.size()) - 1);
			bChanged = true;
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Fit"))
	{
		bFitViewRequested = true;
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%s", Tracks[SelectedTrack].Label.c_str());

	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	const ImVec2 Size = ImGui::GetContentRegionAvail();
	if (Size.x <= 0.0f || Size.y <= 0.0f)
	{
		return bChanged;
	}

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 Max(Origin.x + Size.x, Origin.y + Size.y);
	DrawList->AddRectFilled(Origin, Max, IM_COL32(45, 45, 45, 255));

	const float TrackWidth = (std::min)(190.0f, Size.x * 0.45f);
	DrawList->AddRectFilled(Origin, ImVec2(Origin.x + TrackWidth, Max.y), IM_COL32(120, 120, 120, 255));
	constexpr float AxisLabelWidth = 56.0f;
	constexpr float AxisLabelHeight = 18.0f;
	const ImVec2 GraphMin(Origin.x + TrackWidth + AxisLabelWidth, Origin.y + 4.0f);
	const ImVec2 GraphMax(Max.x - 6.0f, Max.y - AxisLabelHeight);
	if (GraphMax.x <= GraphMin.x || GraphMax.y <= GraphMin.y)
	{
		return bChanged;
	}

	float AutoValueMin = 0.0f;
	float AutoValueMax = 1.0f;
	bool bHasValue = false;
	for (const FCurveEditorTrack& Track : Tracks)
	{
		for (int32 SampleIndex = 0; SampleIndex <= 32; ++SampleIndex)
		{
			const float T = static_cast<float>(SampleIndex) / 32.0f;
			const float Value = EvaluateTrackValue(Track, T);
			AutoValueMin = bHasValue ? (std::min)(AutoValueMin, Value) : Value;
			AutoValueMax = bHasValue ? (std::max)(AutoValueMax, Value) : Value;
			bHasValue = true;
		}
	}
	if (std::abs(AutoValueMax - AutoValueMin) < 0.001f)
	{
		AutoValueMax += 1.0f;
		AutoValueMin -= 1.0f;
	}
	const float Padding = (AutoValueMax - AutoValueMin) * 0.12f;
	AutoValueMin -= Padding;
	AutoValueMax += Padding;

	if (ViewModule != Module || bFitViewRequested)
	{
		ViewModule = Module;
		ViewTimeMin = 0.0f;
		ViewTimeMax = 1.0f;
		ViewValueMin = AutoValueMin;
		ViewValueMax = AutoValueMax;
	}

	auto ClampCurveView = [&]()
	{
		constexpr float MinTimeSpan = 0.01f;
		constexpr float MaxTimeSpan = 1.0f;
		float TimeSpan = ViewTimeMax - ViewTimeMin;
		if (TimeSpan < MinTimeSpan)
		{
			const float Center = (ViewTimeMin + ViewTimeMax) * 0.5f;
			ViewTimeMin = Center - MinTimeSpan * 0.5f;
			ViewTimeMax = Center + MinTimeSpan * 0.5f;
			TimeSpan = MinTimeSpan;
		}
		if (TimeSpan > MaxTimeSpan)
		{
			const float Center = (ViewTimeMin + ViewTimeMax) * 0.5f;
			ViewTimeMin = Center - MaxTimeSpan * 0.5f;
			ViewTimeMax = Center + MaxTimeSpan * 0.5f;
		}
		if (ViewTimeMin < 0.0f)
		{
			ViewTimeMax -= ViewTimeMin;
			ViewTimeMin = 0.0f;
		}
		if (ViewTimeMax > 1.0f)
		{
			const float Overflow = ViewTimeMax - 1.0f;
			ViewTimeMin -= Overflow;
			ViewTimeMax = 1.0f;
		}
		ViewTimeMin = std::clamp(ViewTimeMin, 0.0f, 1.0f - MinTimeSpan);
		ViewTimeMax = std::clamp(ViewTimeMax, ViewTimeMin + MinTimeSpan, 1.0f);

		if (std::abs(ViewValueMax - ViewValueMin) < 0.001f)
		{
			const float Center = (ViewValueMin + ViewValueMax) * 0.5f;
			ViewValueMin = Center - 0.5f;
			ViewValueMax = Center + 0.5f;
		}
	};
	ClampCurveView();

	const ImU32 GridColor = IM_COL32(155, 155, 155, 180);
	const ImU32 AxisColor = IM_COL32(210, 210, 210, 220);
	const ImU32 AxisTextColor = IM_COL32(225, 225, 225, 230);
	ImGuiIO& IO = ImGui::GetIO();
	const ImVec2 MousePos = IO.MousePos;
	const bool bMouseInGraph =
		MousePos.x >= GraphMin.x && MousePos.x <= GraphMax.x &&
		MousePos.y >= GraphMin.y && MousePos.y <= GraphMax.y;

	if (bMouseInGraph && IO.MouseWheel != 0.0f)
	{
		const float TimeUnderMouse = ViewTimeMin + ((MousePos.x - GraphMin.x) / (GraphMax.x - GraphMin.x)) * (ViewTimeMax - ViewTimeMin);
		const float ValueUnderMouse = ViewValueMin + ((GraphMax.y - MousePos.y) / (GraphMax.y - GraphMin.y)) * (ViewValueMax - ViewValueMin);
		const float ZoomFactor = IO.MouseWheel > 0.0f ? 0.86f : 1.16f;
		const float TimeAlpha = std::clamp((TimeUnderMouse - ViewTimeMin) / (ViewTimeMax - ViewTimeMin), 0.0f, 1.0f);
		const float ValueAlpha = std::clamp((ValueUnderMouse - ViewValueMin) / (ViewValueMax - ViewValueMin), 0.0f, 1.0f);
		const float NewTimeSpan = (ViewTimeMax - ViewTimeMin) * ZoomFactor;
		const float NewValueSpan = (ViewValueMax - ViewValueMin) * ZoomFactor;
		ViewTimeMin = TimeUnderMouse - NewTimeSpan * TimeAlpha;
		ViewTimeMax = ViewTimeMin + NewTimeSpan;
		ViewValueMin = ValueUnderMouse - NewValueSpan * ValueAlpha;
		ViewValueMax = ViewValueMin + NewValueSpan;
		ClampCurveView();
		IO.MouseWheel = 0.0f;
	}

	const float TimeTickStep = CalculateNiceTickStep(ViewTimeMax - ViewTimeMin, GraphMax.x - GraphMin.x, 72.0f);
	const float ValueTickStep = CalculateNiceTickStep(ViewValueMax - ViewValueMin, GraphMax.y - GraphMin.y, 36.0f);
	const int32 TimePrecision = GetTickLabelPrecision(TimeTickStep);
	const int32 ValuePrecision = GetTickLabelPrecision(ValueTickStep);
	char LabelBuffer[32];
	float LastTimeLabelRight = -FLT_MAX;
	const float FirstTimeTick = std::ceil(ViewTimeMin / TimeTickStep) * TimeTickStep;
	for (float T = FirstTimeTick; T <= ViewTimeMax + TimeTickStep * 0.5f; T += TimeTickStep)
	{
		if (T < ViewTimeMin - TimeTickStep * 0.5f)
		{
			continue;
		}
		const float X = GraphMin.x + ((T - ViewTimeMin) / (ViewTimeMax - ViewTimeMin)) * (GraphMax.x - GraphMin.x);
		DrawList->AddLine(ImVec2(X, GraphMin.y), ImVec2(X, GraphMax.y), GridColor);
		std::snprintf(LabelBuffer, IM_ARRAYSIZE(LabelBuffer), "%.*f", TimePrecision, T);
		const ImVec2 TextSize = ImGui::CalcTextSize(LabelBuffer);
		const float TextLeft = X - TextSize.x * 0.5f;
		const float TextRight = X + TextSize.x * 0.5f;
		if (TextLeft > LastTimeLabelRight + 6.0f && TextLeft >= GraphMin.x - 2.0f && TextRight <= GraphMax.x + 2.0f)
		{
			DrawList->AddText(ImVec2(TextLeft, GraphMax.y + 2.0f), AxisTextColor, LabelBuffer);
			LastTimeLabelRight = TextRight;
		}
	}

	const float FirstValueTick = std::ceil(ViewValueMin / ValueTickStep) * ValueTickStep;
	float LastValueLabelY = FLT_MAX;
	for (float Value = FirstValueTick; Value <= ViewValueMax + ValueTickStep * 0.5f; Value += ValueTickStep)
	{
		const float Alpha = (Value - ViewValueMin) / (ViewValueMax - ViewValueMin);
		const float Y = GraphMax.y - Alpha * (GraphMax.y - GraphMin.y);
		DrawList->AddLine(ImVec2(GraphMin.x, Y), ImVec2(GraphMax.x, Y), GridColor);
		std::snprintf(LabelBuffer, IM_ARRAYSIZE(LabelBuffer), "%.*f", ValuePrecision, Value);
		const ImVec2 TextSize = ImGui::CalcTextSize(LabelBuffer);
		if (std::abs(Y - LastValueLabelY) > TextSize.y + 4.0f)
		{
			DrawList->AddText(ImVec2(GraphMin.x - AxisLabelWidth + 4.0f, Y - TextSize.y * 0.5f), AxisTextColor, LabelBuffer);
			LastValueLabelY = Y;
		}
	}
	DrawList->AddLine(ImVec2(GraphMin.x, GraphMin.y), ImVec2(GraphMin.x, GraphMax.y), AxisColor, 1.0f);
	DrawList->AddLine(ImVec2(GraphMin.x, GraphMax.y), ImVec2(GraphMax.x, GraphMax.y), AxisColor, 1.0f);

	auto TimeToX = [&](float Time)
	{
		const float Alpha = (Time - ViewTimeMin) / (ViewTimeMax - ViewTimeMin);
		return GraphMin.x + Alpha * (GraphMax.x - GraphMin.x);
	};
	auto ValueToY = [&](float Value)
	{
		const float Alpha = (Value - ViewValueMin) / (ViewValueMax - ViewValueMin);
		return GraphMax.y - Alpha * (GraphMax.y - GraphMin.y);
	};
	auto XToTime = [&](float X)
	{
		const float Alpha = std::clamp((X - GraphMin.x) / (GraphMax.x - GraphMin.x), 0.0f, 1.0f);
		return std::clamp(ViewTimeMin + Alpha * (ViewTimeMax - ViewTimeMin), 0.0f, 1.0f);
	};
	auto YToValue = [&](float Y)
	{
		const float Alpha = std::clamp((GraphMax.y - Y) / (GraphMax.y - GraphMin.y), 0.0f, 1.0f);
		return ViewValueMin + Alpha * (ViewValueMax - ViewValueMin);
	};

	for (int32 TrackIndex = 0; TrackIndex < static_cast<int32>(Tracks.size()); ++TrackIndex)
	{
		const float RowY = Origin.y + 10.0f + TrackIndex * 24.0f;
		const ImVec2 RowMin(Origin.x, RowY - 4.0f);
		const ImVec2 RowMax(Origin.x + TrackWidth, RowY + 18.0f);
		if (TrackIndex == SelectedTrack)
		{
			DrawList->AddRectFilled(RowMin, RowMax, IM_COL32(70, 70, 70, 255));
		}
		DrawList->AddText(ImVec2(Origin.x + 10.0f, RowY), Tracks[TrackIndex].Color, Tracks[TrackIndex].Label.c_str());
		if (MousePos.x >= RowMin.x && MousePos.x <= RowMax.x && MousePos.y >= RowMin.y && MousePos.y <= RowMax.y && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			SelectedTrack = TrackIndex;
			SelectedKey = -1;
		}
	}

	auto IsMouseNearGraphPoint = [&](const ImVec2& Point, float Radius)
	{
		const float DeltaX = MousePos.x - Point.x;
		const float DeltaY = MousePos.y - Point.y;
		return DeltaX * DeltaX + DeltaY * DeltaY <= Radius * Radius;
	};

	bool bMouseOnCurveHandle = false;
	if (FFloatCurve* HitTestCurve = GetTrackCurveIfAlreadyCurve(Tracks[SelectedTrack]))
	{
		for (const FCurveKey& Key : HitTestCurve->Keys)
		{
			const ImVec2 KeyPoint(TimeToX(Key.Time), ValueToY(Key.Value));
			if (KeyPoint.x >= GraphMin.x - 8.0f && KeyPoint.x <= GraphMax.x + 8.0f &&
				KeyPoint.y >= GraphMin.y - 8.0f && KeyPoint.y <= GraphMax.y + 8.0f &&
				IsMouseNearGraphPoint(KeyPoint, 9.0f))
			{
				bMouseOnCurveHandle = true;
				break;
			}
		}
	}
	else
	{
		const ImVec2 HandlePoint(TimeToX(0.5f), ValueToY(EvaluateTrackValue(Tracks[SelectedTrack], 0.5f)));
		bMouseOnCurveHandle =
			HandlePoint.x >= GraphMin.x - 8.0f && HandlePoint.x <= GraphMax.x + 8.0f &&
			HandlePoint.y >= GraphMin.y - 8.0f && HandlePoint.y <= GraphMax.y + 8.0f &&
			IsMouseNearGraphPoint(HandlePoint, 10.0f);
	}

	bool bGraphBackgroundHovered = false;
	bool bGraphBackgroundActive = false;
	if (!bMouseOnCurveHandle)
	{
		ImGui::SetCursorScreenPos(GraphMin);
		ImGui::InvisibleButton(
			"##CurveGraphBackground",
			ImVec2(GraphMax.x - GraphMin.x, GraphMax.y - GraphMin.y),
			ImGuiButtonFlags_MouseButtonLeft);
		bGraphBackgroundHovered = ImGui::IsItemHovered();
		bGraphBackgroundActive = ImGui::IsItemActive();

		if (bGraphBackgroundHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			FFloatCurve* CurveToAddKey = GetEditableTrackCurve(Tracks[SelectedTrack]);
			if (!CurveToAddKey)
			{
				return bChanged;
			}
			const float NewTime = XToTime(MousePos.x);
			const float NewValue = YToValue(MousePos.y);
			CurveToAddKey->AddKey(NewTime, NewValue);
			CurveToAddKey->SortKeys();
			CurveToAddKey->AutoSetTangents();
			SelectedKey = FindNearestCurveKeyIndex(*CurveToAddKey, NewTime, NewValue);
			bChanged = true;
		}
		else if (bGraphBackgroundActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			const float TimeDelta = -IO.MouseDelta.x / (GraphMax.x - GraphMin.x) * (ViewTimeMax - ViewTimeMin);
			const float ValueDelta = IO.MouseDelta.y / (GraphMax.y - GraphMin.y) * (ViewValueMax - ViewValueMin);
			ViewTimeMin += TimeDelta;
			ViewTimeMax += TimeDelta;
			ViewValueMin += ValueDelta;
			ViewValueMax += ValueDelta;
			ClampCurveView();
			SelectedKey = -1;
		}
		else if (bGraphBackgroundHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			SelectedKey = -1;
		}
	}

	if (bGraphBackgroundActive)
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
	}

	DrawList->PushClipRect(GraphMin, GraphMax, true);
	for (int32 TrackIndex = 0; TrackIndex < static_cast<int32>(Tracks.size()); ++TrackIndex)
	{
		if (!Tracks[TrackIndex].Distribution)
		{
			continue;
		}

		const ImU32 CurveColor = TrackIndex == SelectedTrack ? Tracks[TrackIndex].Color : IM_COL32(150, 150, 150, 120);
		ImVec2 PrevPoint(TimeToX(0.0f), ValueToY(EvaluateTrackValue(Tracks[TrackIndex], 0.0f)));
		for (int32 SampleIndex = 1; SampleIndex <= 64; ++SampleIndex)
		{
			const float T = static_cast<float>(SampleIndex) / 64.0f;
			const ImVec2 Point(TimeToX(T), ValueToY(EvaluateTrackValue(Tracks[TrackIndex], T)));
			DrawList->AddLine(PrevPoint, Point, CurveColor, TrackIndex == SelectedTrack ? 2.0f : 1.0f);
			PrevPoint = Point;
		}
	}

	FFloatCurve* SelectedCurve = GetTrackCurveIfAlreadyCurve(Tracks[SelectedTrack]);
	if (SelectedCurve)
	{
		for (int32 KeyIndex = 0; KeyIndex < static_cast<int32>(SelectedCurve->Keys.size()); ++KeyIndex)
		{
			FCurveKey& Key = SelectedCurve->Keys[KeyIndex];
			const ImVec2 KeyPoint(TimeToX(Key.Time), ValueToY(Key.Value));
			if (KeyPoint.x < GraphMin.x - 8.0f || KeyPoint.x > GraphMax.x + 8.0f ||
				KeyPoint.y < GraphMin.y - 8.0f || KeyPoint.y > GraphMax.y + 8.0f)
			{
				continue;
			}

			const float Radius = KeyIndex == SelectedKey ? 5.0f : 4.0f;
			DrawList->AddCircleFilled(KeyPoint, Radius, Tracks[SelectedTrack].Color);

			ImGui::SetCursorScreenPos(ImVec2(KeyPoint.x - 6.0f, KeyPoint.y - 6.0f));
			ImGui::PushID(KeyIndex);
			ImGui::InvisibleButton("##CurveKey", ImVec2(12.0f, 12.0f));
			if (ImGui::IsItemClicked())
			{
				SelectedKey = KeyIndex;
			}
			if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			{
				const float NewTime = XToTime(KeyPoint.x + ImGui::GetIO().MouseDelta.x);
				const float NewValue = YToValue(KeyPoint.y + ImGui::GetIO().MouseDelta.y);
				Key.Time = NewTime;
				Key.Value = NewValue;
				SelectedCurve->SortKeys();
				SelectedCurve->AutoSetTangents();
				SelectedKey = FindNearestCurveKeyIndex(*SelectedCurve, NewTime, NewValue);
				bChanged = true;
			}
			ImGui::PopID();
		}
	}
	else
	{
		const float HandleTime = 0.5f;
		const float HandleValue = EvaluateTrackValue(Tracks[SelectedTrack], HandleTime);
		const ImVec2 KeyPoint(TimeToX(HandleTime), ValueToY(HandleValue));
		if (KeyPoint.x >= GraphMin.x - 8.0f && KeyPoint.x <= GraphMax.x + 8.0f &&
			KeyPoint.y >= GraphMin.y - 8.0f && KeyPoint.y <= GraphMax.y + 8.0f)
		{
			DrawList->AddCircleFilled(KeyPoint, 4.5f, Tracks[SelectedTrack].Color);

			ImGui::SetCursorScreenPos(ImVec2(KeyPoint.x - 7.0f, KeyPoint.y - 7.0f));
			ImGui::PushID("ConstantDistributionHandle");
			ImGui::InvisibleButton("##CurveConstantHandle", ImVec2(14.0f, 14.0f));
			if (ImGui::IsItemClicked())
			{
				SelectedKey = 0;
			}
			if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			{
				SetTrackConstantValue(Tracks[SelectedTrack], YToValue(KeyPoint.y + IO.MouseDelta.y));
				bChanged = true;
			}
			ImGui::PopID();
		}
	}
	DrawList->PopClipRect();

	ImGui::SetCursorScreenPos(Origin);
	ImGui::Dummy(Size);

	SelectedCurve = GetTrackCurveIfAlreadyCurve(Tracks[SelectedTrack]);
	if (SelectedCurve && SelectedKey >= 0 && SelectedKey < static_cast<int32>(SelectedCurve->Keys.size()))
	{
		FCurveKey& Key = SelectedCurve->Keys[SelectedKey];
		ImGui::SetNextItemWidth(90.0f);
		if (ImGui::DragFloat("Time", &Key.Time, 0.005f, 0.0f, 1.0f, "%.3f"))
		{
			Key.Time = std::clamp(Key.Time, 0.0f, 1.0f);
			const float NewTime = Key.Time;
			const float NewValue = Key.Value;
			SelectedCurve->SortKeys();
			SelectedCurve->AutoSetTangents();
			SelectedKey = FindNearestCurveKeyIndex(*SelectedCurve, NewTime, NewValue);
			bChanged = true;
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(110.0f);
		if (ImGui::DragFloat("Value", &Key.Value, 0.01f))
		{
			SelectedCurve->AutoSetTangents();
			bChanged = true;
		}
	}

	if (bChanged)
	{
		SyncModuleLegacyFromDistributions(Module);
	}
	return bChanged;
}

bool FParticleEditorWidget::RenderDetailsPanel()
{
	if (bParticleSystemSelected)
	{
		return RenderParticleSystemDetails();
	}

	UParticleEmitter* Emitter = GetSelectedEmitter();
	UParticleModule* Module = GetSelectedModule();
	SelectedModule = Module;
	if (!Emitter || !Module)
	{
		ImGui::TextDisabled("Select a module.");
		return false;
	}

	bool bChanged = false;
	if (EmitterNameBufferIndex != SelectedEmitterIndex || EmitterNameBufferEmitter != Emitter)
	{
		SyncEmitterNameBuffer();
	}

	ImGui::TextUnformatted("Emitter");
	ImGui::SetNextItemWidth(220.0f);
	if (ImGui::InputText("Name", EmitterNameBuffer, sizeof(EmitterNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
	{
		bChanged |= CommitEmitterNameEdit();
	}
	if (ImGui::IsItemDeactivatedAfterEdit())
	{
		bChanged |= CommitEmitterNameEdit();
	}

	const FString PendingName = EmitterNameBuffer;
	const bool bDuplicateName = !PendingName.empty()
		&& !IsEmitterNameAvailable(PendingName, SelectedEmitterIndex)
		&& Emitter->GetEmitterName().ToString() != PendingName;
	if (bDuplicateName)
	{
		ImGui::TextDisabled("Emitter name must be unique.");
	}

	ImGui::Separator();
	ImGui::TextUnformatted(GetModuleDisplayName(Module).c_str());
	ImGui::Separator();

	bChanged |= RenderModuleDetails(Module);

	return bChanged;
}

bool FParticleEditorWidget::RenderParticleSystemDetails()
{
	if (!EditingParticleSystem)
	{
		ImGui::TextDisabled("No particle system.");
		return false;
	}

	EditingParticleSystem->NormalizeLODData();

	bool bChanged = false;
	ImGui::TextUnformatted("Particle System");
	ImGui::TextDisabled("%s", EditingParticleSystem->GetName().c_str());
	ImGui::Separator();

	if (ImGui::CollapsingHeader("LOD", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const int32 LODCount = EditingParticleSystem->GetLODCount();
		ImGui::TextDisabled("%d LOD distance%s", LODCount, LODCount == 1 ? "" : "s");

		for (int32 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
		{
			ImGui::PushID(LODIndex);
			float Distance = EditingParticleSystem->GetLODDistance(LODIndex);
			char Label[64] = {};
			std::snprintf(Label, sizeof(Label), "LOD Distance %d", LODIndex);
			if (ImGui::DragFloat(Label, &Distance, 1.0f, 0.0f, 0.0f, "%.2f"))
			{
				bChanged |= EditingParticleSystem->SetLODDistance(LODIndex, Distance);
				SelectedLODIndex = ClampLODIndex(SelectedLODIndex);
			}
			ImGui::PopID();
		}
	}

	return bChanged;
}

bool FParticleEditorWidget::RenderModuleDetails(UParticleModule* Module)
{
	if (!Module)
	{
		return false;
	}

	if (UParticleModuleRequired* Required = Cast<UParticleModuleRequired>(Module))
	{
		return RenderRequiredDetails(Required);
	}

	bool bChanged = false;
	bool bEnabled = Module->bEnabled != 0;
	if (ImGui::Checkbox("Enabled", &bEnabled))
	{
		Module->bEnabled = bEnabled;
		bChanged = true;
	}

	if (UParticleModuleSpawn* Spawn = Cast<UParticleModuleSpawn>(Module))
	{
		float Rate = Spawn->Rate;
		if (ImGui::DragFloat("Rate", &Rate, 1.0f, 0.0f, 10000.0f))
		{
			Spawn->Rate = (std::max)(0.0f, Rate);
			if (!Spawn->RateDistribution.UsesCurve())
			{
				Spawn->RateDistribution.SetConstant(Spawn->Rate);
			}
			bChanged = true;
		}

		if (RenderFloatDistributionControls("Rate Distribution", Spawn->RateDistribution, 1.0f, 0.0f, 10000.0f))
		{
			SyncModuleLegacyFromDistributions(Module);
			bChanged = true;
		}
	}
	else if (UParticleModuleLifetime* Lifetime = Cast<UParticleModuleLifetime>(Module))
	{
		float LifetimeMin = Lifetime->LifetimeMin;
		if (ImGui::DragFloat("Lifetime Min", &LifetimeMin, 0.05f, 0.0f, 1000.0f))
		{
			Lifetime->LifetimeMin = (std::max)(0.0f, LifetimeMin);
			Lifetime->Lifetime = Lifetime->LifetimeMax;
			if (!Lifetime->LifetimeDistribution.UsesCurve())
			{
				Lifetime->LifetimeDistribution.SetUniform(Lifetime->LifetimeMin, Lifetime->LifetimeMax);
			}
			bChanged = true;
		}

		float LifetimeMax = Lifetime->LifetimeMax;
		if (ImGui::DragFloat("Lifetime Max", &LifetimeMax, 0.05f, 0.0f, 1000.0f))
		{
			Lifetime->LifetimeMax = (std::max)(0.0f, LifetimeMax);
			Lifetime->Lifetime = Lifetime->LifetimeMax;
			if (!Lifetime->LifetimeDistribution.UsesCurve())
			{
				Lifetime->LifetimeDistribution.SetUniform(Lifetime->LifetimeMin, Lifetime->LifetimeMax);
			}
			bChanged = true;
		}

		if (RenderFloatDistributionControls("Lifetime Distribution", Lifetime->LifetimeDistribution, 0.05f, 0.0f, 1000.0f))
		{
			SyncModuleLegacyFromDistributions(Module);
			bChanged = true;
		}
	}
	else if (UParticleModuleSize* Size = Cast<UParticleModuleSize>(Module))
	{
		FVector StartSizeMin = Size->StartSizeMin;
		if (ImGui::DragFloat3("Start Size Min", &StartSizeMin.X, 0.25f, 0.0f, 10000.0f))
		{
			Size->StartSizeMin = StartSizeMin;
			Size->StartSize = Size->StartSizeMax;
			if (!Size->StartSizeDistribution.UsesCurve())
			{
				Size->StartSizeDistribution.SetUniform(Size->StartSizeMin, Size->StartSizeMax);
			}
			bChanged = true;
		}

		FVector StartSizeMax = Size->StartSizeMax;
		if (ImGui::DragFloat3("Start Size Max", &StartSizeMax.X, 0.25f, 0.0f, 10000.0f))
		{
			Size->StartSizeMax = StartSizeMax;
			Size->StartSize = Size->StartSizeMax;
			if (!Size->StartSizeDistribution.UsesCurve())
			{
				Size->StartSizeDistribution.SetUniform(Size->StartSizeMin, Size->StartSizeMax);
			}
			bChanged = true;
		}

		if (RenderVectorDistributionControls("Start Size Distribution", Size->StartSizeDistribution, 0.25f, 0.0f, 10000.0f))
		{
			SyncModuleLegacyFromDistributions(Module);
			bChanged = true;
		}
	}
	else if (UParticleModuleVelocity* Velocity = Cast<UParticleModuleVelocity>(Module))
	{
		FVector StartVelocityMin = Velocity->StartVelocityMin;
		if (ImGui::DragFloat3("Start Velocity Min", &StartVelocityMin.X, 0.5f, -10000.0f, 10000.0f))
		{
			Velocity->StartVelocityMin = StartVelocityMin;
			Velocity->StartVelocity = Velocity->StartVelocityMax;
			if (!Velocity->StartVelocityDistribution.UsesCurve())
			{
				Velocity->StartVelocityDistribution.SetUniform(Velocity->StartVelocityMin, Velocity->StartVelocityMax);
			}
			bChanged = true;
		}

		FVector StartVelocityMax = Velocity->StartVelocityMax;
		if (ImGui::DragFloat3("Start Velocity Max", &StartVelocityMax.X, 0.5f, -10000.0f, 10000.0f))
		{
			Velocity->StartVelocityMax = StartVelocityMax;
			Velocity->StartVelocity = Velocity->StartVelocityMax;
			if (!Velocity->StartVelocityDistribution.UsesCurve())
			{
				Velocity->StartVelocityDistribution.SetUniform(Velocity->StartVelocityMin, Velocity->StartVelocityMax);
			}
			bChanged = true;
		}

		if (RenderVectorDistributionControls("Start Velocity Distribution", Velocity->StartVelocityDistribution, 0.5f, -10000.0f, 10000.0f))
		{
			SyncModuleLegacyFromDistributions(Module);
			bChanged = true;
		}
	}
	else if (UParticleModuleInitialRotation* Rotation = Cast<UParticleModuleInitialRotation>(Module))
	{
		FVector StartRotationMin = Rotation->StartRotationDegreesMin;
		if (ImGui::DragFloat3("Start Rotation Min (deg)", &StartRotationMin.X, 1.0f, -36000.0f, 36000.0f))
		{
			Rotation->StartRotationDegreesMin = StartRotationMin;
			Rotation->StartRotationDegrees = Rotation->StartRotationDegreesMax;
			if (!Rotation->StartRotationDistribution.UsesCurve())
			{
				Rotation->StartRotationDistribution.SetUniform(Rotation->StartRotationDegreesMin, Rotation->StartRotationDegreesMax);
			}
			bChanged = true;
		}

		FVector StartRotationMax = Rotation->StartRotationDegreesMax;
		if (ImGui::DragFloat3("Start Rotation Max (deg)", &StartRotationMax.X, 1.0f, -36000.0f, 36000.0f))
		{
			Rotation->StartRotationDegreesMax = StartRotationMax;
			Rotation->StartRotationDegrees = Rotation->StartRotationDegreesMax;
			if (!Rotation->StartRotationDistribution.UsesCurve())
			{
				Rotation->StartRotationDistribution.SetUniform(Rotation->StartRotationDegreesMin, Rotation->StartRotationDegreesMax);
			}
			bChanged = true;
		}

		if (RenderVectorDistributionControls("Start Rotation Distribution", Rotation->StartRotationDistribution, 1.0f, -36000.0f, 36000.0f))
		{
			bChanged = true;
		}
	}
	else if (UParticleModuleInitialRotationRate* RotationRate = Cast<UParticleModuleInitialRotationRate>(Module))
	{
		FVector StartRotationRateMin = RotationRate->StartRotationRateDegreesMin;
		if (ImGui::DragFloat3("Start Rotation Rate Min (deg/s)", &StartRotationRateMin.X, 1.0f, -36000.0f, 36000.0f))
		{
			RotationRate->StartRotationRateDegreesMin = StartRotationRateMin;
			RotationRate->StartRotationRateDegrees = RotationRate->StartRotationRateDegreesMax;
			if (!RotationRate->StartRotationRateDistribution.UsesCurve())
			{
				RotationRate->StartRotationRateDistribution.SetUniform(RotationRate->StartRotationRateDegreesMin, RotationRate->StartRotationRateDegreesMax);
			}
			bChanged = true;
		}

		FVector StartRotationRateMax = RotationRate->StartRotationRateDegreesMax;
		if (ImGui::DragFloat3("Start Rotation Rate Max (deg/s)", &StartRotationRateMax.X, 1.0f, -36000.0f, 36000.0f))
		{
			RotationRate->StartRotationRateDegreesMax = StartRotationRateMax;
			RotationRate->StartRotationRateDegrees = RotationRate->StartRotationRateDegreesMax;
			if (!RotationRate->StartRotationRateDistribution.UsesCurve())
			{
				RotationRate->StartRotationRateDistribution.SetUniform(RotationRate->StartRotationRateDegreesMin, RotationRate->StartRotationRateDegreesMax);
			}
			bChanged = true;
		}

		if (RenderVectorDistributionControls("Start Rotation Rate Distribution", RotationRate->StartRotationRateDistribution, 1.0f, -36000.0f, 36000.0f))
		{
			bChanged = true;
		}
	}
	else if (UParticleModuleAcceleration* Acceleration = Cast<UParticleModuleAcceleration>(Module))
	{
		FVector AccelerationValue = Acceleration->Acceleration;
		if (ImGui::DragFloat3("Acceleration", &AccelerationValue.X, 0.5f, -10000.0f, 10000.0f))
		{
			Acceleration->Acceleration = AccelerationValue;
			if (!Acceleration->AccelerationDistribution.UsesCurve())
			{
				Acceleration->AccelerationDistribution.SetConstant(Acceleration->Acceleration);
			}
			bChanged = true;
		}

		if (RenderVectorDistributionControls("Acceleration Distribution", Acceleration->AccelerationDistribution, 0.5f, -10000.0f, 10000.0f))
		{
			SyncModuleLegacyFromDistributions(Module);
			bChanged = true;
		}
	}
	else if (UParticleModuleOrbit* Orbit = Cast<UParticleModuleOrbit>(Module))
	{
		FVector Offset = Orbit->Offset;
		if (ImGui::DragFloat3("Offset", &Offset.X, 0.25f, -100000.0f, 100000.0f))
		{
			Orbit->Offset = Offset;
			if (!Orbit->OffsetDistribution.UsesCurve())
			{
				Orbit->OffsetDistribution.SetConstant(Orbit->Offset);
			}
			bChanged = true;
		}
		if (RenderVectorDistributionControls("Offset Distribution", Orbit->OffsetDistribution, 0.25f, -100000.0f, 100000.0f))
		{
			SyncModuleLegacyFromDistributions(Module);
			bChanged = true;
		}

		FVector RotationDegrees = Orbit->RotationDegrees;
		if (ImGui::DragFloat3("Rotation (deg)", &RotationDegrees.X, 1.0f, -36000.0f, 36000.0f))
		{
			Orbit->RotationDegrees = RotationDegrees;
			if (!Orbit->RotationDistribution.UsesCurve())
			{
				Orbit->RotationDistribution.SetConstant(Orbit->RotationDegrees);
			}
			bChanged = true;
		}
		if (RenderVectorDistributionControls("Rotation Distribution", Orbit->RotationDistribution, 1.0f, -36000.0f, 36000.0f))
		{
			SyncModuleLegacyFromDistributions(Module);
			bChanged = true;
		}

		FVector RotationRateDegrees = Orbit->RotationRateDegrees;
		if (ImGui::DragFloat3("Rotation Rate (deg/s)", &RotationRateDegrees.X, 1.0f, -36000.0f, 36000.0f))
		{
			Orbit->RotationRateDegrees = RotationRateDegrees;
			if (!Orbit->RotationRateDistribution.UsesCurve())
			{
				Orbit->RotationRateDistribution.SetConstant(Orbit->RotationRateDegrees);
			}
			bChanged = true;
		}
		if (RenderVectorDistributionControls("Rotation Rate Distribution", Orbit->RotationRateDistribution, 1.0f, -36000.0f, 36000.0f))
		{
			SyncModuleLegacyFromDistributions(Module);
			bChanged = true;
		}
	}
	else if (UParticleModuleLocation* Location = Cast<UParticleModuleLocation>(Module))
	{
		FVector StartLocationMin = Location->StartLocationMin;
		if (ImGui::DragFloat3("Start Location Min", &StartLocationMin.X, 0.25f))
		{
			Location->StartLocationMin = StartLocationMin;
			Location->StartLocation = Location->StartLocationMax;
			if (!Location->StartLocationDistribution.UsesCurve())
			{
				Location->StartLocationDistribution.SetUniform(Location->StartLocationMin, Location->StartLocationMax);
			}
			bChanged = true;
		}

		FVector StartLocationMax = Location->StartLocationMax;
		if (ImGui::DragFloat3("Start Location Max", &StartLocationMax.X, 0.25f))
		{
			Location->StartLocationMax = StartLocationMax;
			Location->StartLocation = Location->StartLocationMax;
			if (!Location->StartLocationDistribution.UsesCurve())
			{
				Location->StartLocationDistribution.SetUniform(Location->StartLocationMin, Location->StartLocationMax);
			}
			bChanged = true;
		}

		if (RenderVectorDistributionControls("Start Location Distribution", Location->StartLocationDistribution, 0.25f, -10000.0f, 10000.0f))
		{
			SyncModuleLegacyFromDistributions(Module);
			bChanged = true;
		}
	}
	else if (UParticleModuleColor* Color = Cast<UParticleModuleColor>(Module))
	{
		float StartColorMin[3] = { Color->StartColorMin.X, Color->StartColorMin.Y, Color->StartColorMin.Z };
		if (ImGui::ColorEdit3("Start Color Min", StartColorMin))
		{
			Color->StartColorMin = FVector(StartColorMin[0], StartColorMin[1], StartColorMin[2]);
			Color->StartColor = Color->StartColorMax;
			if (!Color->StartColorDistribution.UsesCurve())
			{
				Color->StartColorDistribution.SetUniform(Color->StartColorMin, Color->StartColorMax);
			}
			bChanged = true;
		}

		float StartColorMax[3] = { Color->StartColorMax.X, Color->StartColorMax.Y, Color->StartColorMax.Z };
		if (ImGui::ColorEdit3("Start Color Max", StartColorMax))
		{
			Color->StartColorMax = FVector(StartColorMax[0], StartColorMax[1], StartColorMax[2]);
			Color->StartColor = Color->StartColorMax;
			if (!Color->StartColorDistribution.UsesCurve())
			{
				Color->StartColorDistribution.SetUniform(Color->StartColorMin, Color->StartColorMax);
			}
			bChanged = true;
		}

		float StartAlphaMin = Color->StartAlphaMin;
		if (ImGui::DragFloat("Start Alpha Min", &StartAlphaMin, 0.01f, 0.0f, 1.0f))
		{
			Color->StartAlphaMin = std::clamp(StartAlphaMin, 0.0f, 1.0f);
			Color->StartAlpha = Color->StartAlphaMax;
			if (!Color->StartAlphaDistribution.UsesCurve())
			{
				Color->StartAlphaDistribution.SetUniform(Color->StartAlphaMin, Color->StartAlphaMax);
			}
			bChanged = true;
		}

		float StartAlphaMax = Color->StartAlphaMax;
		if (ImGui::DragFloat("Start Alpha Max", &StartAlphaMax, 0.01f, 0.0f, 1.0f))
		{
			Color->StartAlphaMax = std::clamp(StartAlphaMax, 0.0f, 1.0f);
			Color->StartAlpha = Color->StartAlphaMax;
			if (!Color->StartAlphaDistribution.UsesCurve())
			{
				Color->StartAlphaDistribution.SetUniform(Color->StartAlphaMin, Color->StartAlphaMax);
			}
			bChanged = true;
		}

		if (RenderVectorDistributionControls("Start Color Distribution", Color->StartColorDistribution, 0.01f, 0.0f, 1.0f))
		{
			SyncModuleLegacyFromDistributions(Module);
			bChanged = true;
		}
		if (RenderFloatDistributionControls("Start Alpha Distribution", Color->StartAlphaDistribution, 0.01f, 0.0f, 1.0f))
		{
			SyncModuleLegacyFromDistributions(Module);
			bChanged = true;
		}
	}
	else if (UParticleModuleColorOverLife* ColorOverLife = Cast<UParticleModuleColorOverLife>(Module))
	{
		float EndColor[3] = { ColorOverLife->ColorOverLife.X, ColorOverLife->ColorOverLife.Y, ColorOverLife->ColorOverLife.Z };
		if (ImGui::ColorEdit3("Color Over Life", EndColor))
		{
			ColorOverLife->ColorOverLife = FVector(EndColor[0], EndColor[1], EndColor[2]);
			if (!ColorOverLife->ColorOverLifeDistribution.UsesCurve())
			{
				ColorOverLife->ColorOverLifeDistribution.SetConstant(ColorOverLife->ColorOverLife);
			}
			bChanged = true;
		}

		float EndAlpha = ColorOverLife->AlphaOverLife;
		if (ImGui::DragFloat("Alpha Over Life", &EndAlpha, 0.01f, 0.0f, 1.0f))
		{
			ColorOverLife->AlphaOverLife = std::clamp(EndAlpha, 0.0f, 1.0f);
			if (!ColorOverLife->AlphaOverLifeDistribution.UsesCurve())
			{
				ColorOverLife->AlphaOverLifeDistribution.SetConstant(ColorOverLife->AlphaOverLife);
			}
			bChanged = true;
		}

		if (RenderVectorDistributionControls("Color Over Life Distribution", ColorOverLife->ColorOverLifeDistribution, 0.01f, 0.0f, 1.0f))
		{
			ColorOverLife->ColorOverLife = ColorOverLife->ColorOverLifeDistribution.Evaluate(1.0f);
			bChanged = true;
		}
		if (RenderFloatDistributionControls("Alpha Over Life Distribution", ColorOverLife->AlphaOverLifeDistribution, 0.01f, 0.0f, 1.0f))
		{
			ColorOverLife->AlphaOverLife = std::clamp(ColorOverLife->AlphaOverLifeDistribution.Evaluate(1.0f), 0.0f, 1.0f);
			bChanged = true;
		}
	}
	else if (UParticleModuleColorScaleOverLife* ColorScale = Cast<UParticleModuleColorScaleOverLife>(Module))
	{
		FVector ScaleValue = ColorScale->ColorScaleOverLife;
		if (ImGui::DragFloat3("Color Scale Over Life", &ScaleValue.X, 0.01f, 0.0f, 10.0f))
		{
			ColorScale->ColorScaleOverLife = FVector(
				(std::max)(0.0f, ScaleValue.X),
				(std::max)(0.0f, ScaleValue.Y),
				(std::max)(0.0f, ScaleValue.Z));
			if (!ColorScale->ColorScaleOverLifeDistribution.UsesCurve())
			{
				ColorScale->ColorScaleOverLifeDistribution.SetConstant(ColorScale->ColorScaleOverLife);
			}
			bChanged = true;
		}

		float AlphaScale = ColorScale->AlphaScaleOverLife;
		if (ImGui::DragFloat("Alpha Scale Over Life", &AlphaScale, 0.01f, 0.0f, 10.0f))
		{
			ColorScale->AlphaScaleOverLife = (std::max)(0.0f, AlphaScale);
			if (!ColorScale->AlphaScaleOverLifeDistribution.UsesCurve())
			{
				ColorScale->AlphaScaleOverLifeDistribution.SetConstant(ColorScale->AlphaScaleOverLife);
			}
			bChanged = true;
		}

		if (RenderVectorDistributionControls("Color Scale Distribution", ColorScale->ColorScaleOverLifeDistribution, 0.01f, 0.0f, 10.0f))
		{
			ColorScale->ColorScaleOverLife = ColorScale->ColorScaleOverLifeDistribution.Evaluate(1.0f);
			bChanged = true;
		}
		if (RenderFloatDistributionControls("Alpha Scale Distribution", ColorScale->AlphaScaleOverLifeDistribution, 0.01f, 0.0f, 10.0f))
		{
			ColorScale->AlphaScaleOverLife = (std::max)(0.0f, ColorScale->AlphaScaleOverLifeDistribution.Evaluate(1.0f));
			bChanged = true;
		}
	}
	else if (UParticleModuleTypeDataMesh* MeshTypeData = Cast<UParticleModuleTypeDataMesh>(Module))
	{
		const FString CurrentPath = MeshTypeData->MeshPath;
		const FString PreviewLabel = CurrentPath.empty() ? "None" : CurrentPath.c_str();

		if (ImGui::BeginCombo("Static Mesh", PreviewLabel.c_str()))
		{
			const bool bSelectedNone = CurrentPath.empty();
			if (ImGui::Selectable("None", bSelectedNone))
			{
				MeshTypeData->MeshPath.clear();
				MeshTypeData->Mesh = nullptr;
				bChanged = true;
			}
			if (bSelectedNone)
			{
				ImGui::SetItemDefaultFocus();
			}

			const TArray<FMeshAssetListItem>& MeshFiles = FMeshManager::GetAvailableStaticMeshFiles();
			for (const FMeshAssetListItem& Item : MeshFiles)
			{
				const bool bSelected = CurrentPath == Item.FullPath;
				if (ImGui::Selectable(Item.DisplayName.c_str(), bSelected))
				{
					MeshTypeData->MeshPath = Item.FullPath;
					MeshTypeData->Mesh = LoadEditorStaticMesh(MeshTypeData->MeshPath);
					if (UParticleModuleRequired* Required = GetSelectedRequiredModule())
					{
						bChanged |= EnsureParticleMeshEmitterDefaults(Required);
					}
					UParticleEmitter* Emitter = GetSelectedEmitter();
					bChanged |= EnsureParticleMeshSizeDefaults(GetSelectedLODLevel(Emitter));
					bChanged = true;
				}

				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}

			ImGui::EndCombo();
		}

		if (!MeshTypeData->MeshPath.empty() && !MeshTypeData->Mesh)
		{
			ImGui::TextDisabled("Mesh path is set, but the mesh is not loaded.");
		}

		if (UParticleModuleRequired* Required = GetSelectedRequiredModule())
		{
			bChanged |= EnsureParticleMeshEmitterDefaults(Required);
		}
		UParticleEmitter* Emitter = GetSelectedEmitter();
		bChanged |= EnsureParticleMeshSizeDefaults(GetSelectedLODLevel(Emitter));
	}
	else if (UParticleModuleBeamSource* Source = Cast<UParticleModuleBeamSource>(Module))
	{
		int SourceMethod = static_cast<int>(Source->SourceMethod);
		if (ImGui::Combo("Source Method", &SourceMethod, GBeamSourceTargetMethodNames, IM_ARRAYSIZE(GBeamSourceTargetMethodNames)))
		{
			Source->SourceMethod = static_cast<EBeam2SourceTargetMethod>(std::clamp(SourceMethod, 0, static_cast<int>(PEB2STM_Actor)));
			bChanged = true;
		}

		bool bSourceAbsolute = Source->bSourceAbsolute;
		if (ImGui::Checkbox("Source Absolute", &bSourceAbsolute))
		{
			Source->bSourceAbsolute = bSourceAbsolute;
			bChanged = true;
		}

		bool bLockSource = Source->bLockSource;
		if (ImGui::Checkbox("Lock Source", &bLockSource))
		{
			Source->bLockSource = bLockSource;
			bChanged = true;
		}

		FVector SourcePoint = Source->Source;
		if (ImGui::DragFloat3("Source", &SourcePoint.X, 0.25f))
		{
			Source->Source = SourcePoint;
			bChanged = true;
		}

		bool bLockSourceTangent = Source->bLockSourceTangent;
		if (ImGui::Checkbox("Lock Source Tangent", &bLockSourceTangent))
		{
			Source->bLockSourceTangent = bLockSourceTangent;
			bChanged = true;
		}

		FVector SourceTangent = Source->SourceTangent;
		if (ImGui::DragFloat3("Source Tangent", &SourceTangent.X, 0.25f))
		{
			Source->SourceTangent = SourceTangent;
			bChanged = true;
		}

		float SourceStrength = Source->SourceStrength;
		if (ImGui::DragFloat("Source Strength", &SourceStrength, 0.05f, 0.0f, 1000.0f))
		{
			Source->SourceStrength = (std::max)(0.0f, SourceStrength);
			bChanged = true;
		}
	}
	else if (UParticleModuleBeamTarget* Target = Cast<UParticleModuleBeamTarget>(Module))
	{
		int TargetMethod = static_cast<int>(Target->TargetMethod);
		if (ImGui::Combo("Target Method", &TargetMethod, GBeamSourceTargetMethodNames, IM_ARRAYSIZE(GBeamSourceTargetMethodNames)))
		{
			Target->TargetMethod = static_cast<EBeam2SourceTargetMethod>(std::clamp(TargetMethod, 0, static_cast<int>(PEB2STM_Actor)));
			bChanged = true;
		}

		bool bTargetAbsolute = Target->bTargetAbsolute;
		if (ImGui::Checkbox("Target Absolute", &bTargetAbsolute))
		{
			Target->bTargetAbsolute = bTargetAbsolute;
			bChanged = true;
		}

		bool bLockTarget = Target->bLockTarget;
		if (ImGui::Checkbox("Lock Target", &bLockTarget))
		{
			Target->bLockTarget = bLockTarget;
			bChanged = true;
		}

		FVector TargetPoint = Target->Target;
		if (ImGui::DragFloat3("Target", &TargetPoint.X, 0.25f))
		{
			Target->Target = TargetPoint;
			bChanged = true;
		}

		bool bLockTargetTangent = Target->bLockTargetTangent;
		if (ImGui::Checkbox("Lock Target Tangent", &bLockTargetTangent))
		{
			Target->bLockTargetTangent = bLockTargetTangent;
			bChanged = true;
		}

		FVector TargetTangent = Target->TargetTangent;
		if (ImGui::DragFloat3("Target Tangent", &TargetTangent.X, 0.25f))
		{
			Target->TargetTangent = TargetTangent;
			bChanged = true;
		}

		float TargetStrength = Target->TargetStrength;
		if (ImGui::DragFloat("Target Strength", &TargetStrength, 0.05f, 0.0f, 1000.0f))
		{
			Target->TargetStrength = (std::max)(0.0f, TargetStrength);
			bChanged = true;
		}
	}
	else if (UParticleModuleBeamNoise* Noise = Cast<UParticleModuleBeamNoise>(Module))
	{
		int Frequency = Noise->Frequency;
		if (ImGui::SliderInt("Frequency", &Frequency, 0, 64))
		{
			Noise->Frequency = std::clamp(Frequency, 0, 64);
			bChanged = true;
		}

		float FrequencyDistance = Noise->FrequencyDistance;
		if (ImGui::DragFloat("Frequency Distance", &FrequencyDistance, 0.05f, 0.0f, 10000.0f))
		{
			Noise->FrequencyDistance = (std::max)(0.0f, FrequencyDistance);
			bChanged = true;
		}

		FVector NoiseRange = Noise->NoiseRange;
		if (ImGui::DragFloat3("Noise Range", &NoiseRange.X, 0.25f))
		{
			Noise->NoiseRange = NoiseRange;
			bChanged = true;
		}

		float NoiseSpeed = Noise->NoiseSpeed;
		if (ImGui::DragFloat("Noise Speed", &NoiseSpeed, 0.05f, 0.0f, 1000.0f))
		{
			Noise->NoiseSpeed = (std::max)(0.0f, NoiseSpeed);
			bChanged = true;
		}

		float NoiseLockTime = Noise->NoiseLockTime;
		if (ImGui::DragFloat("Noise Lock Time", &NoiseLockTime, 0.05f, 0.0f, 1000.0f))
		{
			Noise->NoiseLockTime = (std::max)(0.0f, NoiseLockTime);
			bChanged = true;
		}

		bool bTargetNoise = Noise->bTargetNoise;
		if (ImGui::Checkbox("Target Noise", &bTargetNoise))
		{
			Noise->bTargetNoise = bTargetNoise;
			bChanged = true;
		}
	}
	else if (UParticleModuleTypeDataBeam2* Beam = Cast<UParticleModuleTypeDataBeam2>(Module))
	{
		int BeamMethod = static_cast<int>(Beam->BeamMethod);
		if (ImGui::Combo("Method", &BeamMethod, GBeamMethodNames, IM_ARRAYSIZE(GBeamMethodNames)))
		{
			Beam->BeamMethod = static_cast<EBeam2Method>(std::clamp(BeamMethod, 0, static_cast<int>(PEB2M_MAX) - 1));
			bChanged = true;
		}

		int InterpolationPoints = Beam->InterpolationPoints;
		if (ImGui::DragInt("Interpolation Points", &InterpolationPoints, 1.0f, 0, 128))
		{
			Beam->InterpolationPoints = (std::max)(0, InterpolationPoints);
			bChanged = true;
		}

		int Sheets = Beam->Sheets;
		if (ImGui::DragInt("Sheets", &Sheets, 1.0f, 1, 16))
		{
			Beam->Sheets = (std::max)(1, Sheets);
			bChanged = true;
		}

		int MaxBeamCount = Beam->MaxBeamCount;
		if (ImGui::DragInt("Max Beam Count", &MaxBeamCount, 1.0f, 1, 64))
		{
			Beam->MaxBeamCount = (std::max)(1, MaxBeamCount);
			bChanged = true;
		}

		float Speed = Beam->Speed;
		if (ImGui::DragFloat("Speed", &Speed, 0.25f, 0.0f, 10000.0f))
		{
			Beam->Speed = (std::max)(0.0f, Speed);
			bChanged = true;
		}

		float Distance = Beam->Distance;
		if (ImGui::DragFloat("Distance", &Distance, 1.0f, 0.0f, 10000.0f))
		{
			Beam->Distance = (std::max)(0.0f, Distance);
			bChanged = true;
		}

		float Width = Beam->Width;
		if (ImGui::DragFloat("Width", &Width, 0.25f, 0.0f, 1000.0f))
		{
			Beam->Width = (std::max)(0.0f, Width);
			bChanged = true;
		}

		int TextureTile = Beam->TextureTile;
		if (ImGui::DragInt("Texture Tile", &TextureTile, 1.0f, 1, 256))
		{
			Beam->TextureTile = (std::max)(1, TextureTile);
			bChanged = true;
		}

		float TextureTileDistance = Beam->TextureTileDistance;
		if (ImGui::DragFloat("Texture Tile Distance", &TextureTileDistance, 1.0f, 0.0f, 10000.0f))
		{
			Beam->TextureTileDistance = (std::max)(0.0f, TextureTileDistance);
			bChanged = true;
		}

		float BeamColor[3] = { Beam->Color.X, Beam->Color.Y, Beam->Color.Z };
		if (ImGui::ColorEdit3("Color", BeamColor))
		{
			Beam->Color = FVector(BeamColor[0], BeamColor[1], BeamColor[2]);
			bChanged = true;
		}

		float Alpha = Beam->Alpha;
		if (ImGui::DragFloat("Alpha", &Alpha, 0.01f, 0.0f, 1.0f))
		{
			Beam->Alpha = std::clamp(Alpha, 0.0f, 1.0f);
			bChanged = true;
		}

		int TaperMethod = static_cast<int>(Beam->TaperMethod);
		if (ImGui::Combo("Taper Method", &TaperMethod, GBeamTaperMethodNames, IM_ARRAYSIZE(GBeamTaperMethodNames)))
		{
			Beam->TaperMethod = static_cast<EBeamTaperMethod>(std::clamp(TaperMethod, 0, static_cast<int>(PEBTM_MAX) - 1));
			bChanged = true;
		}

		float TaperFactor = Beam->TaperFactor;
		if (ImGui::DragFloat("Taper Factor", &TaperFactor, 0.01f, 0.0f, 1.0f))
		{
			Beam->TaperFactor = std::clamp(TaperFactor, 0.0f, 1.0f);
			bChanged = true;
		}

		float TaperScale = Beam->TaperScale;
		if (ImGui::DragFloat("Taper Scale", &TaperScale, 0.01f, 0.0f, 100.0f))
		{
			Beam->TaperScale = (std::max)(0.0f, TaperScale);
			bChanged = true;
		}

		bool bAlwaysOn = Beam->bAlwaysOn;
		if (ImGui::Checkbox("Always On", &bAlwaysOn))
		{
			Beam->bAlwaysOn = bAlwaysOn;
			bChanged = true;
		}
	}
	else if (UParticleModuleEventGenerator* EventGenerator = Cast<UParticleModuleEventGenerator>(Module))
	{
		if (ImGui::Button("Add Event"))
		{
			FParticleEvent_GenerateInfo EventInfo;
			EventInfo.Type = EPET_Collision;
			EventInfo.CustomName = FName("Collision");
			EventGenerator->Events.push_back(EventInfo);
			bChanged = true;
		}

		for (int32 EventIndex = 0; EventIndex < static_cast<int32>(EventGenerator->Events.size()); ++EventIndex)
		{
			FParticleEvent_GenerateInfo& EventInfo = EventGenerator->Events[EventIndex];
			ImGui::PushID(EventIndex);
			if (ImGui::TreeNodeEx("Event", ImGuiTreeNodeFlags_DefaultOpen))
			{
				int EventType = static_cast<int>(EventInfo.Type);
				if (ImGui::Combo("Type", &EventType, GParticleEventTypeNames, IM_ARRAYSIZE(GParticleEventTypeNames)))
				{
					EventInfo.Type = static_cast<EParticleEventType>(std::clamp(EventType, 0, static_cast<int>(EPET_Blueprint)));
					bChanged = true;
				}

				char NameBuffer[128] = {};
				const FString CurrentName = EventInfo.CustomName.ToString();
				std::snprintf(NameBuffer, sizeof(NameBuffer), "%s", CurrentName.c_str());
				if (ImGui::InputText("Custom Name", NameBuffer, sizeof(NameBuffer)))
				{
					EventInfo.CustomName = FName(NameBuffer);
					bChanged = true;
				}

				int Frequency = EventInfo.Frequency;
				if (ImGui::DragInt("Frequency", &Frequency, 1.0f, 0, 1000))
				{
					EventInfo.Frequency = (std::max)(0, Frequency);
					bChanged = true;
				}

				int ParticleFrequency = EventInfo.ParticleFrequency;
				if (ImGui::DragInt("Particle Frequency", &ParticleFrequency, 1.0f, 0, 1000))
				{
					EventInfo.ParticleFrequency = (std::max)(0, ParticleFrequency);
					bChanged = true;
				}

				if (ImGui::Checkbox("First Time Only", &EventInfo.FirstTimeOnly))
				{
					bChanged = true;
				}
				if (ImGui::Checkbox("Last Time Only", &EventInfo.LastTimeOnly))
				{
					bChanged = true;
				}
				if (ImGui::Checkbox("Use Reflected Impact Vector", &EventInfo.UseReflectedImpactVector))
				{
					bChanged = true;
				}
				if (ImGui::Checkbox("Use Orbit Offset", &EventInfo.bUseOrbitOffset))
				{
					bChanged = true;
				}

				if (ImGui::Button("Remove Event"))
				{
					EventGenerator->Events.erase(EventGenerator->Events.begin() + EventIndex);
					bChanged = true;
					ImGui::TreePop();
					ImGui::PopID();
					break;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
	}
	else if (UParticleModuleEventReceiverSpawn* EventReceiver = Cast<UParticleModuleEventReceiverSpawn>(Module))
	{
		int EventType = static_cast<int>(EventReceiver->EventGeneratorType);
		if (ImGui::Combo("Event Type", &EventType, GParticleEventTypeNames, IM_ARRAYSIZE(GParticleEventTypeNames)))
		{
			EventReceiver->EventGeneratorType = static_cast<EParticleEventType>(std::clamp(EventType, 0, static_cast<int>(EPET_Blueprint)));
			bChanged = true;
		}

		char NameBuffer[128] = {};
		const FString CurrentName = EventReceiver->EventName.ToString();
		std::snprintf(NameBuffer, sizeof(NameBuffer), "%s", CurrentName.c_str());
		if (ImGui::InputText("Event Name", NameBuffer, sizeof(NameBuffer)))
		{
			EventReceiver->EventName = FName(NameBuffer);
			bChanged = true;
		}

		int SpawnCount = EventReceiver->SpawnCount;
		if (ImGui::DragInt("Spawn Count", &SpawnCount, 1.0f, 0, 1024))
		{
			EventReceiver->SpawnCount = std::clamp(SpawnCount, 0, 1024);
			bChanged = true;
		}

		if (ImGui::Checkbox("Spawn Only On Event", &EventReceiver->bSpawnOnlyOnEvent))
		{
			bChanged = true;
		}

		FVector SpawnLocationOffset = EventReceiver->SpawnLocationOffset;
		if (ImGui::DragFloat3("Location Offset", &SpawnLocationOffset.X, 0.25f, -100000.0f, 100000.0f))
		{
			EventReceiver->SpawnLocationOffset = SpawnLocationOffset;
			bChanged = true;
		}

		if (ImGui::Checkbox("Inherit Event Velocity", &EventReceiver->bInheritEventVelocity))
		{
			bChanged = true;
		}

		float EventVelocityScale = EventReceiver->EventVelocityScale;
		if (ImGui::DragFloat("Event Velocity Scale", &EventVelocityScale, 0.01f, 0.0f, 100.0f))
		{
			EventReceiver->EventVelocityScale = (std::max)(0.0f, EventVelocityScale);
			bChanged = true;
		}
	}
	else if (UParticleModuleCollision* Collision = Cast<UParticleModuleCollision>(Module))
	{
		int TraceChannel = static_cast<int>(Collision->TraceChannel);
		if (ImGui::Combo("Trace Channel", &TraceChannel, GCollisionChannelNames, IM_ARRAYSIZE(GCollisionChannelNames)))
		{
			Collision->TraceChannel = static_cast<ECollisionChannel>(std::clamp(TraceChannel, 0, NumActiveCollisionChannels - 1));
			bChanged = true;
		}

		int ResponseMode = static_cast<int>(Collision->ResponseMode);
		if (ImGui::Combo("Response Mode", &ResponseMode, GParticleCollisionResponseNames, IM_ARRAYSIZE(GParticleCollisionResponseNames)))
		{
			ResponseMode = std::clamp(ResponseMode, 0, static_cast<int>(IM_ARRAYSIZE(GParticleCollisionResponseNames)) - 1);
			Collision->ResponseMode = static_cast<EParticleCollisionResponseMode>(ResponseMode);
			bChanged = true;
		}

		float DampingFactor = Collision->DampingFactor;
		if (ImGui::DragFloat("Damping Factor", &DampingFactor, 0.01f, 0.0f, 1.0f))
		{
			Collision->DampingFactor = std::clamp(DampingFactor, 0.0f, 1.0f);
			bChanged = true;
		}

		float CollisionOffset = Collision->CollisionOffset;
		if (ImGui::DragFloat("Collision Offset", &CollisionOffset, 0.01f, 0.0f, 100.0f))
		{
			Collision->CollisionOffset = (std::max)(0.0f, CollisionOffset);
			bChanged = true;
		}

		float CollisionRadiusScale = Collision->CollisionRadiusScale;
		if (ImGui::DragFloat("Collision Radius Scale", &CollisionRadiusScale, 0.05f, 0.0f, 10.0f))
		{
			Collision->CollisionRadiusScale = (std::max)(0.0f, CollisionRadiusScale);
			bChanged = true;
		}

		int MaxCollisions = Collision->MaxCollisions;
		if (ImGui::DragInt("Max Collisions", &MaxCollisions, 1.0f, 0, 128))
		{
			Collision->MaxCollisions = (std::max)(0, MaxCollisions);
			bChanged = true;
		}
	}
	else if (UParticleModuleTypeDataRibbon* Ribbon = Cast<UParticleModuleTypeDataRibbon>(Module))
	{
		if (ImGui::TreeNodeEx("Source", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::Checkbox("Use Source Emitter", &Ribbon->bUseSourceEmitter))
			{
				bChanged = true;
			}

			const bool bHasExplicitSource = Ribbon->SourceEmitterName.IsValid()
				&& Ribbon->SourceEmitterName != FName::None;
			const FString SourcePreview = bHasExplicitSource
				? Ribbon->SourceEmitterName.ToString()
				: FString("Auto");

			if (ImGui::BeginCombo("Source Emitter", SourcePreview.c_str()))
			{
				const bool bAutoSelected = !bHasExplicitSource;
				if (ImGui::Selectable("Auto", bAutoSelected))
				{
					Ribbon->SourceEmitterName = FName::None;
					bChanged = true;
				}
				if (bAutoSelected)
				{
					ImGui::SetItemDefaultFocus();
				}

				if (EditingParticleSystem)
				{
					for (int32 EmitterIndex = 0; EmitterIndex < static_cast<int32>(EditingParticleSystem->Emitters.size()); ++EmitterIndex)
					{
						if (EmitterIndex == SelectedEmitterIndex)
						{
							continue;
						}

						UParticleEmitter* CandidateEmitter = EditingParticleSystem->Emitters[EmitterIndex];
						UParticleLODLevel* CandidateLOD = GetSelectedLODLevel(CandidateEmitter);
						if (!CandidateEmitter || !CandidateLOD || CandidateLOD->TypeDataModule)
						{
							continue;
						}

						const FName& CandidateName = CandidateEmitter->GetEmitterName();
						const FString CandidateLabel = GetEmitterDisplayName(CandidateEmitter, EmitterIndex);
						const bool bSelected = bHasExplicitSource && Ribbon->SourceEmitterName == CandidateName;
						if (ImGui::Selectable(CandidateLabel.c_str(), bSelected))
						{
							Ribbon->SourceEmitterName = CandidateName;
							bChanged = true;
						}
						if (bSelected)
						{
							ImGui::SetItemDefaultFocus();
						}
					}
				}
				ImGui::EndCombo();
			}

			float SourceTrailLifetime = Ribbon->SourceTrailLifetime;
			if (ImGui::DragFloat("Trail Lifetime", &SourceTrailLifetime, 0.01f, 0.001f, 10.0f))
			{
				Ribbon->SourceTrailLifetime = (std::max)(0.001f, SourceTrailLifetime);
				bChanged = true;
			}

			float SourceSampleInterval = Ribbon->SourceSampleInterval;
			if (ImGui::DragFloat("Sample Interval", &SourceSampleInterval, 0.001f, 0.0f, 1.0f, "%.3f"))
			{
				Ribbon->SourceSampleInterval = (std::max)(0.0f, SourceSampleInterval);
				bChanged = true;
			}

			float SourceMinSampleDistance = Ribbon->SourceMinSampleDistance;
			if (ImGui::DragFloat("Min Sample Distance", &SourceMinSampleDistance, 0.1f, 0.0f, 1000.0f))
			{
				Ribbon->SourceMinSampleDistance = (std::max)(0.0f, SourceMinSampleDistance);
				bChanged = true;
			}

			float SourceWidthScale = Ribbon->SourceWidthScale;
			if (ImGui::DragFloat("Source Width Scale", &SourceWidthScale, 0.01f, 0.0f, 100.0f))
			{
				Ribbon->SourceWidthScale = (std::max)(0.0f, SourceWidthScale);
				bChanged = true;
			}

			ImGui::TreePop();
		}

		int MaxTessellationBetweenParticles = Ribbon->MaxTessellationBetweenParticles;
		if (ImGui::DragInt("Max Tessellation Between Particles", &MaxTessellationBetweenParticles, 1.0f, 0, 32))
		{
			Ribbon->MaxTessellationBetweenParticles = std::clamp(MaxTessellationBetweenParticles, 0, 32);
			bChanged = true;
		}

		int SheetsPerTrail = Ribbon->SheetsPerTrail;
		if (ImGui::DragInt("Sheets Per Trail", &SheetsPerTrail, 1.0f, 1, 16))
		{
			Ribbon->SheetsPerTrail = std::clamp(SheetsPerTrail, 1, 16);
			bChanged = true;
		}

		int MaxTrailCount = Ribbon->MaxTrailCount;
		if (ImGui::DragInt("Max Trail Count", &MaxTrailCount, 1.0f, 1, 512))
		{
			Ribbon->MaxTrailCount = std::clamp(MaxTrailCount, 1, 512);
			bChanged = true;
		}

		int MaxParticleInTrailCount = Ribbon->MaxParticleInTrailCount;
		if (ImGui::DragInt("Max Particles In Trail", &MaxParticleInTrailCount, 1.0f, 2, 1024))
		{
			Ribbon->MaxParticleInTrailCount = std::clamp(MaxParticleInTrailCount, 2, 1024);
			bChanged = true;
		}

		int RenderAxis = static_cast<int>(Ribbon->RenderAxis);
		if (ImGui::Combo("Render Axis", &RenderAxis, GTrailRenderAxisNames, IM_ARRAYSIZE(GTrailRenderAxisNames)))
		{
			Ribbon->RenderAxis = static_cast<ETrailsRenderAxisOption>(std::clamp(RenderAxis, 0, static_cast<int>(Trails_MAX) - 1));
			bChanged = true;
		}

		if (ImGui::Checkbox("Spawn Initial Particle", &Ribbon->bSpawnInitialParticle))
		{
			bChanged = true;
		}

		if (ImGui::Checkbox("Dead Trails On Deactivate", &Ribbon->bDeadTrailsOnDeactivate))
		{
			bChanged = true;
		}

		if (ImGui::Checkbox("Dead Trails On Source Loss", &Ribbon->bDeadTrailsOnSourceLoss))
		{
			bChanged = true;
		}

		if (ImGui::Checkbox("Clip Source Segment", &Ribbon->bClipSourceSegment))
		{
			bChanged = true;
		}

		if (ImGui::Checkbox("Previous Tangent Recalculation", &Ribbon->bEnablePreviousTangentRecalculation))
		{
			bChanged = true;
		}

		if (ImGui::Checkbox("Tangent Recalculation Every Frame", &Ribbon->bTangentRecalculationEveryFrame))
		{
			bChanged = true;
		}

		float TangentSpawningScalar = Ribbon->TangentSpawningScalar;
		if (ImGui::DragFloat("Tangent Spawning Scalar", &TangentSpawningScalar, 0.1f, 0.0f, 1000.0f))
		{
			Ribbon->TangentSpawningScalar = (std::max)(0.0f, TangentSpawningScalar);
			bChanged = true;
		}

		if (ImGui::Checkbox("Render Geometry", &Ribbon->bRenderGeometry))
		{
			bChanged = true;
		}

		if (ImGui::Checkbox("Render Spawn Points", &Ribbon->bRenderSpawnPoints))
		{
			bChanged = true;
		}

		if (ImGui::Checkbox("Render Tangents", &Ribbon->bRenderTangents))
		{
			bChanged = true;
		}

		if (ImGui::Checkbox("Render Tessellation", &Ribbon->bRenderTessellation))
		{
			bChanged = true;
		}

		if (ImGui::Checkbox("Tangent Diff Interp Scale", &Ribbon->bEnableTangentDiffInterpScale))
		{
			bChanged = true;
		}

		float TilingDistance = Ribbon->TilingDistance;
		if (ImGui::DragFloat("Tiling Distance", &TilingDistance, 1.0f, 0.0f, 10000.0f))
		{
			Ribbon->TilingDistance = (std::max)(0.0f, TilingDistance);
			bChanged = true;
		}

		float DistanceTessellationStepSize = Ribbon->DistanceTessellationStepSize;
		if (ImGui::DragFloat("Distance Tessellation Step", &DistanceTessellationStepSize, 1.0f, 0.0f, 10000.0f))
		{
			Ribbon->DistanceTessellationStepSize = (std::max)(0.0f, DistanceTessellationStepSize);
			bChanged = true;
		}

		float TangentTessellationScalar = Ribbon->TangentTessellationScalar;
		if (ImGui::DragFloat("Tangent Tessellation Scalar", &TangentTessellationScalar, 0.1f, 0.0f, 1000.0f))
		{
			Ribbon->TangentTessellationScalar = (std::max)(0.0f, TangentTessellationScalar);
			bChanged = true;
		}

		float Width = Ribbon->Width;
		if (ImGui::DragFloat("Width", &Width, 0.25f, 0.0f, 1000.0f))
		{
			Ribbon->Width = (std::max)(0.0f, Width);
			bChanged = true;
		}

		float RibbonColor[3] = { Ribbon->Color.X, Ribbon->Color.Y, Ribbon->Color.Z };
		if (ImGui::ColorEdit3("Color", RibbonColor))
		{
			Ribbon->Color = FVector(RibbonColor[0], RibbonColor[1], RibbonColor[2]);
			bChanged = true;
		}

		float Alpha = Ribbon->Alpha;
		if (ImGui::DragFloat("Alpha", &Alpha, 0.01f, 0.0f, 1.0f))
		{
			Ribbon->Alpha = std::clamp(Alpha, 0.0f, 1.0f);
			bChanged = true;
		}
	}
	else
	{
		ImGui::TextDisabled("No editable fields for this module yet.");
	}

	return bChanged;
}

bool FParticleEditorWidget::RenderRequiredDetails(UParticleModuleRequired* Required)
{
	bool bChanged = false;

	const FString CurrentMaterialPath = GetMaterialPath(Required->Material);
	const char* PreviewLabel = CurrentMaterialPath.empty() ? "None" : CurrentMaterialPath.c_str();
	if (ImGui::BeginCombo("Material", PreviewLabel))
	{
		const TArray<FMaterialAssetListItem>& Materials = FMaterialManager::Get().GetAvailableMaterialFiles();
		for (const FMaterialAssetListItem& Item : Materials)
		{
			const bool bSelected = CurrentMaterialPath == Item.FullPath;
			if (ImGui::Selectable(Item.DisplayName.c_str(), bSelected))
			{
				if (UMaterial* NewMaterial = FMaterialManager::Get().GetOrCreateMaterial(Item.FullPath))
				{
					Required->Material = NewMaterial;
					bChanged = true;
				}
			}
		}
		ImGui::EndCombo();
	}
	if (UMaterial* DroppedMaterial = AcceptMaterialDrop())
	{
		Required->Material = DroppedMaterial;
		bChanged = true;
	}

	int Alignment = static_cast<int>(Required->ScreenAlignment);
	if (ImGui::Combo("Screen Alignment", &Alignment, GScreenAlignmentNames, IM_ARRAYSIZE(GScreenAlignmentNames)))
	{
		Required->ScreenAlignment = static_cast<EParticleScreenAlignment>(std::clamp(Alignment, 0, static_cast<int>(PSA_MAX) - 1));
		bChanged = true;
	}

	int SortMode = static_cast<int>(Required->SortMode);
	if (ImGui::Combo("Sort Mode", &SortMode, GSortModeNames, IM_ARRAYSIZE(GSortModeNames)))
	{
		Required->SortMode = static_cast<EParticleSortMode>(std::clamp(SortMode, 0, static_cast<int>(PSORTMODE_MAX) - 1));
		bChanged = true;
	}

	UMaterial* RequiredMaterial = Required->Material ? Required->Material->GetMaterial() : nullptr;
	const FMaterialParticleSettings* MaterialParticleSettings = RequiredMaterial ? &RequiredMaterial->GetParticleSettings() : nullptr;
	const bool bMaterialControlsSubUV = MaterialParticleSettings && MaterialParticleSettings->bUseSubUV;

	if (bMaterialControlsSubUV)
	{
		const uint32 Columns = (std::max)(1u, MaterialParticleSettings->SubUVColumns);
		const uint32 Rows = (std::max)(1u, MaterialParticleSettings->SubUVRows);
		ImGui::TextDisabled("SubUV Source: Material (%u x %u)", Columns, Rows);
		ImGui::BeginDisabled();
		int SubImagesHorizontal = static_cast<int>(Columns);
		ImGui::DragInt("SubUV Columns", &SubImagesHorizontal, 1.0f, 1, 64);
		int SubImagesVertical = static_cast<int>(Rows);
		ImGui::DragInt("SubUV Rows", &SubImagesVertical, 1.0f, 1, 64);
		ImGui::EndDisabled();
	}
	else
	{
		int SubImagesHorizontal = (std::max)(1, Required->SubImages_Horizontal);
		if (ImGui::DragInt("SubUV Columns", &SubImagesHorizontal, 1.0f, 1, 64))
		{
			Required->SubImages_Horizontal = (std::max)(1, SubImagesHorizontal);
			bChanged = true;
		}

		int SubImagesVertical = (std::max)(1, Required->SubImages_Vertical);
		if (ImGui::DragInt("SubUV Rows", &SubImagesVertical, 1.0f, 1, 64))
		{
			Required->SubImages_Vertical = (std::max)(1, SubImagesVertical);
			bChanged = true;
		}
	}

	int AlphaSource = std::clamp(Required->AlphaSource, 0, static_cast<int>(IM_ARRAYSIZE(GParticleAlphaSourceNames)) - 1);
	if (ImGui::Combo("Alpha Source", &AlphaSource, GParticleAlphaSourceNames, IM_ARRAYSIZE(GParticleAlphaSourceNames)))
	{
		Required->AlphaSource = AlphaSource;
		bChanged = true;
	}

	float AlphaThreshold = Required->AlphaThreshold;
	if (ImGui::DragFloat("Alpha Threshold", &AlphaThreshold, 0.005f, 0.0f, 1.0f, "%.3f"))
	{
		Required->AlphaThreshold = std::clamp(AlphaThreshold, 0.0f, 1.0f);
		bChanged = true;
	}

	float AlphaPower = Required->AlphaPower;
	if (ImGui::DragFloat("Alpha Power", &AlphaPower, 0.01f, 0.001f, 8.0f, "%.3f"))
	{
		Required->AlphaPower = (std::max)(0.001f, AlphaPower);
		bChanged = true;
	}

	float ColorIntensity = Required->ColorIntensity;
	if (ImGui::DragFloat("Color Intensity", &ColorIntensity, 0.01f, 0.0f, 8.0f, "%.3f"))
	{
		Required->ColorIntensity = (std::max)(0.0f, ColorIntensity);
		bChanged = true;
	}

	FVector Origin = Required->EmitterOrigin;
	if (ImGui::DragFloat3("Emitter Origin", &Origin.X, 0.1f))
	{
		Required->EmitterOrigin = Origin;
		bChanged = true;
	}

	float Duration = Required->EmitterDuration;
	if (ImGui::DragFloat("Emitter Duration", &Duration, 0.01f, 0.0f, 1000.0f))
	{
		Required->EmitterDuration = (std::max)(0.0f, Duration);
		bChanged = true;
	}

	int MaxDrawCount = Required->MaxDrawCount;
	if (ImGui::DragInt("Max Draw Count", &MaxDrawCount, 1.0f, 0, 100000))
	{
		Required->MaxDrawCount = (std::max)(0, MaxDrawCount);
		bChanged = true;
	}

	bool bUseLocalSpace = Required->bUseLocalSpace != 0;
	if (ImGui::Checkbox("Use Local Space", &bUseLocalSpace))
	{
		Required->bUseLocalSpace = bUseLocalSpace;
		bChanged = true;
	}

	bool bKillOnDeactivate = Required->bKillOnDeactivate != 0;
	if (ImGui::Checkbox("Kill On Deactivate", &bKillOnDeactivate))
	{
		Required->bKillOnDeactivate = bKillOnDeactivate;
		bChanged = true;
	}

	bool bKillOnCompleted = Required->bKillOnCompleted != 0;
	if (ImGui::Checkbox("Kill On Completed", &bKillOnCompleted))
	{
		Required->bKillOnCompleted = bKillOnCompleted;
		bChanged = true;
	}

	return bChanged;
}
