#include "ParticleEditorWidget.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Runtime/Engine.h"
#include "Slate/SlateApplication.h"
#include "Viewport/Viewport.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Component/Particle/ParticleSystemComponent.h"
#include "Core/TickFunction.h"
#include "Render/Scene/FScene.h"

#include "Materials/MaterialManager.h"
#include "Mesh/MeshManager.h"
#include "Particle/ParticleSystem.h"
#include "Particle/ParticleSystemManager.h"
#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/ParticleModule.h"
#include "Particle/ParticleEmitterInstance.h"
#include "Particle/ParticleModuleVectorField.h"
#include "Particle/VectorField/VectorFieldManager.h"
#include "Editor/UI/ContentBrowser/ContentItem.h"
#include "Platform/Paths.h"
#include "Math/FloatCurve.h"
#include "Particle/Distributions/DistributionFloat.h"
#include "Particle/Distributions/DistributionFloatConstant.h"
#include "Particle/Distributions/DistributionFloatCurve.h"
#include "Particle/Distributions/DistributionFloatUniform.h"
#include "Particle/Distributions/DistributionFloatUniformCurve.h"
#include "Particle/Distributions/DistributionVector.h"
#include "Particle/Distributions/DistributionVectorConstant.h"
#include "Particle/Distributions/DistributionVectorCurve.h"
#include "Particle/Distributions/DistributionVectorUniform.h"
#include "Particle/Distributions/DistributionVectorUniformCurve.h"
#include "Particle/Modules/ParticleModuleAcceleration.h"
#include "Particle/Modules/ParticleModuleBeamSource.h"
#include "Particle/Modules/ParticleModuleBeamTarget.h"
#include "Particle/Modules/ParticleModuleBeamNoise.h"

#include "Particle/Modules/ParticleModuleCollision.h"
#include "Particle/Modules/ParticleModuleColor.h"
#include "Particle/Modules/ParticleModuleColorOverLife.h"
#include "Particle/Modules/ParticleModuleDynamicParameter.h"
#include "Particle/Modules/ParticleModuleEventGenerator.h"
#include "Particle/Modules/ParticleModuleEventReceiverKillAll.h"
#include "Particle/Modules/ParticleModuleEventReceiverSpawn.h"
#include "Particle/Modules/ParticleModuleLifetime.h"
#include "Particle/Modules/ParticleModuleLocation.h"
#include "Particle/Modules/ParticleModuleRequired.h"
#include "Particle/Modules/ParticleModuleSize.h"
#include "Particle/Modules/ParticleModuleSizeByLife.h"
#include "Particle/Modules/ParticleModuleSpawn.h"
#include "Particle/Modules/ParticleModuleSubUV.h"
#include "Particle/Modules/ParticleModuleSubUVMovie.h"
#include "Particle/Modules/ParticleModuleVelocity.h"
#include "Particle/TypeData/ParticleModuleTypeDataBeam.h"
#include "Particle/TypeData/ParticleModuleTypeDataMesh.h"
#include "Particle/TypeData/ParticleModuleTypeDataRibbon.h"
#include "Materials/Material.h" // ShaderPathForSerialize (RequiredModule 머티리얼 필터)

namespace
{
	constexpr int32 ModuleTokenRequired = -100;
	constexpr int32 ModuleTokenSpawn    = -101;
	constexpr int32 ModuleTokenTypeData = -102;

	constexpr int32 CurveSourceNone = -1;
	constexpr int32 CurveSourceSpawnRate = 0;
	constexpr int32 CurveSourceSpawnRateScale = 1;
	constexpr int32 CurveSourceLifetime = 2;
	constexpr int32 CurveSourceInitialLocation = 3;
	constexpr int32 CurveSourceInitialVelocity = 4;
	constexpr int32 CurveSourceAcceleration = 5;
	constexpr int32 CurveSourceInitialSize = 6;
	constexpr int32 CurveSourceSizeByLife = 7;
	constexpr int32 CurveSourceColorOverLifeRGB = 8;
	constexpr int32 CurveSourceColorOverLifeAlpha = 9;
	constexpr int32 CurveSourceInitialColorRGB = 10;
	constexpr int32 CurveSourceInitialColorAlpha = 11;
	constexpr int32 CurveSourceSubImageIndex = 12;
	constexpr int32 CurveSourceSubUVMovieFrameRate = 13;
	constexpr int32 CurveSourceBeamWidth = 14;
	constexpr int32 CurveSourceBeamDistance = 15;
	constexpr int32 CurveSourceBeamSource = 16;
	constexpr int32 CurveSourceBeamTarget = 17;
	constexpr int32 CurveSourceBeamNoiseRange = 18;
	constexpr int32 CurveSourceBeamNoiseFrequency = 19;
	constexpr int32 CurveSourceBeamNoiseSpeed = 20;
	constexpr int32 CurveSourceBeamSourceTangent = 21;
	constexpr int32 CurveSourceBeamTargetTangent = 22;

	uint32 GNextParticleEditorInstanceId = 0;

	constexpr const char* ParticleModuleDragPayloadType = "ParticleModuleReorder";

	struct FParticleModuleDragPayload
	{
		int32 EmitterIndex = -1;
		int32 ModuleIndex = -1;
	};


	bool MoveModuleInLOD(UParticleLODLevel* LOD, int32 SourceIndex, int32 TargetIndex, bool bDropAfterTarget, int32& OutNewIndex)
	{
		OutNewIndex = SourceIndex;
		if (!LOD) return false;

		const int32 Count = static_cast<int32>(LOD->Modules.size());
		if (SourceIndex < 0 || SourceIndex >= Count || TargetIndex < 0 || TargetIndex >= Count)
		{
			return false;
		}

		int32 InsertIndex = TargetIndex + (bDropAfterTarget ? 1 : 0);
		if (SourceIndex < InsertIndex)
		{
			--InsertIndex;
		}

		if (InsertIndex == SourceIndex)
		{
			return false;
		}

		UParticleModule* MovingModule = LOD->Modules[SourceIndex];
		if (!MovingModule) return false;

		LOD->Modules.erase(LOD->Modules.begin() + SourceIndex);
		if (InsertIndex < 0) InsertIndex = 0;
		if (InsertIndex > static_cast<int32>(LOD->Modules.size()))
		{
			InsertIndex = static_cast<int32>(LOD->Modules.size());
		}

		LOD->Modules.insert(LOD->Modules.begin() + InsertIndex, MovingModule);
		OutNewIndex = InsertIndex;
		return true;
	}

	const char* CategoryName(UParticleModule::EModuleCategory Category)
	{
		switch (Category)
		{
		case UParticleModule::EModuleCategory::Required:  return "Required";
		case UParticleModule::EModuleCategory::TypeData:  return "TypeData";
		case UParticleModule::EModuleCategory::Spawn:     return "Spawn";
		case UParticleModule::EModuleCategory::Lifetime:  return "Lifetime";
		case UParticleModule::EModuleCategory::Location:  return "Location";
		case UParticleModule::EModuleCategory::Velocity:  return "Velocity";
		case UParticleModule::EModuleCategory::Acceleration:  return "Const Acceleration";
		case UParticleModule::EModuleCategory::Color:     return "Color";
		case UParticleModule::EModuleCategory::Size:      return "Size";
		case UParticleModule::EModuleCategory::Rotation:  return "Rotation";
		case UParticleModule::EModuleCategory::Collision: return "Collision";
		case UParticleModule::EModuleCategory::Event:     return "Event";
		case UParticleModule::EModuleCategory::SubUV:     return "SubUV";
		case UParticleModule::EModuleCategory::Beam:      return "Beam";
		default:                                         return "Module";
		}
	}

	ImU32 CategoryColor(UParticleModule::EModuleCategory Category, bool bSelected, bool bEnabled)
	{
		ImU32 Color = IM_COL32(64, 64, 64, 255);
		switch (Category)
		{
		case UParticleModule::EModuleCategory::Required:  Color = IM_COL32(202, 209, 94, 255);  break;
		case UParticleModule::EModuleCategory::Spawn:     Color = IM_COL32(185, 92, 92, 255);   break;
		case UParticleModule::EModuleCategory::TypeData:  Color = IM_COL32(104, 205, 138, 255); break;
		case UParticleModule::EModuleCategory::Lifetime:  Color = IM_COL32(83, 83, 93, 255);    break;
		case UParticleModule::EModuleCategory::Location:  Color = IM_COL32(83, 83, 93, 255);    break;
		case UParticleModule::EModuleCategory::Velocity:  Color = IM_COL32(83, 83, 93, 255);    break;
		case UParticleModule::EModuleCategory::Acceleration:  Color = IM_COL32(83, 83, 93, 255);    break;
		case UParticleModule::EModuleCategory::Color:     Color = IM_COL32(56, 105, 56, 255);   break;
		case UParticleModule::EModuleCategory::Size:      Color = IM_COL32(56, 105, 56, 255);   break;
		case UParticleModule::EModuleCategory::Collision: Color = IM_COL32(70, 70, 86, 255);    break;
		case UParticleModule::EModuleCategory::Event:     Color = IM_COL32(72, 94, 120, 255);   break;
		case UParticleModule::EModuleCategory::SubUV:     Color = IM_COL32(72, 94, 120, 255);   break;
		case UParticleModule::EModuleCategory::Beam:      Color = IM_COL32(88, 116, 164, 255);  break;
		default: break;
		}

		if (!bEnabled)
		{
			Color = IM_COL32(46, 46, 48, 255);
		}
		if (bSelected)
		{
			Color = IM_COL32(224, 119, 55, 255);
		}
		return Color;
	}

	float ClampPanelValue(float Value, float MinValue, float MaxValue)
	{
		if (MaxValue < MinValue)
		{
			MaxValue = MinValue;
		}
		return (std::min)((std::max)(Value, MinValue), MaxValue);
	}

	void RenderVerticalSplitter(const char* Id, float& LeftWidth, float MinLeft, float MaxLeft, float Height)
	{
		const float Thickness = 6.0f;
		ImVec2 Pos = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton(Id, ImVec2(Thickness, Height));
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		}
		if (ImGui::IsItemActive())
		{
			LeftWidth = ClampPanelValue(LeftWidth + ImGui::GetIO().MouseDelta.x, MinLeft, MaxLeft);
		}
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImU32 Color = ImGui::IsItemActive() ? IM_COL32(130, 130, 130, 255) : IM_COL32(70, 70, 70, 255);
		DrawList->AddRectFilled(Pos, ImVec2(Pos.x + Thickness, Pos.y + Height), IM_COL32(36, 36, 38, 255));
		DrawList->AddLine(ImVec2(Pos.x + Thickness * 0.5f, Pos.y + 6.0f), ImVec2(Pos.x + Thickness * 0.5f, Pos.y + Height - 6.0f), Color);
	}

	void RenderHorizontalSplitter(const char* Id, float& BottomHeight, float MinBottom, float MaxBottom, float Width)
	{
		const float Thickness = 6.0f;
		ImVec2 Pos = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton(Id, ImVec2(Width, Thickness));
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
		}
		if (ImGui::IsItemActive())
		{
			BottomHeight = ClampPanelValue(BottomHeight - ImGui::GetIO().MouseDelta.y, MinBottom, MaxBottom);
		}
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImU32 Color = ImGui::IsItemActive() ? IM_COL32(130, 130, 130, 255) : IM_COL32(70, 70, 70, 255);
		DrawList->AddRectFilled(Pos, ImVec2(Pos.x + Width, Pos.y + Thickness), IM_COL32(36, 36, 38, 255));
		DrawList->AddLine(ImVec2(Pos.x + 6.0f, Pos.y + Thickness * 0.5f), ImVec2(Pos.x + Width - 6.0f, Pos.y + Thickness * 0.5f), Color);
	}

	bool IsCoreModuleToken(int32 ModuleIndex)
	{
		return ModuleIndex == ModuleTokenRequired || ModuleIndex == ModuleTokenSpawn;
	}

	bool ModuleCanOpenCurvePanel(UParticleModule* Module)
	{
		return Cast<UParticleModuleSpawn>(Module) ||
			Cast<UParticleModuleLifetime>(Module) ||
			Cast<UParticleModuleLocation>(Module) ||
			Cast<UParticleModuleVelocity>(Module) ||
			Cast<UParticleModuleAcceleration>(Module) ||
			Cast<UParticleModuleSize>(Module) ||
			Cast<UParticleModuleSizeByLife>(Module) ||
			Cast<UParticleModuleColor>(Module) ||
			Cast<UParticleModuleColorOverLife>(Module) ||
			Cast<UParticleModuleSubUV>(Module) ||
			Cast<UParticleModuleSubUVMovie>(Module);
	}

	void CopyToBuffer(char* Buffer, size_t BufferSize, const FString& Text)
	{
		if (!Buffer || BufferSize == 0) return;
		std::snprintf(Buffer, BufferSize, "%s", Text.c_str());
	}

	bool InputTextFString(const char* Label, FString& Value)
	{
		char Buffer[512] = {};
		CopyToBuffer(Buffer, sizeof(Buffer), Value);
		if (ImGui::InputText(Label, Buffer, sizeof(Buffer)))
		{
			Value = Buffer;
			return true;
		}
		return false;
	}

	bool InputTextSoftObject(const char* Label, FSoftObjectPtr& Value)
	{
		FString Path = Value.ToString();
		if (Path.empty()) Path = "None";
		if (InputTextFString(Label, Path))
		{
			Value.SetPath(Path.empty() ? FString("None") : Path);
			return true;
		}
		return false;
	}

	template<typename TAssetItem>
	bool AssetComboField(const char* Label, FSoftObjectPtr& Value, const TArray<TAssetItem>& Assets)
	{
		FString CurrentPath = Value.ToString();
		if (CurrentPath.empty())
		{
			CurrentPath = "None";
		}

		bool bChanged = false;
		const char* Preview = CurrentPath.c_str();
		if (ImGui::BeginCombo(Label, Preview))
		{
			const bool bSelectedNone = (CurrentPath == "None");
			if (ImGui::Selectable("None", bSelectedNone))
			{
				Value.SetPath("None");
				CurrentPath = "None";
				bChanged = true;
			}
			if (bSelectedNone)
			{
				ImGui::SetItemDefaultFocus();
			}

			for (const TAssetItem& Item : Assets)
			{
				const bool bSelected = (CurrentPath == Item.FullPath);
				if (ImGui::Selectable(Item.DisplayName.c_str(), bSelected))
				{
					Value.SetPath(Item.FullPath);
					CurrentPath = Item.FullPath;
					bChanged = true;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", Item.FullPath.c_str());
				}
			}
			ImGui::EndCombo();
		}
		return bChanged;
	}

	bool MaterialComboField(const char* Label, FSoftObjectPtr& Value)
	{
		return AssetComboField(Label, Value, FMaterialManager::Get().GetAvailableMaterialFiles());
	}

	// emitter 타입(TypeDataModule 클래스)이 강제하는 파티클 셰이더 경로 — ResolveSectionShader 와 동일 매핑.
	FString ParticleForcedShaderPath(const UParticleLODLevel* LOD)
	{
		if (!LOD || LOD->TypeDataModule == nullptr)
			return "Shaders/Particle/Sprite.hlsl";                       // Sprite (TypeData 없음 = 기본)
		if (Cast<UParticleModuleTypeDataMesh>(LOD->TypeDataModule))
			return "Shaders/Particle/Mesh.hlsl";                         // Mesh
		return "Shaders/Particle/BeamTrail.hlsl";                        // Beam / Ribbon
	}

	// emitter 강제 셰이더와 레이아웃(ShaderPathForSerialize)이 일치하는 머티리얼만 노출하는 콤보.
	// (콤보가 열렸을 때만 머티리얼을 load → 캐시됨. 불일치 머티리얼은 셰이더 레이아웃이 안 맞아 사용 불가.)
	bool MaterialComboFieldFiltered(const char* Label, FSoftObjectPtr& Value, const FString& RequiredShaderPath)
	{
		FString CurrentPath = Value.ToString();
		if (CurrentPath.empty()) CurrentPath = "None";

		bool bChanged = false;
		if (ImGui::BeginCombo(Label, CurrentPath.c_str()))
		{
			const bool bSelectedNone = (CurrentPath == "None");
			if (ImGui::Selectable("None", bSelectedNone)) { Value.SetPath("None"); CurrentPath = "None"; bChanged = true; }
			if (bSelectedNone) ImGui::SetItemDefaultFocus();

			for (const FMaterialAssetListItem& Item : FMaterialManager::Get().GetAvailableMaterialFiles())
			{
				UMaterial* Mat = FMaterialManager::Get().GetOrCreateMaterial(Item.FullPath);
				if (!Mat || Mat->GetShaderPathForSerialize() != RequiredShaderPath)
					continue; // emitter 강제 셰이더와 레이아웃 불일치 → 제외

				const bool bSelected = (CurrentPath == Item.FullPath);
				if (ImGui::Selectable(Item.DisplayName.c_str(), bSelected)) { Value.SetPath(Item.FullPath); CurrentPath = Item.FullPath; bChanged = true; }
				if (bSelected) ImGui::SetItemDefaultFocus();
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Item.FullPath.c_str());
			}
			ImGui::EndCombo();
		}
		return bChanged;
	}

	bool MeshComboField(const char* Label, FSoftObjectPtr& Value)
	{
		return AssetComboField(Label, Value, FMeshManager::Get().GetAvailableStaticMeshFiles());
	}

	bool VectorFieldComboField(const char* Label, FSoftObjectPtr& Value)
	{
		FVectorFieldManager::Get().RefreshAvailableVectorFields();
		bool bChanged = AssetComboField(Label, Value, FVectorFieldManager::Get().GetAvailableVectorFieldFiles());

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("VectorFieldContentItem"))
			{
				if (Payload->DataSize == sizeof(FContentItem))
				{
					const FContentItem* Item = static_cast<const FContentItem*>(Payload->Data);
					if (Item)
					{
						Value.SetPath(FPaths::ToUtf8(Item->Path.lexically_relative(FPaths::RootDir()).generic_wstring()));
						bChanged = true;
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		return bChanged;
	}

	bool DragFloat3Field(const char* Label, FVector& Value, float Speed = 0.1f, float Min = 0.0f, float Max = 0.0f)
	{
		float Data[3] = { Value.X, Value.Y, Value.Z };
		bool bChanged = false;
		if (Min < Max)
		{
			bChanged = ImGui::DragFloat3(Label, Data, Speed, Min, Max, "%.3f");
		}
		else
		{
			bChanged = ImGui::DragFloat3(Label, Data, Speed, 0.0f, 0.0f, "%.3f");
		}

		if (bChanged)
		{
			Value.X = Data[0];
			Value.Y = Data[1];
			Value.Z = Data[2];
		}
		return bChanged;
	}

	bool DragRotatorField(const char* Label, FRotator& Value, float Speed = 0.1f)
	{
		float Data[3] = { Value.Pitch, Value.Yaw, Value.Roll };
		if (ImGui::DragFloat3(Label, Data, Speed, 0.0f, 0.0f, "%.3f"))
		{
			Value.Pitch = Data[0];
			Value.Yaw = Data[1];
			Value.Roll = Data[2];
			return true;
		}
		return false;
	}

	bool Color4Field(const char* Label, FVector4& Value)
	{
		float Data[4] = { Value.R, Value.G, Value.B, Value.A };
		if (ImGui::ColorEdit4(Label, Data))
		{
			Value.R = Data[0];
			Value.G = Data[1];
			Value.B = Data[2];
			Value.A = Data[3];
			return true;
		}
		return false;
	}

	bool ComboInt(const char* Label, int32& Value, const char* const* Names, int32 Count)
	{
		if (Value < 0) Value = 0;
		if (Value >= Count) Value = Count - 1;
		const char* Preview = (Value >= 0 && Value < Count) ? Names[Value] : "Unknown";
		bool bChanged = false;
		if (ImGui::BeginCombo(Label, Preview))
		{
			for (int32 i = 0; i < Count; ++i)
			{
				const bool bSelected = (Value == i);
				if (ImGui::Selectable(Names[i], bSelected))
				{
					Value = i;
					bChanged = true;
				}
				if (bSelected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		return bChanged;
	}

	const char* GetDistributionDisplayName(const UDistribution* Distribution)
	{
		return Distribution ? Distribution->GetDistributionDisplayName() : "None";
	}


	int32 FloatDistributionType(UDistributionFloat* Distribution)
	{
		if (Cast<UDistributionFloatUniform>(Distribution)) return 1;
		if (Cast<UDistributionFloatCurve>(Distribution)) return 2;
		if (Cast<UDistributionFloatUniformCurve>(Distribution)) return 3;
		return 0;
	}

	int32 VectorDistributionType(UDistributionVector* Distribution)
	{
		if (Cast<UDistributionVectorUniform>(Distribution)) return 1;
		if (Cast<UDistributionVectorCurve>(Distribution)) return 2;
		if (Cast<UDistributionVectorUniformCurve>(Distribution)) return 3;
		return 0;
	}

	bool IsFloatCurveDistribution(UDistributionFloat* Distribution)
	{
		return Cast<UDistributionFloatCurve>(Distribution) || Cast<UDistributionFloatUniformCurve>(Distribution);
	}

	bool IsVectorCurveDistribution(UDistributionVector* Distribution)
	{
		return Cast<UDistributionVectorCurve>(Distribution) || Cast<UDistributionVectorUniformCurve>(Distribution);
	}

	UDistributionFloat* EnsureFloatDistribution(UDistributionFloat*& Distribution, UObject* Outer)
	{
		if (!Distribution)
		{
			Distribution = UObjectManager::Get().CreateObject<UDistributionFloatConstant>(Outer);
		}
		return Distribution;
	}

	UDistributionVector* EnsureVectorDistribution(UDistributionVector*& Distribution, UObject* Outer)
	{
		if (!Distribution)
		{
			Distribution = UObjectManager::Get().CreateObject<UDistributionVectorConstant>(Outer);
		}
		return Distribution;
	}

	void EnsureFloatConstantDistribution(UDistributionFloat*& Distribution, UObject* Outer, float Value)
	{
		if (Distribution) return;
		auto* NewDistribution = UObjectManager::Get().CreateObject<UDistributionFloatConstant>(Outer);
		if (NewDistribution)
		{
			NewDistribution->Constant = Value;
			Distribution = NewDistribution;
		}
	}

	void EnsureInitialColorDistributions(UParticleModuleColor* Color)
	{
		if (!Color) return;
		if (!Color->StartColorDistribution)
		{
			auto* Distribution = UObjectManager::Get().CreateObject<UDistributionVectorConstant>(Color);
			Distribution->Constant = FVector(Color->StartColor.X, Color->StartColor.Y, Color->StartColor.Z);
			Color->StartColorDistribution = Distribution;
		}
		if (!Color->StartAlphaDistribution)
		{
			auto* Distribution = UObjectManager::Get().CreateObject<UDistributionFloatConstant>(Color);
			Distribution->Constant = Color->StartColor.W;
			Color->StartAlphaDistribution = Distribution;
		}
	}

	float GetCurveValueOrDefault(const FFloatCurve& Curve, float DefaultValue)
	{
		return Curve.Keys.empty() ? DefaultValue : Curve.Keys.front().Value;
	}

	void InitializeFloatCurveFromRange(UDistributionFloatCurve* CurveDistribution, float PrevMin, float PrevMax)
	{
		if (!CurveDistribution) return;
		const float Value = (PrevMin + PrevMax) * 0.5f;
		CurveDistribution->SetConstant(Value);
	}

	void InitializeFloatUniformCurveFromRange(UDistributionFloatUniformCurve* CurveDistribution, float PrevMin, float PrevMax)
	{
		if (!CurveDistribution) return;
		CurveDistribution->SetConstant(PrevMin, PrevMax);
	}

	void InitializeVectorCurveFromRange(UDistributionVectorCurve* CurveDistribution, const FVector& PrevMin, const FVector& PrevMax)
	{
		if (!CurveDistribution) return;
		CurveDistribution->SetConstant((PrevMin + PrevMax) * 0.5f);
	}

	void InitializeVectorUniformCurveFromRange(UDistributionVectorUniformCurve* CurveDistribution, const FVector& PrevMin, const FVector& PrevMax)
	{
		if (!CurveDistribution) return;
		CurveDistribution->SetConstant(PrevMin, PrevMax);
	}


	bool DrawFloatCurveKeyArrayEditor(const char* Label, FFloatCurve& Curve, float ValueSpeed)
	{
		bool bChanged = false;
		bool bNeedsSort = false;
		bool bNeedsAutoTangents = false;

		ImGui::PushID(Label);
		if (ImGui::TreeNodeEx(Label, ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextUnformatted("Points");
			ImGui::SameLine(160.0f);
			ImGui::Text("%d Array element", static_cast<int32>(Curve.Keys.size()));
			ImGui::SameLine();
			if (ImGui::SmallButton("+"))
			{
				float NewTime = 0.0f;
				float NewValue = Curve.DefaultValue;
				if (!Curve.Keys.empty())
				{
					NewTime = Curve.Keys.back().Time + 0.1f;
					NewValue = Curve.Keys.back().Value;
				}
				Curve.AddKey(NewTime, NewValue, ECurveInterpMode::Linear);
				bChanged = true;
				bNeedsSort = true;
				bNeedsAutoTangents = true;
			}
			if (!Curve.Keys.empty())
			{
				ImGui::SameLine();
				if (ImGui::SmallButton("Delete Last"))
				{
					Curve.Keys.erase(Curve.Keys.end() - 1);
					bChanged = true;
					bNeedsAutoTangents = true;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Sort"))
				{
					bChanged = true;
					bNeedsSort = true;
					bNeedsAutoTangents = true;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Auto"))
				{
					for (FCurveKey& Key : Curve.Keys)
					{
						Key.TangentMode = ECurveTangentMode::Auto;
					}
					bChanged = true;
					bNeedsAutoTangents = true;
				}
			}

			bool bLooped = Curve.PreExtrapMode == ECurveExtrapMode::Loop && Curve.PostExtrapMode == ECurveExtrapMode::Loop;
			if (ImGui::Checkbox("Is Looped", &bLooped))
			{
				Curve.PreExtrapMode = bLooped ? ECurveExtrapMode::Loop : ECurveExtrapMode::Clamp;
				Curve.PostExtrapMode = bLooped ? ECurveExtrapMode::Loop : ECurveExtrapMode::Clamp;
				bChanged = true;
			}

			static const char* InterpNames[] = { "Constant", "Linear", "Cubic" };
			for (int32 KeyIndex = 0; KeyIndex < static_cast<int32>(Curve.Keys.size()); ++KeyIndex)
			{
				ImGui::PushID(KeyIndex);
				char Header[64] = {};
				std::snprintf(Header, sizeof(Header), "Index [%d]", KeyIndex);
				const bool bOpen = ImGui::TreeNodeEx(Header, KeyIndex == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0);
				ImGui::SameLine(160.0f);
				ImGui::TextUnformatted("5 members");
				ImGui::SameLine();
				if (ImGui::SmallButton("Delete"))
				{
					Curve.Keys.erase(Curve.Keys.begin() + KeyIndex);
					bChanged = true;
					bNeedsAutoTangents = true;
					if (bOpen) ImGui::TreePop();
					ImGui::PopID();
					break;
				}

				if (bOpen)
				{
					FCurveKey& Key = Curve.Keys[KeyIndex];
					if (ImGui::DragFloat("In Val", &Key.Time, 0.01f, 0.0f, 0.0f, "%.3f"))
					{
						bChanged = true;
						bNeedsSort = true;
						bNeedsAutoTangents = Key.TangentMode == ECurveTangentMode::Auto;
					}
					if (ImGui::DragFloat("Out Val", &Key.Value, ValueSpeed, 0.0f, 0.0f, "%.3f"))
					{
						bChanged = true;
						bNeedsAutoTangents = Key.TangentMode == ECurveTangentMode::Auto;
					}
					if (ImGui::DragFloat("Arrive Tangent", &Key.ArriveTangent, 0.01f, 0.0f, 0.0f, "%.3f"))
					{
						Key.TangentMode = ECurveTangentMode::User;
						bChanged = true;
					}
					if (ImGui::DragFloat("Leave Tangent", &Key.LeaveTangent, 0.01f, 0.0f, 0.0f, "%.3f"))
					{
						Key.TangentMode = ECurveTangentMode::User;
						bChanged = true;
					}

					int32 Interp = static_cast<int32>(Key.InterpMode);
					if (ComboInt("Interp Mode", Interp, InterpNames, 3))
					{
						Key.InterpMode = static_cast<ECurveInterpMode>(Interp);
						bChanged = true;
						bNeedsAutoTangents = Key.TangentMode == ECurveTangentMode::Auto;
					}

					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			if (bNeedsSort)
			{
				Curve.SortKeys();
			}
			if (bNeedsAutoTangents)
			{
				Curve.AutoSetTangents();
			}

			ImGui::TreePop();
		}
		ImGui::PopID();
		return bChanged;
	}

	void EnsureCurveKeyCount(FFloatCurve& Curve, int32 TargetCount)
	{
		while (static_cast<int32>(Curve.Keys.size()) < TargetCount)
		{
			float NewTime = 0.0f;
			float NewValue = Curve.DefaultValue;
			if (!Curve.Keys.empty())
			{
				NewTime = Curve.Keys.back().Time + 0.1f;
				NewValue = Curve.Keys.back().Value;
			}
			Curve.AddKey(NewTime, NewValue, ECurveInterpMode::Linear);
		}
	}

	bool DrawVectorCurveKeyArrayEditor(UDistributionVectorCurve* CurveDistribution, float ValueSpeed)
	{
		if (!CurveDistribution) return false;

		bool bChanged = false;
		bool bNeedsSort = false;
		bool bNeedsAutoTangents = false;

		FFloatCurve& XCurve = CurveDistribution->GetXCurve();
		FFloatCurve& YCurve = CurveDistribution->GetYCurve();
		FFloatCurve& ZCurve = CurveDistribution->GetZCurve();
		int32 KeyCount = static_cast<int32>((std::max)(XCurve.Keys.size(), (std::max)(YCurve.Keys.size(), ZCurve.Keys.size())));
		EnsureCurveKeyCount(XCurve, KeyCount);
		EnsureCurveKeyCount(YCurve, KeyCount);
		EnsureCurveKeyCount(ZCurve, KeyCount);

		ImGui::PushID("DistributionVectorConstantCurve");
		if (ImGui::TreeNodeEx("Distribution Vector Constant Curve", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextUnformatted("Points");
			ImGui::SameLine(160.0f);
			ImGui::Text("%d Array element", KeyCount);
			ImGui::SameLine();
			if (ImGui::SmallButton("+"))
			{
				float NewTime = 0.0f;
				FVector NewValue(XCurve.DefaultValue, YCurve.DefaultValue, ZCurve.DefaultValue);
				if (KeyCount > 0)
				{
					NewTime = (std::max)(XCurve.Keys.back().Time, (std::max)(YCurve.Keys.back().Time, ZCurve.Keys.back().Time)) + 0.1f;
					NewValue = FVector(XCurve.Keys.back().Value, YCurve.Keys.back().Value, ZCurve.Keys.back().Value);
				}
				XCurve.AddKey(NewTime, NewValue.X, ECurveInterpMode::Linear);
				YCurve.AddKey(NewTime, NewValue.Y, ECurveInterpMode::Linear);
				ZCurve.AddKey(NewTime, NewValue.Z, ECurveInterpMode::Linear);
				++KeyCount;
				bChanged = true;
				bNeedsSort = true;
				bNeedsAutoTangents = true;
			}
			if (KeyCount > 0)
			{
				ImGui::SameLine();
				if (ImGui::SmallButton("Delete Last"))
				{
					XCurve.Keys.erase(XCurve.Keys.end() - 1);
					YCurve.Keys.erase(YCurve.Keys.end() - 1);
					ZCurve.Keys.erase(ZCurve.Keys.end() - 1);
					--KeyCount;
					bChanged = true;
					bNeedsAutoTangents = true;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Sort"))
				{
					bChanged = true;
					bNeedsSort = true;
					bNeedsAutoTangents = true;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Auto"))
				{
					for (int32 KeyIndex = 0; KeyIndex < KeyCount; ++KeyIndex)
					{
						XCurve.Keys[KeyIndex].TangentMode = ECurveTangentMode::Auto;
						YCurve.Keys[KeyIndex].TangentMode = ECurveTangentMode::Auto;
						ZCurve.Keys[KeyIndex].TangentMode = ECurveTangentMode::Auto;
					}
					bChanged = true;
					bNeedsAutoTangents = true;
				}
			}

			bool bLooped = XCurve.PreExtrapMode == ECurveExtrapMode::Loop && XCurve.PostExtrapMode == ECurveExtrapMode::Loop &&
				YCurve.PreExtrapMode == ECurveExtrapMode::Loop && YCurve.PostExtrapMode == ECurveExtrapMode::Loop &&
				ZCurve.PreExtrapMode == ECurveExtrapMode::Loop && ZCurve.PostExtrapMode == ECurveExtrapMode::Loop;
			if (ImGui::Checkbox("Is Looped", &bLooped))
			{
				const ECurveExtrapMode NewMode = bLooped ? ECurveExtrapMode::Loop : ECurveExtrapMode::Clamp;
				XCurve.PreExtrapMode = XCurve.PostExtrapMode = NewMode;
				YCurve.PreExtrapMode = YCurve.PostExtrapMode = NewMode;
				ZCurve.PreExtrapMode = ZCurve.PostExtrapMode = NewMode;
				bChanged = true;
			}

			static const char* InterpNames[] = { "Constant", "Linear", "Cubic" };
			for (int32 KeyIndex = 0; KeyIndex < KeyCount; ++KeyIndex)
			{
				ImGui::PushID(KeyIndex);
				char Header[64] = {};
				std::snprintf(Header, sizeof(Header), "Index [%d]", KeyIndex);
				const bool bOpen = ImGui::TreeNodeEx(Header, KeyIndex == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0);
				ImGui::SameLine(160.0f);
				ImGui::TextUnformatted("5 members");
				ImGui::SameLine();
				if (ImGui::SmallButton("Delete"))
				{
					XCurve.Keys.erase(XCurve.Keys.begin() + KeyIndex);
					YCurve.Keys.erase(YCurve.Keys.begin() + KeyIndex);
					ZCurve.Keys.erase(ZCurve.Keys.begin() + KeyIndex);
					--KeyCount;
					bChanged = true;
					bNeedsAutoTangents = true;
					if (bOpen) ImGui::TreePop();
					ImGui::PopID();
					break;
				}

				if (bOpen)
				{
					FCurveKey& XKey = XCurve.Keys[KeyIndex];
					FCurveKey& YKey = YCurve.Keys[KeyIndex];
					FCurveKey& ZKey = ZCurve.Keys[KeyIndex];

					float InVal = XKey.Time;
					if (ImGui::DragFloat("In Val", &InVal, 0.01f, 0.0f, 0.0f, "%.3f"))
					{
						XKey.Time = YKey.Time = ZKey.Time = InVal;
						bChanged = true;
						bNeedsSort = true;
						bNeedsAutoTangents = XKey.TangentMode == ECurveTangentMode::Auto || YKey.TangentMode == ECurveTangentMode::Auto || ZKey.TangentMode == ECurveTangentMode::Auto;
					}

					float OutVal[3] = { XKey.Value, YKey.Value, ZKey.Value };
					if (ImGui::DragFloat3("Out Val", OutVal, ValueSpeed, 0.0f, 0.0f, "%.3f"))
					{
						XKey.Value = OutVal[0];
						YKey.Value = OutVal[1];
						ZKey.Value = OutVal[2];
						bChanged = true;
						bNeedsAutoTangents = XKey.TangentMode == ECurveTangentMode::Auto || YKey.TangentMode == ECurveTangentMode::Auto || ZKey.TangentMode == ECurveTangentMode::Auto;
					}

					float ArriveTangent[3] = { XKey.ArriveTangent, YKey.ArriveTangent, ZKey.ArriveTangent };
					if (ImGui::DragFloat3("Arrive Tangent", ArriveTangent, 0.01f, 0.0f, 0.0f, "%.3f"))
					{
						XKey.ArriveTangent = ArriveTangent[0];
						YKey.ArriveTangent = ArriveTangent[1];
						ZKey.ArriveTangent = ArriveTangent[2];
						XKey.TangentMode = YKey.TangentMode = ZKey.TangentMode = ECurveTangentMode::User;
						bChanged = true;
					}

					float LeaveTangent[3] = { XKey.LeaveTangent, YKey.LeaveTangent, ZKey.LeaveTangent };
					if (ImGui::DragFloat3("Leave Tangent", LeaveTangent, 0.01f, 0.0f, 0.0f, "%.3f"))
					{
						XKey.LeaveTangent = LeaveTangent[0];
						YKey.LeaveTangent = LeaveTangent[1];
						ZKey.LeaveTangent = LeaveTangent[2];
						XKey.TangentMode = YKey.TangentMode = ZKey.TangentMode = ECurveTangentMode::User;
						bChanged = true;
					}

					int32 Interp = static_cast<int32>(XKey.InterpMode);
					if (ComboInt("Interp Mode", Interp, InterpNames, 3))
					{
						XKey.InterpMode = YKey.InterpMode = ZKey.InterpMode = static_cast<ECurveInterpMode>(Interp);
						bChanged = true;
						bNeedsAutoTangents = XKey.TangentMode == ECurveTangentMode::Auto || YKey.TangentMode == ECurveTangentMode::Auto || ZKey.TangentMode == ECurveTangentMode::Auto;
					}

					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			if (bNeedsSort)
			{
				XCurve.SortKeys();
				YCurve.SortKeys();
				ZCurve.SortKeys();
			}
			if (bNeedsAutoTangents)
			{
				XCurve.AutoSetTangents();
				YCurve.AutoSetTangents();
				ZCurve.AutoSetTangents();
			}

			ImGui::TreePop();
		}
		ImGui::PopID();
		return bChanged;
	}


	bool DrawFloatUniformCurveKeyArrayEditor(UDistributionFloatUniformCurve* CurveDistribution, float ValueSpeed)
	{
		if (!CurveDistribution) return false;

		bool bChanged = false;
		ImGui::PushID("DistributionFloatUniformCurve");
		if (ImGui::TreeNodeEx("Distribution Float Uniform Curve", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bChanged |= DrawFloatCurveKeyArrayEditor("Min", CurveDistribution->GetMinCurve(), ValueSpeed);
			bChanged |= DrawFloatCurveKeyArrayEditor("Max", CurveDistribution->GetMaxCurve(), ValueSpeed);
			ImGui::TreePop();
		}
		ImGui::PopID();
		return bChanged;
	}

	bool DrawVectorUniformCurveKeyArrayEditor(UDistributionVectorUniformCurve* CurveDistribution, float ValueSpeed)
	{
		if (!CurveDistribution) return false;

		bool bChanged = false;
		ImGui::PushID("DistributionVectorUniformCurve");
		if (ImGui::TreeNodeEx("Distribution Vector Uniform Curve", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::TreeNodeEx("Min", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bChanged |= DrawFloatCurveKeyArrayEditor("X", CurveDistribution->GetMinXCurve(), ValueSpeed);
				bChanged |= DrawFloatCurveKeyArrayEditor("Y", CurveDistribution->GetMinYCurve(), ValueSpeed);
				bChanged |= DrawFloatCurveKeyArrayEditor("Z", CurveDistribution->GetMinZCurve(), ValueSpeed);
				ImGui::TreePop();
			}
			if (ImGui::TreeNodeEx("Max", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bChanged |= DrawFloatCurveKeyArrayEditor("X", CurveDistribution->GetMaxXCurve(), ValueSpeed);
				bChanged |= DrawFloatCurveKeyArrayEditor("Y", CurveDistribution->GetMaxYCurve(), ValueSpeed);
				bChanged |= DrawFloatCurveKeyArrayEditor("Z", CurveDistribution->GetMaxZCurve(), ValueSpeed);
				ImGui::TreePop();
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
		return bChanged;
	}

	bool DrawFloatDistributionEditor(const char* Label, UDistributionFloat*& Distribution, UObject* Outer,
	                                 float Speed = 0.05f, float Min = 0.0f, float Max = 0.0f,
	                                 const char* TimeBasisText = nullptr)
	{
		bool bChanged = false;
		(void)TimeBasisText;
		ImGui::PushID(Label);
		ImGui::TextUnformatted(Label);
		EnsureFloatDistribution(Distribution, Outer);

		static const char* TypeNames[] = {
			"Distribution Float Constant",
			"Distribution Float Uniform",
			"Distribution Float Constant Curve",
			"Distribution Float Uniform Curve"
		};
		int32 Type = FloatDistributionType(Distribution);
		ImGui::Text("Current: %s", GetDistributionDisplayName(Distribution));
		if (ComboInt("Distribution Type", Type, TypeNames, 4))
		{
			float PrevMin = 0.0f;
			float PrevMax = 0.0f;
			if (Distribution)
			{
				Distribution->GetOutRange(PrevMin, PrevMax);
				UObjectManager::Get().DestroyObject(Distribution);
				Distribution = nullptr;
			}

			if (Type == 0)
			{
				auto* NewDistribution = UObjectManager::Get().CreateObject<UDistributionFloatConstant>(Outer);
				NewDistribution->Constant = (PrevMin + PrevMax) * 0.5f;
				Distribution = NewDistribution;
			}
			else if (Type == 1)
			{
				auto* NewDistribution = UObjectManager::Get().CreateObject<UDistributionFloatUniform>(Outer);
				NewDistribution->Min = PrevMin;
				NewDistribution->Max = PrevMax;
				Distribution = NewDistribution;
			}
			else if (Type == 2)
			{
				auto* NewDistribution = UObjectManager::Get().CreateObject<UDistributionFloatCurve>(Outer);
				InitializeFloatCurveFromRange(NewDistribution, PrevMin, PrevMax);
				Distribution = NewDistribution;
			}
			else
			{
				auto* NewDistribution = UObjectManager::Get().CreateObject<UDistributionFloatUniformCurve>(Outer);
				InitializeFloatUniformCurveFromRange(NewDistribution, PrevMin, PrevMax);
				Distribution = NewDistribution;
			}
			bChanged = true;
		}

		if (auto* Constant = Cast<UDistributionFloatConstant>(Distribution))
		{
			if (Min < Max)
			{
				bChanged |= ImGui::DragFloat("Constant", &Constant->Constant, Speed, Min, Max, "%.3f");
			}
			else
			{
				bChanged |= ImGui::DragFloat("Constant", &Constant->Constant, Speed, 0.0f, 0.0f, "%.3f");
			}
		}
		else if (auto* Uniform = Cast<UDistributionFloatUniform>(Distribution))
		{
			if (Min < Max)
			{
				bChanged |= ImGui::DragFloat("Min", &Uniform->Min, Speed, Min, Max, "%.3f");
				bChanged |= ImGui::DragFloat("Max", &Uniform->Max, Speed, Min, Max, "%.3f");
			}
			else
			{
				bChanged |= ImGui::DragFloat("Min", &Uniform->Min, Speed, 0.0f, 0.0f, "%.3f");
				bChanged |= ImGui::DragFloat("Max", &Uniform->Max, Speed, 0.0f, 0.0f, "%.3f");
			}
		}
		else if (auto* Curve = Cast<UDistributionFloatCurve>(Distribution))
		{
			bChanged |= DrawFloatCurveKeyArrayEditor("Distribution Float Constant Curve", Curve->GetCurve(), Speed);
		}
		else if (auto* UniformCurve = Cast<UDistributionFloatUniformCurve>(Distribution))
		{
			bChanged |= DrawFloatUniformCurveKeyArrayEditor(UniformCurve, Speed);
		}

		ImGui::PopID();
		return bChanged;
	}

	bool DrawVectorDistributionEditor(const char* Label, UDistributionVector*& Distribution, UObject* Outer,
	                                  float Speed = 0.1f, float Min = 0.0f, float Max = 0.0f,
	                                  const char* TimeBasisText = nullptr)
	{
		bool bChanged = false;
		(void)TimeBasisText;
		ImGui::PushID(Label);
		ImGui::TextUnformatted(Label);
		EnsureVectorDistribution(Distribution, Outer);

		static const char* TypeNames[] = {
			"Distribution Vector Constant",
			"Distribution Vector Uniform",
			"Distribution Vector Constant Curve",
			"Distribution Vector Uniform Curve"
		};
		int32 Type = VectorDistributionType(Distribution);
		ImGui::Text("Current: %s", GetDistributionDisplayName(Distribution));
		if (ComboInt("Distribution Type", Type, TypeNames, 4))
		{
			FVector PrevMin(0.0f, 0.0f, 0.0f);
			FVector PrevMax(0.0f, 0.0f, 0.0f);
			if (Distribution)
			{
				Distribution->GetRange(PrevMin, PrevMax);
				UObjectManager::Get().DestroyObject(Distribution);
				Distribution = nullptr;
			}

			if (Type == 0)
			{
				auto* NewDistribution = UObjectManager::Get().CreateObject<UDistributionVectorConstant>(Outer);
				NewDistribution->Constant = (PrevMin + PrevMax) * 0.5f;
				Distribution = NewDistribution;
			}
			else if (Type == 1)
			{
				auto* NewDistribution = UObjectManager::Get().CreateObject<UDistributionVectorUniform>(Outer);
				NewDistribution->Min = PrevMin;
				NewDistribution->Max = PrevMax;
				Distribution = NewDistribution;
			}
			else if (Type == 2)
			{
				auto* NewDistribution = UObjectManager::Get().CreateObject<UDistributionVectorCurve>(Outer);
				InitializeVectorCurveFromRange(NewDistribution, PrevMin, PrevMax);
				Distribution = NewDistribution;
			}
			else
			{
				auto* NewDistribution = UObjectManager::Get().CreateObject<UDistributionVectorUniformCurve>(Outer);
				InitializeVectorUniformCurveFromRange(NewDistribution, PrevMin, PrevMax);
				Distribution = NewDistribution;
			}
			bChanged = true;
		}

		if (auto* Constant = Cast<UDistributionVectorConstant>(Distribution))
		{
			bChanged |= DragFloat3Field("Constant", Constant->Constant, Speed, Min, Max);
		}
		else if (auto* Uniform = Cast<UDistributionVectorUniform>(Distribution))
		{
			bChanged |= DragFloat3Field("Min", Uniform->Min, Speed, Min, Max);
			bChanged |= DragFloat3Field("Max", Uniform->Max, Speed, Min, Max);
		}
		else if (auto* Curve = Cast<UDistributionVectorCurve>(Distribution))
		{
			bChanged |= DrawVectorCurveKeyArrayEditor(Curve, Speed);
		}
		else if (auto* UniformCurve = Cast<UDistributionVectorUniformCurve>(Distribution))
		{
			bChanged |= DrawVectorUniformCurveKeyArrayEditor(UniformCurve, Speed);
		}

		ImGui::PopID();
		return bChanged;
	}

	void GetCurveViewRange(const FFloatCurve* const* Curves, int32 CurveCount,
	                       float& OutMinTime, float& OutMaxTime,
	                       float& OutMinValue, float& OutMaxValue)
	{
		bool bHasPoint = false;
		OutMinTime = 0.0f;
		OutMaxTime = 1.0f;
		OutMinValue = -1.0f;
		OutMaxValue = 1.0f;

		for (int32 CurveIndex = 0; CurveIndex < CurveCount; ++CurveIndex)
		{
			const FFloatCurve* Curve = Curves[CurveIndex];
			if (!Curve) continue;

			for (const FCurveKey& Key : Curve->Keys)
			{
				if (!bHasPoint)
				{
					OutMinTime = OutMaxTime = Key.Time;
					OutMinValue = OutMaxValue = Key.Value;
					bHasPoint = true;
				}
				else
				{
					OutMinTime = (std::min)(OutMinTime, Key.Time);
					OutMaxTime = (std::max)(OutMaxTime, Key.Time);
					OutMinValue = (std::min)(OutMinValue, Key.Value);
					OutMaxValue = (std::max)(OutMaxValue, Key.Value);
				}
			}
		}

		if (!bHasPoint)
		{
			return;
		}

		if (OutMaxTime <= OutMinTime + 0.001f)
		{
			OutMinTime -= 0.5f;
			OutMaxTime += 0.5f;
		}
		if (OutMaxValue <= OutMinValue + 0.001f)
		{
			OutMinValue -= 0.5f;
			OutMaxValue += 0.5f;
		}

		const float TimePad = (OutMaxTime - OutMinTime) * 0.05f;
		const float ValuePad = (OutMaxValue - OutMinValue) * 0.10f;
		OutMinTime -= TimePad;
		OutMaxTime += TimePad;
		OutMinValue -= ValuePad;
		OutMaxValue += ValuePad;
	}

	ImVec2 CurveToScreenPoint(float Time, float Value,
	                          float MinTime, float MaxTime,
	                          float MinValue, float MaxValue,
	                          const ImVec2& Min, const ImVec2& Max)
	{
		const float TimeSpan = (MaxTime > MinTime) ? (MaxTime - MinTime) : 1.0f;
		const float ValueSpan = (MaxValue > MinValue) ? (MaxValue - MinValue) : 1.0f;
		const float X = (Time - MinTime) / TimeSpan;
		const float Y = (Value - MinValue) / ValueSpan;
		return ImVec2(Min.x + X * (Max.x - Min.x), Max.y - Y * (Max.y - Min.y));
	}

	void ScreenPointToCurve(const ImVec2& Mouse,
	                        float MinTime, float MaxTime,
	                        float MinValue, float MaxValue,
	                        const ImVec2& Min, const ImVec2& Max,
	                        float& OutTime, float& OutValue)
	{
		const float Width = (std::max)(1.0f, Max.x - Min.x);
		const float Height = (std::max)(1.0f, Max.y - Min.y);
		const float X = std::clamp((Mouse.x - Min.x) / Width, 0.0f, 1.0f);
		const float Y = std::clamp((Max.y - Mouse.y) / Height, 0.0f, 1.0f);
		OutTime = MinTime + (MaxTime - MinTime) * X;
		OutValue = MinValue + (MaxValue - MinValue) * Y;
	}

	int32 SortCurveAndFindKey(FFloatCurve& Curve, float Time, float Value)
	{
		Curve.SortKeys();
		Curve.AutoSetTangents();

		int32 BestIndex = -1;
		float BestDistance = 0.0f;
		for (int32 i = 0; i < static_cast<int32>(Curve.Keys.size()); ++i)
		{
			const float Dt = Curve.Keys[i].Time - Time;
			const float Dv = Curve.Keys[i].Value - Value;
			const float Distance = Dt * Dt + Dv * Dv;
			if (BestIndex < 0 || Distance < BestDistance)
			{
				BestIndex = i;
				BestDistance = Distance;
			}
		}
		return BestIndex;
	}

	struct FCurveGraphViewState
	{
		bool  bValid = false;
		float MinTime = 0.0f;
		float MaxTime = 1.0f;
		float MinValue = -1.0f;
		float MaxValue = 1.0f;
		bool  bPanning = false;
		int32 DraggingTangentHandle = 0; // 0=None, 1=Arrive, 2=Leave
	};

	FCurveGraphViewState& GetCurveGraphViewState(int32 CurveSource)
	{
		static FCurveGraphViewState States[16];
		const int32 Index = (std::max)(0, (std::min)(CurveSource, 15));
		return States[Index];
	}

	void FitCurveView(FCurveGraphViewState& View, FFloatCurve* const* Curves, int32 CurveCount, bool bFitTime, bool bFitValue)
	{
		const FFloatCurve* ConstCurves[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
		const int32 SafeCurveCount = (std::min)(CurveCount, 6);
		for (int32 i = 0; i < SafeCurveCount; ++i)
		{
			ConstCurves[i] = Curves[i];
		}

		float AutoMinTime = 0.0f;
		float AutoMaxTime = 1.0f;
		float AutoMinValue = -1.0f;
		float AutoMaxValue = 1.0f;
		GetCurveViewRange(ConstCurves, SafeCurveCount, AutoMinTime, AutoMaxTime, AutoMinValue, AutoMaxValue);

		if (bFitTime || !View.bValid)
		{
			View.MinTime = AutoMinTime;
			View.MaxTime = AutoMaxTime;
		}
		if (bFitValue || !View.bValid)
		{
			View.MinValue = AutoMinValue;
			View.MaxValue = AutoMaxValue;
		}
		View.bValid = true;
	}

	ImVec2 GetTangentHandlePosition(const FCurveKey& Key, bool bArrive,
	                               float MinTime, float MaxTime,
	                               float MinValue, float MaxValue,
	                               const ImVec2& CanvasPos, const ImVec2& CanvasMax)
	{
		constexpr float TangentHandleLength = 48.0f;
		const float Tangent = bArrive ? Key.ArriveTangent : Key.LeaveTangent;
		const float Direction = bArrive ? -1.0f : 1.0f;
		const float Width = (std::max)(1.0f, CanvasMax.x - CanvasPos.x);
		const float Height = (std::max)(1.0f, CanvasMax.y - CanvasPos.y);
		const float TimeSpan = (std::max)(0.001f, MaxTime - MinTime);
		const float ValueSpan = (std::max)(0.001f, MaxValue - MinValue);

		ImVec2 DirectionVector(Direction * Width / TimeSpan, -Direction * Tangent * Height / ValueSpan);
		const float Length = std::sqrt(DirectionVector.x * DirectionVector.x + DirectionVector.y * DirectionVector.y);
		if (Length <= 1e-6f)
		{
			DirectionVector = ImVec2(Direction, 0.0f);
		}
		else
		{
			DirectionVector.x /= Length;
			DirectionVector.y /= Length;
		}

		const ImVec2 KeyPos = CurveToScreenPoint(Key.Time, Key.Value, MinTime, MaxTime, MinValue, MaxValue, CanvasPos, CanvasMax);
		return ImVec2(KeyPos.x + DirectionVector.x * TangentHandleLength, KeyPos.y + DirectionVector.y * TangentHandleLength);
	}

	bool IsPointNear(const ImVec2& A, const ImVec2& B, float Radius)
	{
		const float Dx = A.x - B.x;
		const float Dy = A.y - B.y;
		return (Dx * Dx + Dy * Dy) <= Radius * Radius;
	}

	void DrawCurveGrid(ImDrawList* DrawList, const ImVec2& CanvasPos, const ImVec2& CanvasMax,
	                   float MinTime, float MaxTime, float MinValue, float MaxValue)
	{
		constexpr int32 VerticalLineCount = 10;
		constexpr int32 HorizontalLineCount = 8;
		const ImU32 MajorColor = IM_COL32(125, 125, 125, 150);
		const ImU32 MinorColor = IM_COL32(78, 78, 78, 130);
		const ImU32 TextColor = IM_COL32(205, 205, 205, 230);

		for (int32 GridX = 0; GridX <= VerticalLineCount; ++GridX)
		{
			const float Alpha = static_cast<float>(GridX) / static_cast<float>(VerticalLineCount);
			const float X = CanvasPos.x + (CanvasMax.x - CanvasPos.x) * Alpha;
			DrawList->AddLine(ImVec2(X, CanvasPos.y), ImVec2(X, CanvasMax.y), GridX == 0 ? MajorColor : MinorColor);
			const float Time = MinTime + (MaxTime - MinTime) * Alpha;
			char Buffer[32] = {};
			std::snprintf(Buffer, sizeof(Buffer), "%.2f", Time);
			DrawList->AddText(ImVec2(X + 3.0f, CanvasMax.y - 18.0f), TextColor, Buffer);
		}

		for (int32 GridY = 0; GridY <= HorizontalLineCount; ++GridY)
		{
			const float Alpha = static_cast<float>(GridY) / static_cast<float>(HorizontalLineCount);
			const float Y = CanvasPos.y + (CanvasMax.y - CanvasPos.y) * Alpha;
			DrawList->AddLine(ImVec2(CanvasPos.x, Y), ImVec2(CanvasMax.x, Y), GridY == HorizontalLineCount ? MajorColor : MinorColor);
			const float Value = MaxValue - (MaxValue - MinValue) * Alpha;
			char Buffer[32] = {};
			std::snprintf(Buffer, sizeof(Buffer), "%.2f", Value);
			DrawList->AddText(ImVec2(CanvasPos.x + 4.0f, Y + 2.0f), TextColor, Buffer);
		}

		if (MinTime <= 0.0f && MaxTime >= 0.0f)
		{
			const ImVec2 ZeroBottom = CurveToScreenPoint(0.0f, MinValue, MinTime, MaxTime, MinValue, MaxValue, CanvasPos, CanvasMax);
			DrawList->AddLine(ImVec2(ZeroBottom.x, CanvasPos.y), ImVec2(ZeroBottom.x, CanvasMax.y), IM_COL32(170, 170, 170, 170), 1.5f);
		}
		if (MinValue <= 0.0f && MaxValue >= 0.0f)
		{
			const ImVec2 ZeroLeft = CurveToScreenPoint(MinTime, 0.0f, MinTime, MaxTime, MinValue, MaxValue, CanvasPos, CanvasMax);
			DrawList->AddLine(ImVec2(CanvasPos.x, ZeroLeft.y), ImVec2(CanvasMax.x, ZeroLeft.y), IM_COL32(170, 170, 170, 170), 1.5f);
		}
	}

	bool DrawCurveGraph(const char* Label, FFloatCurve* const* Curves,
	                    const char* const* ChannelNames,
	                    const ImU32* Colors, int32 CurveCount, int32 CurveSource,
	                    int32& SelectedCurveSource, int32& SelectedCurveChannel,
	                    int32& SelectedCurveKeyIndex, bool& bDraggingCurveKey,
	                    float Height = 150.0f)
	{
		bool bChanged = false;
		ImGui::PushID(Label);

		const int32 SafeCurveCount = (std::min)(CurveCount, 6);
		FCurveGraphViewState& View = GetCurveGraphViewState(CurveSource);
		if (!View.bValid)
		{
			FitCurveView(View, Curves, SafeCurveCount, true, true);
		}

		bool bHasSelectedKey = SelectedCurveSource == CurveSource &&
			SelectedCurveChannel >= 0 && SelectedCurveChannel < SafeCurveCount &&
			Curves[SelectedCurveChannel] &&
			SelectedCurveKeyIndex >= 0 &&
			SelectedCurveKeyIndex < static_cast<int32>(Curves[SelectedCurveChannel]->Keys.size());

		if (ImGui::Button("Horizontal"))
		{
			FitCurveView(View, Curves, SafeCurveCount, true, false);
		}
		ImGui::SameLine();
		if (ImGui::Button("Vertical"))
		{
			FitCurveView(View, Curves, SafeCurveCount, false, true);
		}
		ImGui::SameLine();
		if (ImGui::Button("Fit"))
		{
			FitCurveView(View, Curves, SafeCurveCount, true, true);
		}
		ImGui::SameLine();
		if (bHasSelectedKey)
		{
			FFloatCurve* SelectedCurve = Curves[SelectedCurveChannel];
			FCurveKey& SelectedKey = SelectedCurve->Keys[SelectedCurveKeyIndex];
			if (ImGui::Button("Auto"))
			{
				SelectedKey.InterpMode = ECurveInterpMode::Cubic;
				SelectedKey.TangentMode = ECurveTangentMode::Auto;
				SelectedCurve->AutoSetTangents();
				bChanged = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Linear"))
			{
				SelectedKey.InterpMode = ECurveInterpMode::Linear;
				bChanged = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Constant"))
			{
				SelectedKey.InterpMode = ECurveInterpMode::Constant;
				bChanged = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Flatten"))
			{
				SelectedKey.ArriveTangent = 0.0f;
				SelectedKey.LeaveTangent = 0.0f;
				SelectedKey.TangentMode = ECurveTangentMode::User;
				bChanged = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Delete"))
			{
				SelectedCurve->Keys.erase(SelectedCurve->Keys.begin() + SelectedCurveKeyIndex);
				SelectedCurveKeyIndex = -1;
				View.DraggingTangentHandle = 0;
				bDraggingCurveKey = false;
				bHasSelectedKey = false;
				bChanged = true;
			}
		}
		else
		{
			ImGui::TextDisabled("Select key for Auto / Linear / Constant / Flatten / Delete");
		}

		const ImVec2 CanvasPos = ImGui::GetCursorScreenPos();
		const float Width = (std::max)(120.0f, ImGui::GetContentRegionAvail().x);
		const ImVec2 CanvasSize(Width, Height);
		ImGui::InvisibleButton("##CurveGraph", CanvasSize);
		const bool bCanvasHovered = ImGui::IsItemHovered();
		const bool bCanvasActive = ImGui::IsItemActive();
		ImGuiIO& IO = ImGui::GetIO();

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImVec2 CanvasMax(CanvasPos.x + CanvasSize.x, CanvasPos.y + CanvasSize.y);
		DrawList->AddRectFilled(CanvasPos, CanvasMax, IM_COL32(52, 52, 52, 255));
		DrawList->AddRect(CanvasPos, CanvasMax, IM_COL32(130, 130, 130, 190));

		DrawCurveGrid(DrawList, CanvasPos, CanvasMax, View.MinTime, View.MaxTime, View.MinValue, View.MaxValue);

		if (bCanvasHovered && IO.MouseWheel != 0.0f)
		{
			float MouseTime = 0.0f;
			float MouseValue = 0.0f;
			ScreenPointToCurve(IO.MousePos, View.MinTime, View.MaxTime, View.MinValue, View.MaxValue, CanvasPos, CanvasMax, MouseTime, MouseValue);
			const float Zoom = IO.MouseWheel > 0.0f ? 0.85f : 1.1764706f;
			const bool bZoomTime = !IO.KeyCtrl;
			const bool bZoomValue = !IO.KeyShift;
			if (bZoomTime)
			{
				View.MinTime = MouseTime + (View.MinTime - MouseTime) * Zoom;
				View.MaxTime = MouseTime + (View.MaxTime - MouseTime) * Zoom;
				if (View.MaxTime <= View.MinTime + 0.001f) View.MaxTime = View.MinTime + 0.001f;
			}
			if (bZoomValue)
			{
				View.MinValue = MouseValue + (View.MinValue - MouseValue) * Zoom;
				View.MaxValue = MouseValue + (View.MaxValue - MouseValue) * Zoom;
				if (View.MaxValue <= View.MinValue + 0.001f) View.MaxValue = View.MinValue + 0.001f;
			}
		}

		if (bCanvasHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
		{
			View.bPanning = true;
		}
		if (View.bPanning && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 1.0f)))
		{
			const float TimeSpan = View.MaxTime - View.MinTime;
			const float ValueSpan = View.MaxValue - View.MinValue;
			const float TimeDelta = (IO.MouseDelta.x / (std::max)(1.0f, CanvasSize.x)) * TimeSpan;
			const float ValueDelta = (IO.MouseDelta.y / (std::max)(1.0f, CanvasSize.y)) * ValueSpan;
			View.MinTime -= TimeDelta;
			View.MaxTime -= TimeDelta;
			View.MinValue += ValueDelta;
			View.MaxValue += ValueDelta;
		}
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle))
		{
			View.bPanning = false;
		}

		const ImVec2 MousePos = IO.MousePos;
		int32 HoveredChannel = -1;
		int32 HoveredKeyIndex = -1;
		float HoveredDistanceSq = 0.0f;

		constexpr int32 SampleCount = 160;
		for (int32 CurveIndex = 0; CurveIndex < SafeCurveCount; ++CurveIndex)
		{
			FFloatCurve* Curve = Curves[CurveIndex];
			if (!Curve) continue;

			ImVec2 Prev;
			bool bHasPrev = false;
			for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
			{
				const float Alpha = static_cast<float>(SampleIndex) / static_cast<float>(SampleCount - 1);
				const float T = View.MinTime + (View.MaxTime - View.MinTime) * Alpha;
				const float V = Curve->Evaluate(T);
				const ImVec2 P = CurveToScreenPoint(T, V, View.MinTime, View.MaxTime, View.MinValue, View.MaxValue, CanvasPos, CanvasMax);
				if (bHasPrev)
				{
					DrawList->AddLine(Prev, P, Colors[CurveIndex], 2.0f);
				}
				Prev = P;
				bHasPrev = true;
			}

			for (int32 KeyIndex = 0; KeyIndex < static_cast<int32>(Curve->Keys.size()); ++KeyIndex)
			{
				const FCurveKey& Key = Curve->Keys[KeyIndex];
				const ImVec2 P = CurveToScreenPoint(Key.Time, Key.Value, View.MinTime, View.MaxTime, View.MinValue, View.MaxValue, CanvasPos, CanvasMax);
				const bool bSelectedKey = SelectedCurveSource == CurveSource && SelectedCurveChannel == CurveIndex && SelectedCurveKeyIndex == KeyIndex;
				const ImU32 KeyColor = bSelectedKey ? IM_COL32(255, 245, 80, 255) : Colors[CurveIndex];
				DrawList->AddCircleFilled(P, bSelectedKey ? 6.0f : 4.5f, KeyColor);
				DrawList->AddCircle(P, bSelectedKey ? 7.0f : 5.5f, bSelectedKey ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 20, 20, 255));

				const float Dx = MousePos.x - P.x;
				const float Dy = MousePos.y - P.y;
				const float DistanceSq = Dx * Dx + Dy * Dy;
				if (bCanvasHovered && DistanceSq <= 81.0f && (HoveredKeyIndex < 0 || DistanceSq < HoveredDistanceSq))
				{
					HoveredChannel = CurveIndex;
					HoveredKeyIndex = KeyIndex;
					HoveredDistanceSq = DistanceSq;
				}
			}
		}

		int32 HoveredTangentHandle = 0;
		if (bHasSelectedKey)
		{
			FFloatCurve* SelectedCurve = Curves[SelectedCurveChannel];
			FCurveKey& Key = SelectedCurve->Keys[SelectedCurveKeyIndex];
			if (Key.InterpMode == ECurveInterpMode::Cubic)
			{
				const ImVec2 KeyPos = CurveToScreenPoint(Key.Time, Key.Value, View.MinTime, View.MaxTime, View.MinValue, View.MaxValue, CanvasPos, CanvasMax);
				const ImVec2 ArriveHandle = GetTangentHandlePosition(Key, true, View.MinTime, View.MaxTime, View.MinValue, View.MaxValue, CanvasPos, CanvasMax);
				const ImVec2 LeaveHandle = GetTangentHandlePosition(Key, false, View.MinTime, View.MaxTime, View.MinValue, View.MaxValue, CanvasPos, CanvasMax);
				DrawList->AddLine(KeyPos, ArriveHandle, IM_COL32(95, 150, 255, 190), 1.5f);
				DrawList->AddLine(KeyPos, LeaveHandle, IM_COL32(95, 150, 255, 190), 1.5f);
				DrawList->AddCircleFilled(ArriveHandle, 4.5f, IM_COL32(95, 150, 255, 255));
				DrawList->AddCircleFilled(LeaveHandle, 4.5f, IM_COL32(95, 150, 255, 255));
				DrawList->AddCircle(ArriveHandle, 4.5f, IM_COL32(15, 20, 30, 220));
				DrawList->AddCircle(LeaveHandle, 4.5f, IM_COL32(15, 20, 30, 220));
				if (bCanvasHovered && IsPointNear(MousePos, ArriveHandle, 7.0f)) HoveredTangentHandle = 1;
				if (bCanvasHovered && IsPointNear(MousePos, LeaveHandle, 7.0f)) HoveredTangentHandle = 2;
			}
		}

		if (HoveredTangentHandle != 0 || HoveredKeyIndex >= 0)
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		}
		else if (View.bPanning)
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
		}

		if (bCanvasHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			int32 TargetChannel = -1;
			if (SelectedCurveSource == CurveSource && SelectedCurveChannel >= 0 && SelectedCurveChannel < SafeCurveCount && Curves[SelectedCurveChannel])
			{
				TargetChannel = SelectedCurveChannel;
			}
			else
			{
				for (int32 CurveIndex = 0; CurveIndex < SafeCurveCount; ++CurveIndex)
				{
					if (Curves[CurveIndex])
					{
						TargetChannel = CurveIndex;
						break;
					}
				}
			}
			FFloatCurve* TargetCurve = TargetChannel >= 0 ? Curves[TargetChannel] : nullptr;
			if (TargetCurve)
			{
				float NewTime = 0.0f;
				float NewValue = 0.0f;
				ScreenPointToCurve(MousePos, View.MinTime, View.MaxTime, View.MinValue, View.MaxValue, CanvasPos, CanvasMax, NewTime, NewValue);
				TargetCurve->AddKey(NewTime, NewValue, ECurveInterpMode::Linear);
				SelectedCurveSource = CurveSource;
				SelectedCurveChannel = TargetChannel;
				SelectedCurveKeyIndex = SortCurveAndFindKey(*TargetCurve, NewTime, NewValue);
				bChanged = true;
			}
		}
		else if (bCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			if (HoveredTangentHandle != 0)
			{
				View.DraggingTangentHandle = HoveredTangentHandle;
				bDraggingCurveKey = false;
			}
			else if (HoveredKeyIndex >= 0)
			{
				SelectedCurveSource = CurveSource;
				SelectedCurveChannel = HoveredChannel;
				SelectedCurveKeyIndex = HoveredKeyIndex;
				bDraggingCurveKey = true;
			}
			else
			{
				SelectedCurveSource = CurveSourceNone;
				SelectedCurveChannel = -1;
				SelectedCurveKeyIndex = -1;
				bDraggingCurveKey = false;
				View.DraggingTangentHandle = 0;
			}
		}

		if (View.DraggingTangentHandle != 0 && bCanvasActive && bHasSelectedKey && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			FFloatCurve* SelectedCurve = Curves[SelectedCurveChannel];
			FCurveKey& Key = SelectedCurve->Keys[SelectedCurveKeyIndex];
			float MouseTime = 0.0f;
			float MouseValue = 0.0f;
			ScreenPointToCurve(MousePos, View.MinTime, View.MaxTime, View.MinValue, View.MaxValue, CanvasPos, CanvasMax, MouseTime, MouseValue);

			float NewTangent = 0.0f;
			if (View.DraggingTangentHandle == 1)
			{
				const float DeltaTime = Key.Time - MouseTime;
				NewTangent = std::fabs(DeltaTime) > 0.001f ? (Key.Value - MouseValue) / DeltaTime : Key.ArriveTangent;
			}
			else
			{
				const float DeltaTime = MouseTime - Key.Time;
				NewTangent = std::fabs(DeltaTime) > 0.001f ? (MouseValue - Key.Value) / DeltaTime : Key.LeaveTangent;
			}

			if (Key.TangentMode == ECurveTangentMode::Auto)
			{
				Key.TangentMode = ECurveTangentMode::User;
			}
			if (Key.TangentMode == ECurveTangentMode::Break)
			{
				if (View.DraggingTangentHandle == 1) Key.ArriveTangent = NewTangent;
				else Key.LeaveTangent = NewTangent;
			}
			else
			{
				Key.ArriveTangent = NewTangent;
				Key.LeaveTangent = NewTangent;
			}
			bChanged = true;
		}

		if (bDraggingCurveKey && bCanvasActive && SelectedCurveSource == CurveSource &&
			SelectedCurveChannel >= 0 && SelectedCurveChannel < SafeCurveCount)
		{
			FFloatCurve* Curve = Curves[SelectedCurveChannel];
			if (Curve && SelectedCurveKeyIndex >= 0 && SelectedCurveKeyIndex < static_cast<int32>(Curve->Keys.size()) && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			{
				FCurveKey& Key = Curve->Keys[SelectedCurveKeyIndex];
				const float TimeSpan = View.MaxTime - View.MinTime;
				const float ValueSpan = View.MaxValue - View.MinValue;
				Key.Time += (IO.MouseDelta.x / (std::max)(1.0f, CanvasSize.x)) * TimeSpan;
				Key.Value -= (IO.MouseDelta.y / (std::max)(1.0f, CanvasSize.y)) * ValueSpan;
				if (Key.TangentMode == ECurveTangentMode::Auto)
				{
					Curve->AutoSetTangents();
				}
				bChanged = true;
			}
		}

		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			if (bDraggingCurveKey && SelectedCurveSource == CurveSource && SelectedCurveChannel >= 0 && SelectedCurveChannel < SafeCurveCount)
			{
				FFloatCurve* Curve = Curves[SelectedCurveChannel];
				if (Curve && SelectedCurveKeyIndex >= 0 && SelectedCurveKeyIndex < static_cast<int32>(Curve->Keys.size()))
				{
					const float NewTime = Curve->Keys[SelectedCurveKeyIndex].Time;
					const float NewValue = Curve->Keys[SelectedCurveKeyIndex].Value;
					SelectedCurveKeyIndex = SortCurveAndFindKey(*Curve, NewTime, NewValue);
				}
			}
			bDraggingCurveKey = false;
			View.DraggingTangentHandle = 0;
		}

		if (HoveredKeyIndex >= 0)
		{
			const FCurveKey& HoveredKey = Curves[HoveredChannel]->Keys[HoveredKeyIndex];
			ImGui::SetTooltip("%s  Key %d\nIn %.3f\nOut %.3f",
				ChannelNames && ChannelNames[HoveredChannel] ? ChannelNames[HoveredChannel] : "Curve",
				HoveredKeyIndex,
				HoveredKey.Time,
				HoveredKey.Value);
		}

		if (ImGui::BeginPopupContextItem("CurveGraphContext"))
		{
			if (HoveredKeyIndex >= 0 && ImGui::MenuItem("Delete Key"))
			{
				FFloatCurve* Curve = Curves[HoveredChannel];
				Curve->Keys.erase(Curve->Keys.begin() + HoveredKeyIndex);
				SelectedCurveKeyIndex = -1;
				bChanged = true;
			}
			if (ImGui::MenuItem("Add Key Here"))
			{
				int32 TargetChannel = -1;
				if (SelectedCurveSource == CurveSource && SelectedCurveChannel >= 0 && SelectedCurveChannel < SafeCurveCount && Curves[SelectedCurveChannel])
				{
					TargetChannel = SelectedCurveChannel;
				}
				else
				{
					for (int32 CurveIndex = 0; CurveIndex < SafeCurveCount; ++CurveIndex)
					{
						if (Curves[CurveIndex])
						{
							TargetChannel = CurveIndex;
							break;
						}
					}
				}
				FFloatCurve* TargetCurve = TargetChannel >= 0 ? Curves[TargetChannel] : nullptr;
				if (TargetCurve)
				{
					float NewTime = 0.0f;
					float NewValue = 0.0f;
					ScreenPointToCurve(MousePos, View.MinTime, View.MaxTime, View.MinValue, View.MaxValue, CanvasPos, CanvasMax, NewTime, NewValue);
					TargetCurve->AddKey(NewTime, NewValue, ECurveInterpMode::Linear);
					SelectedCurveSource = CurveSource;
					SelectedCurveChannel = TargetChannel;
					SelectedCurveKeyIndex = SortCurveAndFindKey(*TargetCurve, NewTime, NewValue);
					bChanged = true;
				}
			}
			if (ImGui::MenuItem("Fit To Keys"))
			{
				FitCurveView(View, Curves, SafeCurveCount, true, true);
			}
			ImGui::EndPopup();
		}

		float LegendX = CanvasPos.x + 8.0f;
		const float LegendY = CanvasPos.y + 8.0f;
		for (int32 CurveIndex = 0; CurveIndex < SafeCurveCount; ++CurveIndex)
		{
			if (!ChannelNames || !ChannelNames[CurveIndex]) continue;
			DrawList->AddCircleFilled(ImVec2(LegendX + 4.0f, LegendY + 7.0f), 4.0f, Colors[CurveIndex]);
			DrawList->AddText(ImVec2(LegendX + 12.0f, LegendY), IM_COL32(230, 230, 230, 240), ChannelNames[CurveIndex]);
			LegendX += ImGui::CalcTextSize(ChannelNames[CurveIndex]).x + 34.0f;
		}

		ImGui::TextDisabled("LMB drag key: edit  |  Double click: add key  |  RMB/MMB drag: pan  |  Wheel: zoom  |  Ctrl/Shift+Wheel: one-axis zoom");
		ImGui::PopID();
		return bChanged;
	}

	bool DrawFloatCurveDistributionPanel(const char* Label, UDistributionFloat* Distribution, int32 CurveSource,
	                                    int32& SelectedCurveSource, int32& SelectedCurveChannel,
	                                    int32& SelectedCurveKeyIndex, bool& bDraggingCurveKey)
	{
		bool bChanged = false;
		ImGui::PushID(Label);
		if (ImGui::CollapsingHeader(Label, ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (auto* UniformCurve = Cast<UDistributionFloatUniformCurve>(Distribution))
			{
				FFloatCurve* Curves[] = { &UniformCurve->GetMinCurve(), &UniformCurve->GetMaxCurve() };
				const char* Channels[] = { "Min", "Max" };
				const ImU32 Colors[] = {
					IM_COL32(90, 170, 255, 255),
					IM_COL32(255, 210, 80, 255)
				};
				const float GraphHeight = (std::max)(180.0f, ImGui::GetContentRegionAvail().y - 28.0f);
				bChanged |= DrawCurveGraph("FloatUniformCurveGraph", Curves, Channels, Colors, 2, CurveSource,
					SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey, GraphHeight);
			}
			else if (auto* CurveDistribution = Cast<UDistributionFloatCurve>(Distribution))
			{
				FFloatCurve& Curve = CurveDistribution->GetCurve();
				FFloatCurve* Curves[] = { &Curve };
				const char* Channels[] = { "Value" };
				const ImU32 Colors[] = { IM_COL32(255, 210, 80, 255) };
				const float GraphHeight = (std::max)(180.0f, ImGui::GetContentRegionAvail().y - 28.0f);
				bChanged |= DrawCurveGraph("FloatCurveGraph", Curves, Channels, Colors, 1, CurveSource,
					SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey, GraphHeight);
			}
		}
		ImGui::PopID();
		return bChanged;
	}

	bool DrawVectorCurveDistributionPanel(const char* Label, UDistributionVector* Distribution, int32 CurveSource,
	                                     int32& SelectedCurveSource, int32& SelectedCurveChannel,
	                                     int32& SelectedCurveKeyIndex, bool& bDraggingCurveKey)
	{
		bool bChanged = false;
		ImGui::PushID(Label);
		if (ImGui::CollapsingHeader(Label, ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (auto* UniformCurve = Cast<UDistributionVectorUniformCurve>(Distribution))
			{
				FFloatCurve* Curves[] = {
					&UniformCurve->GetMinXCurve(),
					&UniformCurve->GetMinYCurve(),
					&UniformCurve->GetMinZCurve(),
					&UniformCurve->GetMaxXCurve(),
					&UniformCurve->GetMaxYCurve(),
					&UniformCurve->GetMaxZCurve()
				};
				const char* Channels[] = { "Min.X", "Min.Y", "Min.Z", "Max.X", "Max.Y", "Max.Z" };
				const ImU32 Colors[] = {
					IM_COL32(160, 70, 70, 255),
					IM_COL32(70, 150, 70, 255),
					IM_COL32(70, 90, 190, 255),
					IM_COL32(255, 90, 90, 255),
					IM_COL32(90, 230, 90, 255),
					IM_COL32(90, 150, 255, 255)
				};
				const float GraphHeight = (std::max)(180.0f, ImGui::GetContentRegionAvail().y - 28.0f);
				bChanged |= DrawCurveGraph("VectorUniformCurveGraph", Curves, Channels, Colors, 6, CurveSource,
					SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey, GraphHeight);
			}
			else if (auto* CurveDistribution = Cast<UDistributionVectorCurve>(Distribution))
			{
				FFloatCurve* Curves[] = {
					&CurveDistribution->GetXCurve(),
					&CurveDistribution->GetYCurve(),
					&CurveDistribution->GetZCurve()
				};
				const char* Channels[] = { "X / R", "Y / G", "Z / B" };
				const ImU32 Colors[] = {
					IM_COL32(240, 90, 90, 255),
					IM_COL32(90, 220, 90, 255),
					IM_COL32(90, 140, 255, 255)
				};
				const float GraphHeight = (std::max)(180.0f, ImGui::GetContentRegionAvail().y - 28.0f);
				bChanged |= DrawCurveGraph("VectorCurveGraph", Curves, Channels, Colors, 3, CurveSource,
					SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey, GraphHeight);
			}
		}
		ImGui::PopID();
		return bChanged;
	}


	template<typename TModule>
	TModule* CreateParticleModule(UParticleLODLevel* LOD, UParticleEmitter* Owner)
	{
		if (!LOD) return nullptr;
		TModule* Module = UObjectManager::Get().CreateObject<TModule>(LOD);
		if (!Module) return nullptr;
		Module->SetToSensibleDefaults(Owner);
		return Module;
	}

	FString ModuleSelectionLabel(int32 Token)
	{
		switch (Token)
		{
		case ModuleTokenRequired: return "Required";
		case ModuleTokenSpawn:    return "Spawn";
		case ModuleTokenTypeData: return "TypeData";
		default:                 return Token >= 0 ? ("Module[" + std::to_string(Token) + "]") : "Emitter";
		}
	}
}

FParticleEditorWidget::FParticleEditorWidget()
	: InstanceId(static_cast<int32>(GNextParticleEditorInstanceId++))
{
	const FString Id = std::to_string(InstanceId);
	PreviewWorldHandle = FName("ParticleEditorPreview_" + Id);
	WindowIdSuffix = "###ParticleEditor_" + Id;
}

FParticleEditorWidget::~FParticleEditorWidget()
{
	Close();
}

bool FParticleEditorWidget::CanEdit(UObject* Object) const
{
	return Cast<UParticleSystem>(Object) != nullptr;
}

bool FParticleEditorWidget::IsEditingObject(UObject* Object) const
{
	if (FAssetEditorWidget::IsEditingObject(Object))
	{
		return true;
	}

	const UParticleSystem* Current = Cast<UParticleSystem>(EditedObject);
	const UParticleSystem* Requested = Cast<UParticleSystem>(Object);
	if (!IsOpen() || !Current || !Requested)
	{
		return false;
	}

	const FString& CurrentPath = Current->GetSourcePath();
	return !CurrentPath.empty()
		&& CurrentPath != "None"
		&& CurrentPath == Requested->GetSourcePath();
}

void FParticleEditorWidget::Open(UObject* Object)
{
	FAssetEditorWidget::Open(Object);
	if (!IsOpen())
	{
		return;
	}

	SelectedEmitterIndex = -1;
	SelectedModuleIndex  = -1;
	ClearSelectedCurveKey();
	CurrentLODIndex      = 0;
	bSimPlaying          = true;
	bPendingClose        = false;

	FWorldContext& WorldContext = GEngine->CreateWorldContext(EWorldType::EditorPreview, PreviewWorldHandle);
	WorldContext.World->SetWorldType(EWorldType::EditorPreview);
	WorldContext.World->InitWorld();

	PreviewActor = WorldContext.World->SpawnActor<AActor>();
	PreviewActor->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));
	PreviewActor->bTickInEditor = false;

	PreviewParticleComponent = PreviewActor->AddComponent<UParticleSystemComponent>();
	PreviewActor->SetRootComponent(PreviewParticleComponent);
	PreviewParticleComponent->SetTemplate(GetEditedSystem());
	PreviewParticleComponent->Activate(true);

	ImVec2 ViewportSize = ImGui::GetContentRegionAvail();
	ViewportClient.Initialize(GEngine->GetRenderer().GetFD3DDevice().GetDevice(),
		static_cast<uint32>((std::max)(1.0f, ViewportSize.x)),
		static_cast<uint32>((std::max)(1.0f, ViewportSize.y)));
	ViewportClient.SetPreviewWorld(WorldContext.World);
	ViewportClient.SetPreviewActor(PreviewActor);
	ViewportClient.SetPreviewMeshComponent(nullptr);
	ViewportClient.ResetCameraToPreviewBounds();

	WorldContext.World->SetEditorPOVProvider(&ViewportClient);
	FSlateApplication::Get().RegisterViewport(&ViewportClient);

	if (UParticleSystem* System = GetEditedSystem())
	{
		System->BuildEmitters();
	}
}

void FParticleEditorWidget::Close()
{
	if (!IsOpen() && !ViewportClient.IsRenderable())
	{
		return;
	}

	if (UWorld* PreviewWorld = ViewportClient.GetPreviewWorld())
	{
		FScene& PreviewScene = PreviewWorld->GetScene();
		GEngine->GetRenderer().GetResources().ReleaseShadowResourcesForScene(&PreviewScene);
		PreviewWorld->SetEditorPOVProvider(nullptr);

		if (PreviewWorldHandle.IsValid())
		{
			GEngine->DestroyWorldContext(PreviewWorldHandle);
		}
	}

	FSlateApplication::Get().UnregisterViewport(&ViewportClient);
	ViewportClient.Release();
	PreviewActor = nullptr;
	PreviewParticleComponent = nullptr;

	FAssetEditorWidget::Close();
}

void FParticleEditorWidget::Tick(float DeltaTime)
{
	if (!IsOpen())
	{
		return;
	}

	if (ViewportClient.IsRenderable())
	{
		ViewportClient.Tick(DeltaTime);
	}

	if (bSimPlaying && PreviewParticleComponent && PreviewParticleComponent->IsActive())
	{
		const float SafeSpeed = (std::max)(0.0f, SimulationSpeed);
		PreviewParticleComponent->TickComponent(DeltaTime * SafeSpeed, LEVELTICK_ViewportsOnly,
			PreviewParticleComponent->PrimaryComponentTick);
	}
}

void FParticleEditorWidget::CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const
{
	if (IsOpen())
	{
		OutClients.push_back(const_cast<FStaticMeshEditorViewportClient*>(&ViewportClient));
	}
}

void FParticleEditorWidget::Render(float DeltaTime)
{
	(void)DeltaTime;

	if (bPendingClose)
	{
		Close();
		bPendingClose = false;
		return;
	}

	if (!IsOpen() || !EditedObject)
	{
		return;
	}

	UParticleSystem* System = GetEditedSystem();

	bool bWindowOpen = true;
	FString VisibleTitle = "Particle Editor";
	if (System && !System->GetSourcePath().empty())
	{
		VisibleTitle += " - ";
		VisibleTitle += System->GetSourcePath();
	}
	if (IsDirty())
	{
		VisibleTitle += " *";
	}

	ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_None;
	if (ViewportClient.IsMouseOverViewport())
	{
		WindowFlags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
	}

	if (ConsumeFocusRequest())
	{
		ImGui::SetNextWindowFocus();
	}

	FString WindowTitle = VisibleTitle + WindowIdSuffix;
	if (!ImGui::Begin(WindowTitle.c_str(), &bWindowOpen, WindowFlags))
	{
		ImGui::End();
		if (!bWindowOpen)
		{
			bPendingClose = true;
		}
		return;
	}

	RenderDocument(DeltaTime);

	ImGui::End();

	if (!bWindowOpen)
	{
		bPendingClose = true;
	}
}

void FParticleEditorWidget::RenderDocument(float DeltaTime)
{
	(void)DeltaTime;

	if (bPendingClose)
	{
		Close();
		bPendingClose = false;
		return;
	}

	if (!IsOpen() || !EditedObject)
	{
		return;
	}

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
	{
		FSlateApplication::Get().BringViewportToFront(&ViewportClient);
	}

	RenderToolbar();
	ImGui::Separator();

	const float TotalWidth = ImGui::GetContentRegionAvail().x;
	const float TotalHeight = ImGui::GetContentRegionAvail().y;
	const float SplitterThickness = 6.0f;
	const float MinLeftWidth = 300.0f;
	const float MinRightWidth = 360.0f;
	const float MinTopHeight = 180.0f;
	const float MinBottomHeight = 170.0f;

	DetailsWidth = ClampPanelValue(DetailsWidth, MinLeftWidth, TotalWidth - MinRightWidth - SplitterThickness);
	CurvePanelHeight = ClampPanelValue(CurvePanelHeight, MinBottomHeight, TotalHeight - MinTopHeight - SplitterThickness);

	const float LeftWidth = DetailsWidth;
	const float RightWidth = (std::max)(MinRightWidth, TotalWidth - LeftWidth - SplitterThickness);
	const float BottomHeight = CurvePanelHeight;
	const float TopHeight = (std::max)(MinTopHeight, TotalHeight - BottomHeight - SplitterThickness);

	ImGui::BeginGroup();
	RenderPreviewViewport(ImVec2(LeftWidth, TopHeight));
	ImGui::SameLine(0.0f, 0.0f);
	RenderVerticalSplitter("##ParticleVSplitTop", DetailsWidth, MinLeftWidth, TotalWidth - MinRightWidth - SplitterThickness, TopHeight);
	ImGui::SameLine(0.0f, 0.0f);
	RenderEmitterStrip(ImVec2(RightWidth, TopHeight));
	ImGui::EndGroup();

	RenderHorizontalSplitter("##ParticleHSplit", CurvePanelHeight, MinBottomHeight, TotalHeight - MinTopHeight - SplitterThickness, TotalWidth);

	ImGui::BeginGroup();
	RenderPropertyPanel(ImVec2(LeftWidth, BottomHeight));
	ImGui::SameLine(0.0f, 0.0f);
	RenderVerticalSplitter("##ParticleVSplitBottom", DetailsWidth, MinLeftWidth, TotalWidth - MinRightWidth - SplitterThickness, BottomHeight);
	ImGui::SameLine(0.0f, 0.0f);
	RenderCurveEditor(ImVec2(RightWidth, BottomHeight));
	ImGui::EndGroup();
}

FString FParticleEditorWidget::GetDocumentTitle() const
{
	const UParticleSystem* System = GetEditedSystem();
	const FString SourcePath = System ? System->GetSourcePath() : FString();
	return SourcePath.empty() ? FString("Particle") : FString("Particle - " + SourcePath);
}

FString FParticleEditorWidget::GetDocumentPayloadId() const
{
	const UParticleSystem* System = GetEditedSystem();
	const FString SourcePath = System ? System->GetSourcePath() : FString();
	return SourcePath.empty() ? FAssetEditorWidget::GetDocumentPayloadId() : SourcePath;
}

void FParticleEditorWidget::RenderToolbar()
{
	UParticleSystem* System = GetEditedSystem();
	const bool bCanSave = System && !System->GetSourcePath().empty();

	if (!bCanSave) ImGui::BeginDisabled();
	if (ImGui::Button("Save"))
	{
		if (FParticleSystemManager::Get().Save(System))
		{
			ClearDirty();
		}
	}
	if (!bCanSave) ImGui::EndDisabled();

	ImGui::SameLine();
	if (ImGui::Button("Restart Sim"))
	{
		RestartPreview();
	}

	ImGui::SameLine();
	if (ImGui::Button(bSimPlaying ? "Pause" : "Play"))
	{
		bSimPlaying = !bSimPlaying;
	}

	ImGui::SameLine();
	if (ImGui::Button("Reset Camera"))
	{
		ViewportClient.ResetCameraToPreviewBounds();
	}

	ImGui::SameLine();
	ImGui::Checkbox("Vector Field", &ViewportClient.GetRenderOptions().bParticleVectorFieldDebug);

	ImGui::SameLine();
	ImGui::Checkbox("Bounds", &bShowBounds);
	ViewportClient.GetRenderOptions().ShowFlags.bParticleBounds = bShowBounds;

	ImGui::SameLine();
	ImGui::SetNextItemWidth(120.0f);
	if (ImGui::SliderFloat("Speed", &SimulationSpeed, 0.0f, 2.0f, "%.2fx"))
	{
		SimulationSpeed = (std::max)(0.0f, SimulationSpeed);
	}

	ImGui::SameLine();
	ImGui::SetNextItemWidth(70.0f);
	if (ImGui::InputInt("LOD", &CurrentLODIndex))
	{
		if (CurrentLODIndex < 0) CurrentLODIndex = 0;
		if (System)
		{
			const int32 RequestedLODIndex = CurrentLODIndex;
			System->EnsureLODDistances();
			if (RequestedLODIndex >= System->GetMaxLODCount())
			{
				for (UParticleEmitter* Emitter : System->Emitters)
				{
					if (!Emitter) continue;
					while (Emitter->GetLODCount() <= RequestedLODIndex)
					{
						Emitter->CreateLODLevel(Emitter->GetLODCount());
					}
				}
				System->EnsureLODDistances();
				MarkDirty();
			}
			CurrentLODIndex = RequestedLODIndex;
		}
		ApplyCurrentLODToPreview();
	}

	ImGui::SameLine();
	if (ImGui::Button("Select System"))
	{
		SelectSystem();
	}

	ImGui::SameLine();
	if (System)
	{
		ImGui::TextDisabled("Emitters: %d", System->GetEmitterCount());
	}
}

void FParticleEditorWidget::RenderPreviewViewport(ImVec2 Size)
{
	ImGui::BeginChild("##ParticlePreviewPanel", Size, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	const ImVec2 PanelSize = ImGui::GetContentRegionAvail();
	const ImVec2 ViewportPos = ImGui::GetCursorScreenPos();
	ViewportClient.SetViewportRect(ViewportPos.x, ViewportPos.y, PanelSize.x, PanelSize.y);

	FViewport* VP = ViewportClient.GetViewport();
	if (!VP || PanelSize.x <= 0.0f || PanelSize.y <= 0.0f)
	{
		ImGui::Dummy(PanelSize);
		ImGui::EndChild();
		return;
	}

	VP->RequestResize(static_cast<uint32>(PanelSize.x), static_cast<uint32>(PanelSize.y));
	if (VP->GetSRV())
	{
		ImGui::Image((ImTextureID)VP->GetSRV(), PanelSize);
	}
	else
	{
		ImGui::Dummy(PanelSize);
	}

	FSlateApplication::Get().SetViewportImGuiHovered(&ViewportClient, ImGui::IsItemHovered());

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(ViewportPos, ImVec2(ViewportPos.x + PanelSize.x, ViewportPos.y + 28.0f), IM_COL32(30, 30, 30, 220));
	DrawList->AddText(ImVec2(ViewportPos.x + 8.0f, ViewportPos.y + 7.0f), IM_COL32(230, 230, 230, 255), "Viewport");

	const char* StateText = bSimPlaying ? "Sim: Playing" : "Sim: Paused";
	const ImVec2 StateSize = ImGui::CalcTextSize(StateText);
	DrawList->AddText(ImVec2(ViewportPos.x + PanelSize.x - StateSize.x - 8.0f, ViewportPos.y + 7.0f), IM_COL32(170, 220, 170, 255), StateText);

	// Cascade reference view does not draw an additional bottom-left overlay axis here.
	// The preview render target may still draw its own scene gizmos if the viewport client enables them.

	ImGui::EndChild();
}

void FParticleEditorWidget::RenderEmitterStrip(ImVec2 Size)
{
	ImGui::BeginChild("##ParticleEmitterStrip", Size, true, ImGuiWindowFlags_HorizontalScrollbar);
	ImGui::TextUnformatted("Emitters");
	ImGui::SameLine();
	if (ImGui::SmallButton("+ Emitter"))
	{
		AddEmitter();
	}
	ImGui::SameLine();
	if (SelectedEmitterIndex < 0) ImGui::BeginDisabled();
	if (ImGui::SmallButton("- Emitter"))
	{
		RemoveSelectedEmitter();
	}
	if (SelectedEmitterIndex < 0) ImGui::EndDisabled();
	ImGui::SameLine();
	if (SelectedEmitterIndex < 0) ImGui::BeginDisabled();
	if (ImGui::SmallButton("+ Module"))
	{
		AddModuleToSelectedEmitter();
	}
	if (SelectedEmitterIndex < 0) ImGui::EndDisabled();
	ImGui::Separator();
	RenderAddModulePopup();

	UParticleSystem* System = GetEditedSystem();
	if (!System)
	{
		ImGui::TextDisabled("No particle system.");
		ImGui::EndChild();
		return;
	}

	if (System->Emitters.empty())
	{
		ImGui::TextDisabled("No emitters. Click + Emitter.");
		ImGui::EndChild();
		return;
	}

	for (int32 i = 0; i < static_cast<int32>(System->Emitters.size()); ++i)
	{
		RenderEmitterColumn(System->Emitters[i], i);
		if (i + 1 < static_cast<int32>(System->Emitters.size()))
		{
			ImGui::SameLine(0.0f, 2.0f);
		}
	}

	ImGui::EndChild();
}

void FParticleEditorWidget::RenderEmitterColumn(UParticleEmitter* Emitter, int32 EmitterIndex)
{
	const float ColumnWidth = 178.0f;
	ImGui::PushID(EmitterIndex);
	ImGui::BeginGroup();

	const bool bSelectedEmitter = SelectedEmitterIndex == EmitterIndex && SelectedModuleIndex == -1;
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 HeaderPos = ImGui::GetCursorScreenPos();
	const ImVec2 HeaderSize(ColumnWidth, 76.0f);
	const ImVec2 ToggleMin(HeaderPos.x + 6.0f, HeaderPos.y + 8.0f);
	const ImVec2 ToggleMax(ToggleMin.x + 17.0f, ToggleMin.y + 17.0f);
	DrawList->AddRectFilled(HeaderPos, ImVec2(HeaderPos.x + HeaderSize.x, HeaderPos.y + HeaderSize.y),
		bSelectedEmitter ? IM_COL32(224, 119, 55, 255) : (Emitter && !Emitter->bEnabled ? IM_COL32(40, 40, 44, 255) : IM_COL32(58, 58, 62, 255)), 2.0f);

	ImGui::InvisibleButton("##EmitterHeader", HeaderSize);
	const bool bToggleHovered = ImGui::IsMouseHoveringRect(ToggleMin, ToggleMax);
	if (bToggleHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && Emitter)
	{
		Emitter->bEnabled = !Emitter->bEnabled;
		SelectEmitter(EmitterIndex);
		NotifyParticleAssetChanged(true);
	}
	else if (ImGui::IsItemClicked())
	{
		SelectEmitter(EmitterIndex);
	}
	if (ImGui::BeginPopupContextItem("##EmitterHeaderContext"))
	{
		SelectEmitter(EmitterIndex);
		if (Emitter)
		{
			UParticleLODLevel* ContextLOD = Emitter->GetCurrentLODLevel(CurrentLODIndex);
			if (ContextLOD)
			{
				if (ImGui::BeginMenu("Location"))
				{
					if (ImGui::MenuItem("Initial Location"))
					{
						UParticleModule* Module = CreateParticleModule<UParticleModuleLocation>(ContextLOD, Emitter);
						if (Module && ContextLOD->AddModule(Module)) { SelectedModuleIndex = static_cast<int32>(ContextLOD->Modules.size()) - 1; NotifyParticleAssetChanged(true); }
						else if (Module) { UObjectManager::Get().DestroyObject(Module); }
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Velocity"))
				{
					if (ImGui::MenuItem("Initial Velocity"))
					{
						UParticleModule* Module = CreateParticleModule<UParticleModuleVelocity>(ContextLOD, Emitter);
						if (Module && ContextLOD->AddModule(Module)) { SelectedModuleIndex = static_cast<int32>(ContextLOD->Modules.size()) - 1; NotifyParticleAssetChanged(true); }
						else if (Module) { UObjectManager::Get().DestroyObject(Module); }
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Acceleration"))
				{
					if (ImGui::MenuItem("Const Acceleration"))
					{
						UParticleModule* Module = CreateParticleModule<UParticleModuleAcceleration>(ContextLOD, Emitter);
						if (Module && ContextLOD->AddModule(Module)) { SelectedModuleIndex = static_cast<int32>(ContextLOD->Modules.size()) - 1; NotifyParticleAssetChanged(true); }
						else if (Module) { UObjectManager::Get().DestroyObject(Module); }
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Lifetime"))
				{
					if (ImGui::MenuItem("Lifetime"))
					{
						UParticleModule* Module = CreateParticleModule<UParticleModuleLifetime>(ContextLOD, Emitter);
						if (Module && ContextLOD->AddModule(Module)) { SelectedModuleIndex = static_cast<int32>(ContextLOD->Modules.size()) - 1; NotifyParticleAssetChanged(true); }
						else if (Module) { UObjectManager::Get().DestroyObject(Module); }
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Color"))
				{
					if (ImGui::MenuItem("Initial Color"))
					{
						UParticleModule* Module = CreateParticleModule<UParticleModuleColor>(ContextLOD, Emitter);
						if (Module && ContextLOD->AddModule(Module)) { SelectedModuleIndex = static_cast<int32>(ContextLOD->Modules.size()) - 1; NotifyParticleAssetChanged(true); }
						else if (Module) { UObjectManager::Get().DestroyObject(Module); }
					}
					if (ImGui::MenuItem("Color Over Life"))
					{
						UParticleModule* Module = CreateParticleModule<UParticleModuleColorOverLife>(ContextLOD, Emitter);
						if (Module && ContextLOD->AddModule(Module)) { SelectedModuleIndex = static_cast<int32>(ContextLOD->Modules.size()) - 1; NotifyParticleAssetChanged(true); }
						else if (Module) { UObjectManager::Get().DestroyObject(Module); }
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Size"))
				{
					if (ImGui::MenuItem("Initial Size"))
					{
						UParticleModule* Module = CreateParticleModule<UParticleModuleSize>(ContextLOD, Emitter);
						if (Module && ContextLOD->AddModule(Module)) { SelectedModuleIndex = static_cast<int32>(ContextLOD->Modules.size()) - 1; NotifyParticleAssetChanged(true); }
						else if (Module) { UObjectManager::Get().DestroyObject(Module); }
					}
					if (ImGui::MenuItem("Size By Life"))
					{
						UParticleModule* Module = CreateParticleModule<UParticleModuleSizeByLife>(ContextLOD, Emitter);
						if (Module && ContextLOD->AddModule(Module)) { SelectedModuleIndex = static_cast<int32>(ContextLOD->Modules.size()) - 1; NotifyParticleAssetChanged(true); }
						else if (Module) { UObjectManager::Get().DestroyObject(Module); }
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Collision"))
				{
					if (ImGui::MenuItem("Collision"))
					{
						UParticleModule* Module = CreateParticleModule<UParticleModuleCollision>(ContextLOD, Emitter);
						if (Module && ContextLOD->AddModule(Module)) { SelectedModuleIndex = static_cast<int32>(ContextLOD->Modules.size()) - 1; NotifyParticleAssetChanged(true); }
						else if (Module) { UObjectManager::Get().DestroyObject(Module); }
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Event"))
				{
					if (ImGui::MenuItem("Event Generator"))
					{
						UParticleModule* Module = CreateParticleModule<UParticleModuleEventGenerator>(ContextLOD, Emitter);
						if (Module && ContextLOD->AddModule(Module)) { SelectedModuleIndex = static_cast<int32>(ContextLOD->Modules.size()) - 1; NotifyParticleAssetChanged(true); }
						else if (Module) { UObjectManager::Get().DestroyObject(Module); }
					}
					if (ImGui::MenuItem("Event Receiver Spawn"))
					{
						UParticleModule* Module = CreateParticleModule<UParticleModuleEventReceiverSpawn>(ContextLOD, Emitter);
						if (Module && ContextLOD->AddModule(Module)) { SelectedModuleIndex = static_cast<int32>(ContextLOD->Modules.size()) - 1; NotifyParticleAssetChanged(true); }
						else if (Module) { UObjectManager::Get().DestroyObject(Module); }
					}
					if (ImGui::MenuItem("Event Receiver Kill All"))
					{
						UParticleModule* Module = CreateParticleModule<UParticleModuleEventReceiverKillAll>(ContextLOD, Emitter);
						if (Module && ContextLOD->AddModule(Module)) { SelectedModuleIndex = static_cast<int32>(ContextLOD->Modules.size()) - 1; NotifyParticleAssetChanged(true); }
						else if (Module) { UObjectManager::Get().DestroyObject(Module); }
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("SubUV"))
				{
					if (ImGui::MenuItem("Sub Image Index"))
					{
						UParticleModule* Module = CreateParticleModule<UParticleModuleSubUV>(ContextLOD, Emitter);
						if (Module && ContextLOD->AddModule(Module)) { SelectedModuleIndex = static_cast<int32>(ContextLOD->Modules.size()) - 1; NotifyParticleAssetChanged(true); }
						else if (Module) { UObjectManager::Get().DestroyObject(Module); }
					}
					if (ImGui::MenuItem("SubUV Movie"))
					{
						UParticleModule* Module = CreateParticleModule<UParticleModuleSubUVMovie>(ContextLOD, Emitter);
						if (Module && ContextLOD->AddModule(Module)) { SelectedModuleIndex = static_cast<int32>(ContextLOD->Modules.size()) - 1; NotifyParticleAssetChanged(true); }
						else if (Module) { UObjectManager::Get().DestroyObject(Module); }
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Beam"))
				{
					if (ImGui::MenuItem("Beam Source"))
					{
						UParticleModule* Module = CreateParticleModule<UParticleModuleBeamSource>(ContextLOD, Emitter);
						if (Module && ContextLOD->AddModule(Module)) { SelectedModuleIndex = static_cast<int32>(ContextLOD->Modules.size()) - 1; NotifyParticleAssetChanged(true); }
						else if (Module) { UObjectManager::Get().DestroyObject(Module); }
					}
					if (ImGui::MenuItem("Beam Target"))
					{
						UParticleModule* Module = CreateParticleModule<UParticleModuleBeamTarget>(ContextLOD, Emitter);
						if (Module && ContextLOD->AddModule(Module)) { SelectedModuleIndex = static_cast<int32>(ContextLOD->Modules.size()) - 1; NotifyParticleAssetChanged(true); }
						else if (Module) { UObjectManager::Get().DestroyObject(Module); }
					}
					if (ImGui::MenuItem("Beam Noise"))
					{
						UParticleModule* Module = CreateParticleModule<UParticleModuleBeamNoise>(ContextLOD, Emitter);
						if (Module && ContextLOD->AddModule(Module)) { SelectedModuleIndex = static_cast<int32>(ContextLOD->Modules.size()) - 1; NotifyParticleAssetChanged(true); }
						else if (Module) { UObjectManager::Get().DestroyObject(Module); }
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("TypeData"))
				{
					const bool bHasTypeData = ContextLOD->TypeDataModule != nullptr;
					if (bHasTypeData) ImGui::BeginDisabled();
					if (ImGui::MenuItem("TypeData Mesh"))
					{
						UParticleModule* Module = CreateParticleModule<UParticleModuleTypeDataMesh>(ContextLOD, Emitter);
						if (Module && ContextLOD->AddModule(Module)) { SelectedModuleIndex = ModuleTokenTypeData; NotifyParticleAssetChanged(true); }
						else if (Module) { UObjectManager::Get().DestroyObject(Module); }
					}
					if (ImGui::MenuItem("TypeData Beam"))
					{
						UParticleModule* Module = CreateParticleModule<UParticleModuleTypeDataBeam>(ContextLOD, Emitter);
						if (Module && ContextLOD->AddModule(Module)) { SelectedModuleIndex = ModuleTokenTypeData; NotifyParticleAssetChanged(true); }
						else if (Module) { UObjectManager::Get().DestroyObject(Module); }
					}
					if (ImGui::MenuItem("TypeData Ribbon"))
					{
						UParticleModule* Module = CreateParticleModule<UParticleModuleTypeDataRibbon>(ContextLOD, Emitter);
						if (Module && ContextLOD->AddModule(Module)) { SelectedModuleIndex = ModuleTokenTypeData; NotifyParticleAssetChanged(true); }
						else if (Module) { UObjectManager::Get().DestroyObject(Module); }
					}
					if (bHasTypeData) ImGui::EndDisabled();
					ImGui::EndMenu();
				}
			}
			ImGui::Separator();
			if (ImGui::MenuItem(Emitter->bEnabled ? "Disable Emitter" : "Enable Emitter"))
			{
				Emitter->bEnabled = !Emitter->bEnabled;
				NotifyParticleAssetChanged(true);
			}
			if (ImGui::MenuItem("Delete Emitter"))
			{
				RemoveSelectedEmitter();
			}
		}
		ImGui::EndPopup();
	}

	const char* Name = Emitter ? Emitter->EmitterName.c_str() : "Null Emitter";
	DrawList->AddText(ImVec2(HeaderPos.x + 28.0f, HeaderPos.y + 7.0f), IM_COL32(245, 245, 245, 255), Name);
	DrawList->AddRectFilled(ToggleMin, ToggleMax, Emitter && Emitter->bEnabled ? IM_COL32(48, 118, 54, 255) : IM_COL32(130, 38, 38, 255), 2.0f);
	DrawList->AddRect(ToggleMin, ToggleMax, IM_COL32(210, 210, 210, 220), 2.0f);
	if (Emitter && Emitter->bEnabled)
	{
		DrawList->AddLine(ImVec2(ToggleMin.x + 4.0f, ToggleMin.y + 9.0f), ImVec2(ToggleMin.x + 7.0f, ToggleMin.y + 12.0f), IM_COL32(255, 255, 255, 255), 2.0f);
		DrawList->AddLine(ImVec2(ToggleMin.x + 7.0f, ToggleMin.y + 12.0f), ImVec2(ToggleMin.x + 13.0f, ToggleMin.y + 5.0f), IM_COL32(255, 255, 255, 255), 2.0f);
	}
	else
	{
		DrawList->AddLine(ImVec2(ToggleMin.x + 5.0f, ToggleMin.y + 5.0f), ImVec2(ToggleMax.x - 5.0f, ToggleMax.y - 5.0f), IM_COL32(255, 255, 255, 255), 2.0f);
		DrawList->AddLine(ImVec2(ToggleMax.x - 5.0f, ToggleMin.y + 5.0f), ImVec2(ToggleMin.x + 5.0f, ToggleMax.y - 5.0f), IM_COL32(255, 255, 255, 255), 2.0f);
	}
	if (Emitter)
	{
		UParticleLODLevel* HeaderLOD = Emitter->GetCurrentLODLevel(CurrentLODIndex);
		const char* PreviewType = "Sprite";
		if (HeaderLOD && Cast<UParticleModuleTypeDataMesh>(HeaderLOD->TypeDataModule)) PreviewType = "Mesh";
		else if (HeaderLOD && Cast<UParticleModuleTypeDataBeam>(HeaderLOD->TypeDataModule)) PreviewType = "Beam";
		else if (HeaderLOD && Cast<UParticleModuleTypeDataRibbon>(HeaderLOD->TypeDataModule)) PreviewType = "Ribbon";
		DrawList->AddText(ImVec2(HeaderPos.x + 8.0f, HeaderPos.y + 31.0f), Emitter->bEnabled ? IM_COL32(190, 220, 190, 255) : IM_COL32(150, 150, 150, 255), PreviewType);
		char CountText[64] = {};
		std::snprintf(CountText, sizeof(CountText), "LOD %d / %d", CurrentLODIndex, Emitter->GetLODCount());
		DrawList->AddText(ImVec2(HeaderPos.x + 8.0f, HeaderPos.y + 50.0f), IM_COL32(165, 165, 165, 255), CountText);
	}

	if (!Emitter)
	{
		ImGui::EndGroup();
		ImGui::PopID();
		return;
	}


	UParticleLODLevel* LOD = Emitter->GetCurrentLODLevel(CurrentLODIndex);
	if (!LOD)
	{
		ImGui::TextDisabled("No LOD");
		ImGui::EndGroup();
		ImGui::PopID();
		return;
	}

	RenderModuleCard(LOD, LOD->RequiredModule, ModuleTokenRequired);
	RenderModuleCard(LOD, LOD->SpawnModule, ModuleTokenSpawn);
	if (LOD->TypeDataModule)
	{
		RenderModuleCard(LOD, LOD->TypeDataModule, ModuleTokenTypeData);
	}
	else
	{
		const bool bSelected = SelectedEmitterIndex == EmitterIndex && SelectedModuleIndex == ModuleTokenTypeData;
		ImDrawList* LocalDrawList = ImGui::GetWindowDrawList();
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		LocalDrawList->AddRectFilled(Pos, ImVec2(Pos.x + ColumnWidth, Pos.y + 24.0f),
			bSelected ? IM_COL32(224, 119, 55, 255) : IM_COL32(35, 35, 38, 255));
		ImGui::InvisibleButton("##SpriteTypeData", ImVec2(ColumnWidth, 24.0f));
		if (ImGui::IsItemClicked())
		{
			SelectModule(EmitterIndex, ModuleTokenTypeData);
		}
		LocalDrawList->AddText(ImVec2(Pos.x + 8.0f, Pos.y + 5.0f), IM_COL32(180, 180, 180, 255), "TypeData: Sprite");
	}

	for (int32 ModuleIndex = 0; ModuleIndex < static_cast<int32>(LOD->Modules.size()); ++ModuleIndex)
	{
		RenderModuleCard(LOD, LOD->Modules[ModuleIndex], ModuleIndex);
	}

	ImGui::EndGroup();
	ImGui::PopID();
}

void FParticleEditorWidget::RenderModuleCard(UParticleLODLevel* LOD, UParticleModule* Module, int32 ModuleIndex)
{
	const float CardWidth = 178.0f;
	const float CardHeight = 24.0f;
	if (!Module)
	{
		ImGui::Dummy(ImVec2(CardWidth, CardHeight));
		return;
	}

	ImGui::PushID(ModuleIndex);
	int32 OwnerEmitterIndex = -1;
	if (LOD)
	{
		UParticleEmitter* OwnerEmitter = Cast<UParticleEmitter>(LOD->GetOuter());
		if (UParticleSystem* System = GetEditedSystem())
		{
			for (int32 i = 0; i < static_cast<int32>(System->Emitters.size()); ++i)
			{
				if (System->Emitters[i] == OwnerEmitter)
				{
					OwnerEmitterIndex = i;
					break;
				}
			}
		}
	}

	const bool bSelected = OwnerEmitterIndex == SelectedEmitterIndex && SelectedModuleIndex == ModuleIndex;
	const bool bEnabled = Module->IsEnabled();
	const bool bCurveCapable = ModuleCanOpenCurvePanel(Module);
	const bool bCoreLocked = IsCoreModuleToken(ModuleIndex);
	const ImVec2 Pos = ImGui::GetCursorScreenPos();
	const ImVec2 CheckMin(Pos.x + CardWidth - 39.0f, Pos.y + 5.0f);
	const ImVec2 CheckMax(CheckMin.x + 13.0f, CheckMin.y + 13.0f);
	const ImVec2 GraphMin(Pos.x + CardWidth - 20.0f, Pos.y + 5.0f);
	const ImVec2 GraphMax(GraphMin.x + 13.0f, GraphMin.y + 13.0f);
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(Pos, ImVec2(Pos.x + CardWidth, Pos.y + CardHeight),
		CategoryColor(Module->GetCategory(), bSelected, bEnabled));
	DrawList->AddText(ImVec2(Pos.x + 7.0f, Pos.y + 5.0f), bEnabled ? IM_COL32(255, 255, 255, 255) : IM_COL32(150, 150, 150, 255), Module->GetDisplayName());

	// Cascade 스타일: 모듈 이름 오른쪽에 Enabled 체크박스, 그 옆에 Curve 표시 버튼.
	DrawList->AddRectFilled(CheckMin, CheckMax, IM_COL32(24, 24, 26, 255), 2.0f);
	DrawList->AddRect(CheckMin, CheckMax, IM_COL32(175, 175, 175, 255), 2.0f);
	if (bEnabled)
	{
		DrawList->AddLine(ImVec2(CheckMin.x + 3.0f, CheckMin.y + 7.0f), ImVec2(CheckMin.x + 6.0f, CheckMin.y + 10.0f), IM_COL32(235, 235, 235, 255), 2.0f);
		DrawList->AddLine(ImVec2(CheckMin.x + 6.0f, CheckMin.y + 10.0f), ImVec2(CheckMin.x + 11.0f, CheckMin.y + 3.0f), IM_COL32(235, 235, 235, 255), 2.0f);
	}
	else
	{
		DrawList->AddLine(ImVec2(CheckMin.x + 4.0f, CheckMin.y + 4.0f), ImVec2(CheckMax.x - 4.0f, CheckMax.y - 4.0f), IM_COL32(235, 80, 80, 255), 1.5f);
		DrawList->AddLine(ImVec2(CheckMax.x - 4.0f, CheckMin.y + 4.0f), ImVec2(CheckMin.x + 4.0f, CheckMax.y - 4.0f), IM_COL32(235, 80, 80, 255), 1.5f);
	}

	DrawList->AddRectFilled(GraphMin, GraphMax, bCurveCapable ? IM_COL32(42, 95, 42, 255) : IM_COL32(42, 42, 46, 255), 2.0f);
	DrawList->AddRect(GraphMin, GraphMax, bCurveCapable ? IM_COL32(145, 220, 145, 255) : IM_COL32(90, 90, 94, 255), 2.0f);
	DrawList->AddLine(ImVec2(GraphMin.x + 2.0f, GraphMax.y - 3.0f), ImVec2(GraphMin.x + 5.0f, GraphMin.y + 7.0f), bCurveCapable ? IM_COL32(160, 240, 160, 255) : IM_COL32(110, 110, 110, 255), 1.3f);
	DrawList->AddLine(ImVec2(GraphMin.x + 5.0f, GraphMin.y + 7.0f), ImVec2(GraphMin.x + 8.0f, GraphMin.y + 9.0f), bCurveCapable ? IM_COL32(160, 240, 160, 255) : IM_COL32(110, 110, 110, 255), 1.3f);
	DrawList->AddLine(ImVec2(GraphMin.x + 8.0f, GraphMin.y + 9.0f), ImVec2(GraphMax.x - 2.0f, GraphMin.y + 3.0f), bCurveCapable ? IM_COL32(160, 240, 160, 255) : IM_COL32(110, 110, 110, 255), 1.3f);

	ImGui::InvisibleButton("##ModuleCard", ImVec2(CardWidth, CardHeight));
	const bool bClickedEnabledToggle = ImGui::IsMouseHoveringRect(CheckMin, CheckMax) && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	const bool bClickedCurveButton = ImGui::IsMouseHoveringRect(GraphMin, GraphMax) && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	if (bClickedEnabledToggle)
	{
		Module->SetEnabled(!bEnabled);
		SelectModule(OwnerEmitterIndex, ModuleIndex);
		NotifyParticleAssetChanged(true);
	}
	else if (bClickedCurveButton)
	{
		SelectModuleCurve(OwnerEmitterIndex, ModuleIndex);
	}
	else if (ImGui::IsItemClicked())
	{
		SelectModule(OwnerEmitterIndex, ModuleIndex);
	}

	// 일반 Module 배열 안에서만 순서 변경을 허용한다.
	// Required / Spawn / TypeData 는 고정 슬롯이라 drag reorder 대상에서 제외한다.
	if (ModuleIndex >= 0 && OwnerEmitterIndex >= 0)
	{
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
		{
			if (SelectedEmitterIndex != OwnerEmitterIndex || SelectedModuleIndex != ModuleIndex)
			{
				SelectModule(OwnerEmitterIndex, ModuleIndex);
			}

			FParticleModuleDragPayload Payload;
			Payload.EmitterIndex = OwnerEmitterIndex;
			Payload.ModuleIndex = ModuleIndex;
			ImGui::SetDragDropPayload(ParticleModuleDragPayloadType, &Payload, sizeof(Payload));
			ImGui::Text("Move %s", Module->GetDisplayName());
			ImGui::TextDisabled("Drop inside the same emitter only");
			ImGui::EndDragDropSource();
		}

		if (ImGui::BeginDragDropTarget())
		{
			const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(ParticleModuleDragPayloadType, ImGuiDragDropFlags_AcceptBeforeDelivery);
			if (Payload && Payload->DataSize == sizeof(FParticleModuleDragPayload))
			{
				const FParticleModuleDragPayload* DragPayload = static_cast<const FParticleModuleDragPayload*>(Payload->Data);
				if (DragPayload && DragPayload->EmitterIndex == OwnerEmitterIndex && DragPayload->ModuleIndex >= 0)
				{
					const bool bDropAfterTarget = ImGui::GetMousePos().y > Pos.y + CardHeight * 0.5f;
					const float DropLineY = bDropAfterTarget ? Pos.y + CardHeight : Pos.y;
					DrawList->AddLine(
						ImVec2(Pos.x + 2.0f, DropLineY),
						ImVec2(Pos.x + CardWidth - 2.0f, DropLineY),
						IM_COL32(255, 178, 55, 255),
						3.0f);

					if (Payload->IsDelivery())
					{
						int32 NewModuleIndex = DragPayload->ModuleIndex;
						if (MoveModuleInLOD(LOD, DragPayload->ModuleIndex, ModuleIndex, bDropAfterTarget, NewModuleIndex))
						{
							SelectedEmitterIndex = OwnerEmitterIndex;
							SelectedModuleIndex = NewModuleIndex;
							NotifyParticleAssetChanged(true);
						}
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
	}

	if (ImGui::IsMouseHoveringRect(CheckMin, CheckMax))
	{
		ImGui::SetTooltip("Enable / Disable module");
	}
	else if (ImGui::IsMouseHoveringRect(GraphMin, GraphMax))
	{
		ImGui::SetTooltip(bCurveCapable ? "Show this module in Curve Editor" : "This module has no curve editor data");
	}

	if (ImGui::BeginPopupContextItem("##ModuleContext"))
	{
		if (ModuleIndex < 0) ImGui::BeginDisabled();
		if (ImGui::MenuItem("Duplicate Module"))
		{
			SelectedEmitterIndex = OwnerEmitterIndex;
			SelectedModuleIndex = ModuleIndex;
			DuplicateSelectedModule();
		}
		if (ModuleIndex < 0) ImGui::EndDisabled();

		if (bCoreLocked) ImGui::BeginDisabled();
		const char* DeleteLabel = ModuleIndex == ModuleTokenTypeData ? "Clear TypeData" : "Delete Module";
		if (ImGui::MenuItem(DeleteLabel))
		{
			SelectedEmitterIndex = OwnerEmitterIndex;
			SelectedModuleIndex = ModuleIndex;
			RemoveSelectedModule();
		}
		if (bCoreLocked) ImGui::EndDisabled();

		if (bCoreLocked)
		{
			ImGui::TextDisabled("Required / Spawn modules cannot be deleted.");
		}
		ImGui::EndPopup();
	}

	ImGui::PopID();
}

void FParticleEditorWidget::RenderPropertyPanel(ImVec2 Size)
{
	ImGui::BeginChild("##ParticleDetails", Size, true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
	ImGui::TextUnformatted("Details");
	ImGui::SameLine();
	ImGui::TextDisabled("%s", ModuleSelectionLabel(SelectedModuleIndex).c_str());
	ImGui::Separator();

	UParticleSystem* System = GetEditedSystem();
	if (!System)
	{
		ImGui::TextDisabled("No particle system.");
		ImGui::EndChild();
		return;
	}

	bool bChanged = false;
	bool bCurveKeyChanged = false;
	bool bResetPreview = true;
	UParticleModule* SelectedModuleForReset = nullptr;

	if (SelectedEmitterIndex < 0)
	{
		if (ImGui::CollapsingHeader("System", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bChanged |= ImGui::Checkbox("Looping", &System->bLooping);
			bChanged |= ImGui::DragFloat("Update Time FPS", &System->UpdateTimeFPS, 1.0f, 10.0f, 120.0f, "%.0f");
			bChanged |= ImGui::Checkbox("Use Fixed Relative Bounding Box", &System->bUseFixedRelativeBoundingBox);
			bChanged |= DragFloat3Field("Bounds Min", System->SystemBoundsMin, 1.0f);
			bChanged |= DragFloat3Field("Bounds Max", System->SystemBoundsMax, 1.0f);
			ImGui::TextDisabled("Path: %s", System->GetSourcePath().empty() ? "Unsaved asset" : System->GetSourcePath().c_str());
		}
		if (ImGui::CollapsingHeader("LOD", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bChanged |= ImGui::Checkbox("Use Automatic LOD", &System->bUseAutomaticLOD);
			bChanged |= ImGui::DragFloat("LOD Distance Hysteresis", &System->LODDistanceHysteresis, 1.0f, 0.0f, 1000000.0f, "%.1f");
			bChanged |= ImGui::DragFloat("LOD Switch Delay", &System->LODSwitchDelay, 0.01f, 0.0f, 60.0f, "%.2f");

			System->EnsureLODDistances();
			const int32 LODCount = System->GetMaxLODCount();
			ImGui::Text("LOD Count: %d", LODCount);
			if (ImGui::Button("Add LOD"))
			{
				const int32 NewLODIndex = LODCount;
				for (UParticleEmitter* Emitter : System->Emitters)
				{
					if (!Emitter) continue;
					while (Emitter->GetLODCount() <= NewLODIndex)
					{
						Emitter->CreateLODLevel(Emitter->GetLODCount());
					}
				}
				CurrentLODIndex = NewLODIndex;
				System->EnsureLODDistances();
				ApplyCurrentLODToPreview();
				NotifyParticleAssetChanged(true);
			}
			ImGui::SameLine();
			if (LODCount <= 1) ImGui::BeginDisabled();
			if (ImGui::Button("Remove Current LOD"))
			{
				for (UParticleEmitter* Emitter : System->Emitters)
				{
					if (!Emitter || Emitter->GetLODCount() <= 1) continue;
					if (CurrentLODIndex < Emitter->GetLODCount())
					{
						Emitter->RemoveLODLevel(CurrentLODIndex);
					}
				}
				System->EnsureLODDistances();
				const int32 NewMaxLODCount = System->GetMaxLODCount();
				if (CurrentLODIndex >= NewMaxLODCount)
				{
					CurrentLODIndex = (std::max)(0, NewMaxLODCount - 1);
				}
				ApplyCurrentLODToPreview();
				NotifyParticleAssetChanged(true);
			}
			if (LODCount <= 1) ImGui::EndDisabled();

			for (int32 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
			{
				ImGui::PushID(LODIndex);
				float Distance = System->GetLODDistance(LODIndex);
				if (LODIndex == 0)
				{
					ImGui::BeginDisabled();
				}
				if (ImGui::DragFloat("Distance", &Distance, 10.0f, 0.0f, 10000000.0f, "%.1f"))
				{
					System->SetLODDistance(LODIndex, Distance);
					bChanged = true;
				}
				if (LODIndex == 0)
				{
					ImGui::EndDisabled();
				}
				ImGui::SameLine();
				ImGui::Text("LOD %d", LODIndex);
				ImGui::PopID();
			}
		}
	}
	else if (UParticleEmitter* Emitter = GetSelectedEmitter())
	{
		UParticleLODLevel* LOD = GetSelectedLOD();
		UParticleModule* Module = GetSelectedModule();
		SelectedModuleForReset = Module;

		if (SelectedModuleIndex == -1)
		{
			if (ImGui::CollapsingHeader("Emitter", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bChanged |= InputTextFString("Emitter Name", Emitter->EmitterName);
				bChanged |= ImGui::Checkbox("Enabled", &Emitter->bEnabled);
				ImGui::Text("LOD Count: %d", Emitter->GetLODCount());
				ImGui::Text("Particle Size: %u bytes", Emitter->GetParticleSize());
				ImGui::Text("Instance Bytes: %u bytes", Emitter->GetReqInstanceBytes());
				if (ImGui::Button("Add LOD"))
				{
					const int32 NewLODIndex = Emitter->GetLODCount();
					Emitter->CreateLODLevel(NewLODIndex);
					CurrentLODIndex = NewLODIndex;
					if (System) System->EnsureLODDistances();
					ApplyCurrentLODToPreview();
					NotifyParticleAssetChanged(true);
				}
				ImGui::SameLine();
				if (Emitter->GetLODCount() <= 1) ImGui::BeginDisabled();
				if (ImGui::Button("Remove Current LOD"))
				{
					Emitter->RemoveLODLevel(CurrentLODIndex);
					if (CurrentLODIndex >= Emitter->GetLODCount()) CurrentLODIndex = (std::max)(0, Emitter->GetLODCount() - 1);
					if (System) System->EnsureLODDistances();
					ApplyCurrentLODToPreview();
					NotifyParticleAssetChanged(true);
				}
				if (Emitter->GetLODCount() <= 1) ImGui::EndDisabled();
			}

			if (LOD && ImGui::CollapsingHeader("Current LOD", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Text("Level: %d", LOD->Level);
				bChanged |= ImGui::Checkbox("LOD Enabled", &LOD->bEnabled);
			}
		}
		else if (SelectedModuleIndex == ModuleTokenTypeData && !Module)
		{
			ImGui::TextWrapped("Sprite emitter: TypeData module is empty. Add Mesh/Beam/Ribbon TypeData from Add Module menu to switch emitter type.");
			if (ImGui::Button("Add TypeData Mesh"))
			{
				if (LOD)
				{
					UParticleModuleTypeDataMesh* NewModule = CreateParticleModule<UParticleModuleTypeDataMesh>(LOD, Emitter);
					if (LOD->AddModule(NewModule))
					{
						NotifyParticleAssetChanged(true);
					}
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Beam"))
			{
				if (LOD)
				{
					UParticleModuleTypeDataBeam* NewModule = CreateParticleModule<UParticleModuleTypeDataBeam>(LOD, Emitter);
					if (LOD->AddModule(NewModule)) NotifyParticleAssetChanged(true);
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Ribbon"))
			{
				if (LOD)
				{
					UParticleModuleTypeDataRibbon* NewModule = CreateParticleModule<UParticleModuleTypeDataRibbon>(LOD, Emitter);
					if (LOD->AddModule(NewModule)) NotifyParticleAssetChanged(true);
				}
			}
		}
		else if (Module)
		{
			ImGui::Text("Module: %s", Module->GetDisplayName());
			ImGui::TextDisabled("Category: %s", CategoryName(Module->GetCategory()));
			bool bModuleEnabled = Module->IsEnabled();
			if (ImGui::Checkbox("Module Enabled", &bModuleEnabled))
			{
				Module->SetEnabled(bModuleEnabled);
				bChanged = true;
			}

			if (UParticleModuleRequired* Required = Cast<UParticleModuleRequired>(Module))
			{
				if (ImGui::CollapsingHeader("Required", ImGuiTreeNodeFlags_DefaultOpen))
				{
					const FString ForcedShader = ParticleForcedShaderPath(LOD);
					if (MaterialComboFieldFiltered("Material", Required->MaterialSlot, ForcedShader))
					{
						Required->CachedMaterial = nullptr;
						bChanged = true;
					}
					// 셰이더는 emitter 타입이 강제 → 그 셰이더 레이아웃과 맞는 머티리얼만 위 목록에 표시.
					ImGui::TextDisabled("Shader (forced by emitter): %s", ForcedShader.c_str());
					// Blend State는 Material(.mat)이 결정 — 에디터 비노출. Material 슬롯에서 변경한다.
					ImGui::TextDisabled("Blend State: from Material (.mat)");
					bChanged |= ImGui::Checkbox("Use Local Space", &Required->bUseLocalSpace);
					bChanged |= ImGui::DragInt("SubImages Horizontal", &Required->SubImagesHorizontal, 1.0f, 1, 64);
					bChanged |= ImGui::DragInt("SubImages Vertical", &Required->SubImagesVertical, 1.0f, 1, 64);
					bChanged |= ImGui::DragFloat("Emitter Duration", &Required->EmitterDuration, 0.05f, 0.0f, 9999.0f, "%.2f");
					bChanged |= ImGui::DragInt("Emitter Loops", &Required->EmitterLoops, 1.0f, 0, 9999);
					static const char* SortNames[] = { "None", "ViewProjDepth", "ViewDistance", "Age_OldestFirst", "Age_NewestFirst" };
					int32 Sort = static_cast<int32>(Required->SortMode);
					if (ComboInt("Sort Mode", Sort, SortNames, 5)) { Required->SortMode = static_cast<UParticleModuleRequired::ESortMode>(Sort); bChanged = true; }
					static const char* AlignNames[] = { "Square", "Rectangle", "Velocity", "FacingCameraPosition" };
					int32 Align = static_cast<int32>(Required->ScreenAlignment);
					if (ComboInt("Screen Alignment", Align, AlignNames, 4)) { Required->ScreenAlignment = static_cast<UParticleModuleRequired::EScreenAlignment>(Align); bChanged = true; }
				}
			}
			else if (UParticleModuleSpawn* Spawn = Cast<UParticleModuleSpawn>(Module))
			{
				if (ImGui::CollapsingHeader("Spawn", ImGuiTreeNodeFlags_DefaultOpen))
				{
					bChanged |= DrawFloatDistributionEditor("Rate", Spawn->RateDistribution, Spawn, 1.0f, 0.0f, 10000.0f, "EmitterTime");
					ImGui::Separator();
					bChanged |= DrawFloatDistributionEditor("Rate Scale", Spawn->RateScaleDistribution, Spawn, 0.01f, 0.0f, 10.0f, "EmitterTime");
					if (ImGui::TreeNodeEx("Bursts", ImGuiTreeNodeFlags_DefaultOpen))
					{
						for (int32 i = 0; i < static_cast<int32>(Spawn->BurstList.size()); ++i)
						{
							ImGui::PushID(i);
							ImGui::Separator();
							ImGui::Text("Burst %d", i);
							bChanged |= ImGui::DragFloat("Time", &Spawn->BurstList[i].Time, 0.01f, 0.0f, 9999.0f, "%.2f");
							bChanged |= ImGui::DragInt("Count", &Spawn->BurstList[i].Count, 1.0f, 0, 100000);
							if (ImGui::SmallButton("Remove Burst"))
							{
								Spawn->BurstList.erase(Spawn->BurstList.begin() + i);
								bChanged = true;
								ImGui::PopID();
								break;
							}
							ImGui::PopID();
						}
						if (ImGui::Button("Add Burst"))
						{
							FBurstEntry Entry;
							Entry.Time = 0.0f;
							Entry.Count = 10;
							Spawn->BurstList.push_back(Entry);
							bChanged = true;
						}
						ImGui::TreePop();
					}
				}
			}
			else if (UParticleModuleLifetime* Lifetime = Cast<UParticleModuleLifetime>(Module))
			{
				if (ImGui::CollapsingHeader("Lifetime", ImGuiTreeNodeFlags_DefaultOpen))
				{
					bChanged |= DrawFloatDistributionEditor("Lifetime", Lifetime->LifetimeDistribution, Lifetime, 0.05f, 0.001f, 60.0f, "SpawnTime");
				}
			}
			else if (UParticleModuleLocation* Location = Cast<UParticleModuleLocation>(Module))
			{
				if (ImGui::CollapsingHeader("Location", ImGuiTreeNodeFlags_DefaultOpen))
				{
					bChanged |= DrawVectorDistributionEditor("Start Location", Location->StartLocationDistribution, Location, 0.1f, 0.0f, 0.0f, "SpawnTime");
					bChanged |= ImGui::Checkbox("World Space Override", &Location->bWorldSpaceOverride);
				}
			}
			else if (UParticleModuleVelocity* Velocity = Cast<UParticleModuleVelocity>(Module))
			{
				if (ImGui::CollapsingHeader("Velocity", ImGuiTreeNodeFlags_DefaultOpen))
				{
					bChanged |= DrawVectorDistributionEditor("Start Velocity", Velocity->StartVelocityDistribution, Velocity, 0.1f, 0.0f, 0.0f, "SpawnTime");
					bChanged |= ImGui::Checkbox("In World Space", &Velocity->bInWorldSpace);
				}
			}
			else if (UParticleModuleVectorFieldLocal* VectorField = Cast<UParticleModuleVectorFieldLocal>(Module))
			{
				if (ImGui::CollapsingHeader("Local Vector Field", ImGuiTreeNodeFlags_DefaultOpen))
				{
					if (VectorFieldComboField("Vector Field", VectorField->VectorFieldPath))
					{
						bChanged = true;
					}
					bChanged |= ImGui::DragFloat("Intensity", &VectorField->Intensity, 0.1f, 0.0f, 10000.0f, "%.3f");
					bChanged |= DragFloat3Field("Field Bounds Offset", VectorField->FieldBoundsOffset, 0.1f);
					bChanged |= DragFloat3Field("Field Bounds Scale", VectorField->FieldBoundsScale, 0.1f, 0.001f, 100000.0f);
					bChanged |= ImGui::Checkbox("Use Trilinear Sampling", &VectorField->bUseTrilinearSampling);
					bChanged |= ImGui::Checkbox("Draw Bounds", &VectorField->bDrawBounds);
					bChanged |= ImGui::Checkbox("Draw Vectors", &VectorField->bDrawVectors);
					bChanged |= ImGui::DragFloat("Vector Draw Scale", &VectorField->DebugVectorScale, 0.1f, 0.0f, 10000.0f, "%.3f");
				}
			}
			else if (UParticleModuleVectorFieldRotation* VectorFieldRotation = Cast<UParticleModuleVectorFieldRotation>(Module))
			{
				if (ImGui::CollapsingHeader("Vector Field Rotation", ImGuiTreeNodeFlags_DefaultOpen))
				{
					bChanged |= DragRotatorField("Rotation", VectorFieldRotation->Rotation, 0.1f);
					bChanged |= DragRotatorField("Rotation Rate", VectorFieldRotation->RotationRate, 0.1f);
				}
			}
			else if (UParticleModuleAcceleration* Acceleration = Cast<UParticleModuleAcceleration>(Module))
			{
				if (ImGui::CollapsingHeader("Const Acceleration", ImGuiTreeNodeFlags_DefaultOpen))
				{
					bChanged |= DrawVectorDistributionEditor("Acceleration", Acceleration->AccelerationDistribution, Acceleration, 0.1f, 0.0f, 0.0f, "SpawnTime");
				}
			}
			else if (UParticleModuleColor* Color = Cast<UParticleModuleColor>(Module))
			{
				if (ImGui::CollapsingHeader("Initial Color", ImGuiTreeNodeFlags_DefaultOpen))
				{
					EnsureInitialColorDistributions(Color);
					bChanged |= DrawVectorDistributionEditor("Color / RGB", Color->StartColorDistribution, Color, 0.01f, 0.0f, 1.0f, "SpawnTime");
					ImGui::Separator();
					bChanged |= DrawFloatDistributionEditor("Alpha", Color->StartAlphaDistribution, Color, 0.01f, 0.0f, 1.0f, "SpawnTime");
				}
			}
			else if (UParticleModuleColorOverLife* ColorOverLife = Cast<UParticleModuleColorOverLife>(Module))
			{
				if (ImGui::CollapsingHeader("Color Over Life", ImGuiTreeNodeFlags_DefaultOpen))
				{
					bChanged |= DrawVectorDistributionEditor("Color / RGB", ColorOverLife->ColorOverLifeDistribution, ColorOverLife, 0.01f, 0.0f, 1.0f, "RelativeTime");
					ImGui::Separator();
					bChanged |= DrawFloatDistributionEditor("Alpha", ColorOverLife->AlphaOverLifeDistribution, ColorOverLife, 0.01f, 0.0f, 1.0f, "RelativeTime");
					bChanged |= ImGui::Checkbox("Multiply Base Color", &ColorOverLife->bMultiplyBaseColor);
				}
			}
			else if (UParticleModuleSize* Size = Cast<UParticleModuleSize>(Module))
			{
				if (ImGui::CollapsingHeader("Initial Size", ImGuiTreeNodeFlags_DefaultOpen))
				{
					bChanged |= DrawVectorDistributionEditor("Start Size", Size->StartSizeDistribution, Size, 0.1f, 0.0f, 10000.0f, "SpawnTime");
				}
			}
			else if (UParticleModuleSizeByLife* SizeByLife = Cast<UParticleModuleSizeByLife>(Module))
			{
				if (ImGui::CollapsingHeader("Size By Life", ImGuiTreeNodeFlags_DefaultOpen))
				{
					bChanged |= DrawVectorDistributionEditor("Life Multiplier", SizeByLife->LifeMultiplierDistribution, SizeByLife, 0.01f, 0.0f, 10000.0f, "RelativeTime");
				}
			}
			else if (UParticleModuleSubUV* SubUV = Cast<UParticleModuleSubUV>(Module))
			{
				if (ImGui::CollapsingHeader("Sub Image Index", ImGuiTreeNodeFlags_DefaultOpen))
				{
					bChanged |= DrawFloatDistributionEditor("Sub Image Index", SubUV->SubImageIndexDistribution, SubUV, 1.0f, 0.0f, 100000.0f, "RelativeTime");
					ImGui::TextDisabled("RelativeTime 0..1 -> evaluated frame index. Frame 0 is valid; -1 is render fallback only.");
				}
			}
			else if (UParticleModuleSubUVMovie* SubUVMovie = Cast<UParticleModuleSubUVMovie>(Module))
			{
				if (ImGui::CollapsingHeader("SubUV Movie", ImGuiTreeNodeFlags_DefaultOpen))
				{
					bChanged |= ImGui::DragInt("Start Frame", &SubUVMovie->StartFrame, 1.0f, 0, 100000);
					bChanged |= ImGui::DragInt("End Frame (-1 = Last)", &SubUVMovie->EndFrame, 1.0f, -1, 100000);
					bChanged |= DrawFloatDistributionEditor("Frame Rate", SubUVMovie->FrameRateDistribution, SubUVMovie, 0.1f, 0.0f, 240.0f, "RelativeTime");
					ImGui::TextDisabled("Frame Rate is FPS. 10.0 means next frame every 0.1 sec; 0 or less means one pass over particle lifetime.");
					bChanged |= ImGui::Checkbox("Is Looped", &SubUVMovie->bLooped);
					bChanged |= ImGui::Checkbox("Random Start Frame", &SubUVMovie->bRandomStartFrame);
				}
			}
			else if (UParticleModuleCollision* Collision = Cast<UParticleModuleCollision>(Module))
			{
				if (ImGui::CollapsingHeader("Collision", ImGuiTreeNodeFlags_DefaultOpen))
				{
					bChanged |= ImGui::DragFloat("Damping Factor", &Collision->DampingFactor, 0.01f, 0.0f, 1.0f, "%.2f");
					bChanged |= ImGui::DragInt("Max Collisions", &Collision->MaxCollisions, 1.0f, 0, 1000);
					ImGui::TextDisabled("Max Collisions is a per-particle hit-count limit, not a per-tick query workload budget.");
					static const char* ChannelNames[] = { "WorldStatic", "WorldDynamic", "Pawn", "Projectile", "Trigger" };
					int32 Channel = static_cast<int32>(Collision->CollisionChannel);
					if (ComboInt("Collision Channel", Channel, ChannelNames, 5)) { Collision->CollisionChannel = static_cast<ECollisionChannel>(Channel); bChanged = true; }
					bChanged |= ImGui::Checkbox("Kill On Collision", &Collision->bKillOnCollision);
					bChanged |= ImGui::Checkbox("Generate Collision Events", &Collision->bGenerateCollisionEvents);

					ImGui::Separator();
					if (ImGui::CollapsingHeader("LOD Policy Override", ImGuiTreeNodeFlags_DefaultOpen))
					{
						const bool bIsLowerLOD = LOD && LOD->Level > 0;
						ImGui::TextDisabled(
							bIsLowerLOD
								? "Current lower-LOD outer collision policy override. This controls workload/reporting, not hit response semantics."
								: "Optional current-LOD outer collision policy override. Mainly useful for reduced lower-LOD tuning.");

						auto& LODPolicyOverride = Collision->LODPolicyOverride;
						bChanged |= ImGui::Checkbox("Override LOD Collision Policy", &LODPolicyOverride.bEnabled);

						if (!LODPolicyOverride.bEnabled) ImGui::BeginDisabled();
						bChanged |= ImGui::DragInt("Collision Query Budget", &LODPolicyOverride.CollisionQueryBudget, 1.0f, 0, 100000);
						ImGui::TextDisabled("Per-emitter per-tick collision query workload for this LOD.");
						bChanged |= ImGui::Checkbox("Override Disable Policy", &LODPolicyOverride.bOverrideDisablePolicy);
						if (!LODPolicyOverride.bOverrideDisablePolicy) ImGui::BeginDisabled();
						bChanged |= ImGui::Checkbox("Disable Collision Queries", &LODPolicyOverride.bDisableCollisionQueries);
						if (!LODPolicyOverride.bOverrideDisablePolicy) ImGui::EndDisabled();
						bChanged |= ImGui::Checkbox("Override Collision Event Policy", &LODPolicyOverride.bOverrideCollisionEventPolicy);
						if (!LODPolicyOverride.bOverrideCollisionEventPolicy) ImGui::BeginDisabled();
						bChanged |= ImGui::Checkbox("Allow Collision Events", &LODPolicyOverride.bAllowCollisionEvents);
						if (!LODPolicyOverride.bOverrideCollisionEventPolicy) ImGui::EndDisabled();
						if (!LODPolicyOverride.bEnabled)
						{
							ImGui::EndDisabled();
							ImGui::TextDisabled("When disabled, runtime falls back to the default hardcoded LOD collision policy.");
						}
					}
				}
			}
			else if (UParticleModuleEventGenerator* EventGen = Cast<UParticleModuleEventGenerator>(Module))
			{
				if (ImGui::CollapsingHeader("Event Generator", ImGuiTreeNodeFlags_DefaultOpen))
				{
					static const char* EventNames[] = { "Spawn", "Death", "Collision", "Burst" };
					for (int32 i = 0; i < static_cast<int32>(EventGen->Entries.size()); ++i)
					{
						ImGui::PushID(i);
						ImGui::Separator();
						ImGui::Text("Entry %d", i);
						int32 Type = static_cast<int32>(EventGen->Entries[i].Type);
						if (ComboInt("Type", Type, EventNames, 4)) { EventGen->Entries[i].Type = static_cast<EParticleEventType>(Type); bChanged = true; }
						FString EventName = EventGen->Entries[i].EventName.ToString();
						if (InputTextFString("Event Name", EventName)) { EventGen->Entries[i].EventName = FName(EventName); bChanged = true; }
						bChanged |= ImGui::Checkbox("Enabled", &EventGen->Entries[i].bEnabled);
						if (ImGui::SmallButton("Remove Entry"))
						{
							EventGen->Entries.erase(EventGen->Entries.begin() + i);
							bChanged = true;
							ImGui::PopID();
							break;
						}
						ImGui::PopID();
					}
					if (ImGui::Button("Add Event Entry"))
					{
						FParticleEventGeneratorEntry Entry;
						Entry.Type = EParticleEventType::Death;
						Entry.EventName = FName("ParticleEvent");
						Entry.bEnabled = true;
						EventGen->Entries.push_back(Entry);
						bChanged = true;
					}
				}
			}
			else if (UParticleModuleEventReceiverSpawn* ReceiverSpawn = Cast<UParticleModuleEventReceiverSpawn>(Module))
			{
				if (ImGui::CollapsingHeader("Event Receiver Spawn", ImGuiTreeNodeFlags_DefaultOpen))
				{
					static const char* EventNames[] = { "Spawn", "Death", "Collision", "Burst" };
					bChanged |= ImGui::Checkbox("Use Event Type Filter", &ReceiverSpawn->bUseEventTypeFilter);
					int32 Type = static_cast<int32>(ReceiverSpawn->SourceEventType);
					if (ComboInt("Source Event Type", Type, EventNames, 4)) { ReceiverSpawn->SourceEventType = static_cast<EParticleEventType>(Type); bChanged = true; }
					bChanged |= ImGui::Checkbox("Use Event Name Filter", &ReceiverSpawn->bUseEventNameFilter);
					FString EventName = ReceiverSpawn->EventName.ToString();
					if (InputTextFString("Event Name", EventName)) { ReceiverSpawn->EventName = FName(EventName); bChanged = true; }
					bChanged |= ImGui::DragInt("Spawn Count", &ReceiverSpawn->SpawnCount, 1.0f, 0, 1024);
					bChanged |= ImGui::Checkbox("Use Event Location", &ReceiverSpawn->bUseEventLocation);
					bChanged |= ImGui::Checkbox("Inherit Event Velocity", &ReceiverSpawn->bInheritEventVelocity);
					bChanged |= ImGui::DragFloat("Inherit Velocity Scale", &ReceiverSpawn->InheritVelocityScale, 0.05f, -1000.0f, 1000.0f, "%.3f");
				}
			}
			else if (UParticleModuleEventReceiverKillAll* ReceiverKillAll = Cast<UParticleModuleEventReceiverKillAll>(Module))
			{
				if (ImGui::CollapsingHeader("Event Receiver Kill All", ImGuiTreeNodeFlags_DefaultOpen))
				{
					static const char* EventNames[] = { "Spawn", "Death", "Collision", "Burst" };
					bChanged |= ImGui::Checkbox("Use Event Type Filter", &ReceiverKillAll->bUseEventTypeFilter);
					int32 Type = static_cast<int32>(ReceiverKillAll->SourceEventType);
					if (ComboInt("Source Event Type", Type, EventNames, 4)) { ReceiverKillAll->SourceEventType = static_cast<EParticleEventType>(Type); bChanged = true; }
					bChanged |= ImGui::Checkbox("Use Event Name Filter", &ReceiverKillAll->bUseEventNameFilter);
					FString EventName = ReceiverKillAll->EventName.ToString();
					if (InputTextFString("Event Name", EventName)) { ReceiverKillAll->EventName = FName(EventName); bChanged = true; }
					bChanged |= ImGui::Checkbox("Stop Spawning", &ReceiverKillAll->bStopSpawning);
				}
			}
			else if (UParticleModuleTypeDataMesh* Mesh = Cast<UParticleModuleTypeDataMesh>(Module))
			{
				if (ImGui::CollapsingHeader("TypeData Mesh", ImGuiTreeNodeFlags_DefaultOpen))
				{
					if (MeshComboField("Static Mesh", Mesh->MeshSlot))
					{
						Mesh->CachedMesh = nullptr;
						bChanged = true;
					}
					static const char* AlignmentNames[] = { "None", "Velocity", "FacingCamera", "AxisLock" };
					int32 Alignment = static_cast<int32>(Mesh->Alignment);
					if (ComboInt("Alignment", Alignment, AlignmentNames, 4)) { Mesh->Alignment = static_cast<UParticleModuleTypeDataMesh::EMeshAlignment>(Alignment); bChanged = true; }
					bChanged |= ImGui::Checkbox("Override Material", &Mesh->bOverrideMaterial);
				}
			}
			else if (UParticleModuleBeamSource* BeamSource = Cast<UParticleModuleBeamSource>(Module))
			{
				if (ImGui::CollapsingHeader("Beam Source", ImGuiTreeNodeFlags_DefaultOpen))
				{
					static const char* SourceMethodNames[] = { "Default", "UserSet", "Emitter" };
					int32 Method = static_cast<int32>(BeamSource->SourceMethod);
					if (ComboInt("Source Method", Method, SourceMethodNames, 3)) { BeamSource->SourceMethod = static_cast<UParticleModuleBeamSource::EBeam2SourceMethod>(Method); bChanged = true; }
					bChanged |= DrawVectorDistributionEditor("Source", BeamSource->SourceDistribution, BeamSource, 0.1f, 0.0f, 0.0f, "EmitterTime");
					ImGui::Separator();
					bChanged |= DrawVectorDistributionEditor("Source Tangent", BeamSource->SourceTangentDistribution, BeamSource, 0.1f, 0.0f, 0.0f, "EmitterTime");
					bChanged |= ImGui::Checkbox("Source Absolute", &BeamSource->bSourceAbsolute);
					bChanged |= ImGui::Checkbox("Lock Source", &BeamSource->bLockSource);
				}
			}
			else if (UParticleModuleBeamTarget* BeamTarget = Cast<UParticleModuleBeamTarget>(Module))
			{
				if (ImGui::CollapsingHeader("Beam Target", ImGuiTreeNodeFlags_DefaultOpen))
				{
					static const char* TargetMethodNames[] = { "Default", "UserSet", "Emitter", "Distance" };
					int32 Method = static_cast<int32>(BeamTarget->TargetMethod);
					if (ComboInt("Target Method", Method, TargetMethodNames, 4)) { BeamTarget->TargetMethod = static_cast<UParticleModuleBeamTarget::EBeam2TargetMethod>(Method); bChanged = true; }
					bChanged |= DrawVectorDistributionEditor("Target", BeamTarget->TargetDistribution, BeamTarget, 0.1f, 0.0f, 0.0f, "EmitterTime");
					ImGui::Separator();
					bChanged |= DrawVectorDistributionEditor("Target Tangent", BeamTarget->TargetTangentDistribution, BeamTarget, 0.1f, 0.0f, 0.0f, "EmitterTime");
					bChanged |= ImGui::Checkbox("Target Absolute", &BeamTarget->bTargetAbsolute);
					bChanged |= ImGui::Checkbox("Lock Target", &BeamTarget->bLockTarget);
				}
			}
			else if (UParticleModuleBeamNoise* BeamNoise = Cast<UParticleModuleBeamNoise>(Module))
			{
				if (ImGui::CollapsingHeader("Beam Noise", ImGuiTreeNodeFlags_DefaultOpen))
				{
					bChanged |= DrawFloatDistributionEditor("Noise Range", BeamNoise->NoiseRangeDistribution, BeamNoise, 0.1f, 0.0f, 10000.0f, "EmitterTime");
					ImGui::Separator();
					bChanged |= DrawVectorDistributionEditor("Noise Direction", BeamNoise->NoiseDirectionDistribution, BeamNoise, 0.1f, 0.0f, 0.0f, "EmitterTime");
					ImGui::Separator();
					bChanged |= DrawFloatDistributionEditor("Frequency", BeamNoise->FrequencyDistribution, BeamNoise, 0.1f, 0.0f, 1000.0f, "EmitterTime");
					ImGui::Separator();
					bChanged |= DrawFloatDistributionEditor("Noise Speed", BeamNoise->NoiseSpeedDistribution, BeamNoise, 0.1f, -10000.0f, 10000.0f, "EmitterTime");
					bChanged |= ImGui::Checkbox("Smooth", &BeamNoise->bSmooth);
					bChanged |= ImGui::DragInt("Noise Tessellation", &BeamNoise->NoiseTessellation, 1.0f, 0, 128);
				}
			}
			else if (UParticleModuleTypeDataBeam* Beam = Cast<UParticleModuleTypeDataBeam>(Module))
			{
				if (ImGui::CollapsingHeader("TypeData Beam", ImGuiTreeNodeFlags_DefaultOpen))
				{
					static const char* BeamMethodNames[] = { "Distance", "Target" };
					int32 BeamMethod = static_cast<int32>(Beam->BeamMethod);
					if (ComboInt("Beam Method", BeamMethod, BeamMethodNames, 2)) { Beam->BeamMethod = static_cast<EBeam2Method>(BeamMethod); bChanged = true; }
					bChanged |= ImGui::DragFloat("Speed", &Beam->Speed, 0.1f, 0.0f, 100000.0f, "%.2f");
					bChanged |= ImGui::DragInt("Interpolation Points", &Beam->InterpolationPoints, 1.0f, 0, 128);
					EnsureFloatConstantDistribution(Beam->WidthDistribution, Beam, Beam->Width);
					EnsureFloatConstantDistribution(Beam->DistanceDistribution, Beam, Beam->Distance);
					bChanged |= DrawFloatDistributionEditor("Width", Beam->WidthDistribution, Beam, 0.01f, 0.0f, 10000.0f, "EmitterTime");
					ImGui::Separator();
					bChanged |= DrawFloatDistributionEditor("Distance", Beam->DistanceDistribution, Beam, 0.1f, 0.0f, 100000.0f, "EmitterTime");
					bChanged |= ImGui::Checkbox("Tile UV", &Beam->bTileUV);
					static const char* TaperNames[] = { "None", "Full" };
					int32 Taper = static_cast<int32>(Beam->TaperMethod);
					if (ComboInt("Taper Method", Taper, TaperNames, 2)) { Beam->TaperMethod = static_cast<EBeamTaperMethod>(Taper); bChanged = true; }
					bChanged |= ImGui::DragFloat("Taper Factor", &Beam->TaperFactor, 0.01f, 0.0f, 10.0f, "%.2f");
					bChanged |= ImGui::Checkbox("Render Geometry", &Beam->bRenderGeometry);
					bChanged |= DragFloat3Field("Default Source", Beam->DefaultSource, 0.1f);
					bChanged |= DragFloat3Field("Default Target", Beam->DefaultTarget, 0.1f);
				}
			}
			else if (UParticleModuleTypeDataRibbon* Ribbon = Cast<UParticleModuleTypeDataRibbon>(Module))
			{
				if (ImGui::CollapsingHeader("TypeData Ribbon", ImGuiTreeNodeFlags_DefaultOpen))
				{
					bChanged |= ImGui::DragInt("Max Tessellation", &Ribbon->MaxTessellation, 1.0f, 1, 64);
					bChanged |= ImGui::DragFloat("Tangent Tension", &Ribbon->TangentTension, 0.01f, 0.0f, 1.0f, "%.2f");
					bChanged |= ImGui::DragFloat("Tiles Per Trail", &Ribbon->TilesPerTrail, 0.01f, 0.0f, 9999.0f, "%.2f");
				}
			}
			else
			{
				ImGui::TextWrapped("This module has no dedicated editor yet. It is still selectable and can be enabled/disabled/removed if it is not a core module.");
				bResetPreview = false;
			}

			ImGui::Separator();
			const bool bCoreLocked = IsCoreModuleToken(SelectedModuleIndex);
			const bool bEmptySpriteTypeData = SelectedModuleIndex == ModuleTokenTypeData && !Module;
			if (bCoreLocked || bEmptySpriteTypeData) ImGui::BeginDisabled();
			if (ImGui::Button(SelectedModuleIndex == ModuleTokenTypeData ? "Clear TypeData" : "Delete Module"))
			{
				RemoveSelectedModule();
			}
			if (bCoreLocked || bEmptySpriteTypeData) ImGui::EndDisabled();
			if (bCoreLocked)
			{
				ImGui::TextDisabled("Required / Spawn modules are locked and cannot be deleted.");
			}

			bCurveKeyChanged |= RenderSelectedCurveKeyDetails();
		}
	}

	if (bChanged || bCurveKeyChanged)
	{
		const bool bCurveChangeNeedsReset =
			bCurveKeyChanged &&
			(Cast<UParticleModuleLifetime>(SelectedModuleForReset) ||
			 Cast<UParticleModuleLocation>(SelectedModuleForReset) ||
			 Cast<UParticleModuleVelocity>(SelectedModuleForReset) ||
			 Cast<UParticleModuleColor>(SelectedModuleForReset) ||
			 Cast<UParticleModuleSize>(SelectedModuleForReset));
		NotifyParticleAssetChanged((bChanged && bResetPreview) || bCurveChangeNeedsReset);
	}

	ImGui::EndChild();
}

bool FParticleEditorWidget::RenderSelectedCurveKeyDetails()
{
	FString CurveName;
	FString ChannelName;
	FFloatCurve* Curve = GetSelectedCurve(&CurveName, &ChannelName);
	if (!Curve)
	{
		return false;
	}

	bool bChanged = false;
	ImGui::Separator();
	if (!ImGui::CollapsingHeader("Curve Key", ImGuiTreeNodeFlags_DefaultOpen))
	{
		return false;
	}

	ImGui::Text("Curve: %s", CurveName.c_str());
	ImGui::Text("Channel: %s", ChannelName.c_str());

	if (ImGui::SmallButton("Add Key"))
	{
		float NewTime = 0.0f;
		float NewValue = Curve->DefaultValue;
		if (!Curve->Keys.empty())
		{
			NewTime = Curve->Keys.back().Time + 0.1f;
			NewValue = Curve->Keys.back().Value;
		}
		Curve->AddKey(NewTime, NewValue, ECurveInterpMode::Linear);
		SelectedCurveKeyIndex = SortCurveAndFindKey(*Curve, NewTime, NewValue);
		bChanged = true;
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Sort Keys"))
	{
		Curve->SortKeys();
		if (SelectedCurveKeyIndex >= static_cast<int32>(Curve->Keys.size()))
		{
			SelectedCurveKeyIndex = static_cast<int32>(Curve->Keys.size()) - 1;
		}
		bChanged = true;
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Auto Tangents"))
	{
		for (FCurveKey& Key : Curve->Keys)
		{
			Key.TangentMode = ECurveTangentMode::Auto;
		}
		Curve->AutoSetTangents();
		bChanged = true;
	}

	bChanged |= ImGui::DragFloat("Default Value", &Curve->DefaultValue, 0.01f, 0.0f, 0.0f, "%.3f");

	if (SelectedCurveKeyIndex < 0 || SelectedCurveKeyIndex >= static_cast<int32>(Curve->Keys.size()))
	{
		return bChanged;
	}

	FCurveKey& Key = Curve->Keys[SelectedCurveKeyIndex];
	ImGui::Text("Index: %d", SelectedCurveKeyIndex);

	bool bSortNeeded = false;
	bool bAutoTangentNeeded = false;

	if (ImGui::DragFloat("In Val", &Key.Time, 0.01f, 0.0f, 0.0f, "%.3f"))
	{
		bChanged = true;
		bSortNeeded = true;
		bAutoTangentNeeded = Key.TangentMode == ECurveTangentMode::Auto;
	}
	if (ImGui::DragFloat("Out Val", &Key.Value, 0.01f, 0.0f, 0.0f, "%.3f"))
	{
		bChanged = true;
		bAutoTangentNeeded = Key.TangentMode == ECurveTangentMode::Auto;
	}

	static const char* InterpNames[] = { "Constant", "Linear", "Cubic" };
	int32 Interp = static_cast<int32>(Key.InterpMode);
	if (ComboInt("Interp Mode", Interp, InterpNames, 3))
	{
		Key.InterpMode = static_cast<ECurveInterpMode>(Interp);
		bChanged = true;
		bAutoTangentNeeded = Key.TangentMode == ECurveTangentMode::Auto;
	}

	static const char* TangentNames[] = { "Auto", "User", "Break" };
	int32 TangentMode = static_cast<int32>(Key.TangentMode);
	if (ComboInt("Tangent Mode", TangentMode, TangentNames, 3))
	{
		Key.TangentMode = static_cast<ECurveTangentMode>(TangentMode);
		bChanged = true;
		bAutoTangentNeeded = Key.TangentMode == ECurveTangentMode::Auto;
	}

	if (Key.TangentMode == ECurveTangentMode::Auto)
	{
		ImGui::BeginDisabled();
	}
	if (ImGui::DragFloat("Arrive Tangent", &Key.ArriveTangent, 0.01f, 0.0f, 0.0f, "%.3f"))
	{
		bChanged = true;
	}
	if (ImGui::DragFloat("Leave Tangent", &Key.LeaveTangent, 0.01f, 0.0f, 0.0f, "%.3f"))
	{
		bChanged = true;
	}
	if (Key.TangentMode == ECurveTangentMode::Auto)
	{
		ImGui::EndDisabled();
	}

	if (ImGui::SmallButton("Delete Key"))
	{
		Curve->Keys.erase(Curve->Keys.begin() + SelectedCurveKeyIndex);
		if (SelectedCurveKeyIndex >= static_cast<int32>(Curve->Keys.size()))
		{
			SelectedCurveKeyIndex = static_cast<int32>(Curve->Keys.size()) - 1;
		}
		bChanged = true;
		return bChanged;
	}

	if (bSortNeeded)
	{
		const float NewTime = Key.Time;
		const float NewValue = Key.Value;
		SelectedCurveKeyIndex = SortCurveAndFindKey(*Curve, NewTime, NewValue);
	}
	else if (bAutoTangentNeeded)
	{
		Curve->AutoSetTangents();
	}

	return bChanged;
}


void FParticleEditorWidget::RenderCurveEditor(ImVec2 Size)
{
	ImGui::BeginChild("##ParticleCurveEditor", Size, true);
	ImGui::TextUnformatted("Curve Editor");
	ImGui::SameLine();
	ImGui::TextDisabled("Distribution Curve editor");
	ImGui::Separator();

	UParticleModule* Module = GetSelectedModule();
	if (!Module)
	{
		ImGui::TextDisabled("Select a module that owns a Curve distribution.");
		ImGui::EndChild();
		return;
	}

	bool bChanged = false;
	bool bHasCurve = false;

	auto DrawSpawnCurves = [&](UParticleModuleSpawn* Spawn) -> bool
	{
		auto* RateCurveDistribution = Cast<UDistributionFloatCurve>(Spawn ? Spawn->RateDistribution : nullptr);
		auto* RateScaleCurveDistribution = Cast<UDistributionFloatCurve>(Spawn ? Spawn->RateScaleDistribution : nullptr);
		FFloatCurve* Curves[] = {
			RateCurveDistribution ? &RateCurveDistribution->GetCurve() : nullptr,
			RateScaleCurveDistribution ? &RateScaleCurveDistribution->GetCurve() : nullptr
		};
		const char* Channels[] = { "Rate", "Rate Scale" };
		const ImU32 Colors[] = {
			IM_COL32(255, 210, 80, 255),
			IM_COL32(120, 190, 255, 255)
		};
		if (!Curves[0] && !Curves[1]) return false;
		ImGui::TextUnformatted("Spawn");
		const float GraphHeight = (std::max)(180.0f, ImGui::GetContentRegionAvail().y - 46.0f);
		bChanged |= DrawCurveGraph("SpawnCombinedGraph", Curves, Channels, Colors, 2, CurveSourceSpawnRate,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey, GraphHeight);
		return true;
	};

	auto DrawColorOverLifeCurves = [&](UParticleModuleColorOverLife* ColorOverLife) -> bool
	{
		auto* ColorCurveDistribution = Cast<UDistributionVectorCurve>(ColorOverLife ? ColorOverLife->ColorOverLifeDistribution : nullptr);
		auto* AlphaCurveDistribution = Cast<UDistributionFloatCurve>(ColorOverLife ? ColorOverLife->AlphaOverLifeDistribution : nullptr);
		FFloatCurve* Curves[] = {
			ColorCurveDistribution ? &ColorCurveDistribution->GetXCurve() : nullptr,
			ColorCurveDistribution ? &ColorCurveDistribution->GetYCurve() : nullptr,
			ColorCurveDistribution ? &ColorCurveDistribution->GetZCurve() : nullptr,
			AlphaCurveDistribution ? &AlphaCurveDistribution->GetCurve() : nullptr
		};
		const char* Channels[] = { "Color.R", "Color.G", "Color.B", "Alpha.A" };
		const ImU32 Colors[] = {
			IM_COL32(255, 70, 70, 255),
			IM_COL32(70, 230, 70, 255),
			IM_COL32(80, 130, 255, 255),
			IM_COL32(255, 230, 70, 255)
		};
		if (!Curves[0] && !Curves[1] && !Curves[2] && !Curves[3]) return false;
		ImGui::TextUnformatted("Color Over Life");
		const float GraphHeight = (std::max)(180.0f, ImGui::GetContentRegionAvail().y - 46.0f);
		bChanged |= DrawCurveGraph("ColorOverLifeCombinedGraph", Curves, Channels, Colors, 4, CurveSourceColorOverLifeRGB,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey, GraphHeight);
		return true;
	};

	auto DrawInitialColorCurves = [&](UParticleModuleColor* Color) -> bool
	{
		EnsureInitialColorDistributions(Color);
		auto* ColorCurveDistribution = Cast<UDistributionVectorCurve>(Color ? Color->StartColorDistribution : nullptr);
		auto* AlphaCurveDistribution = Cast<UDistributionFloatCurve>(Color ? Color->StartAlphaDistribution : nullptr);
		FFloatCurve* Curves[] = {
			ColorCurveDistribution ? &ColorCurveDistribution->GetXCurve() : nullptr,
			ColorCurveDistribution ? &ColorCurveDistribution->GetYCurve() : nullptr,
			ColorCurveDistribution ? &ColorCurveDistribution->GetZCurve() : nullptr,
			AlphaCurveDistribution ? &AlphaCurveDistribution->GetCurve() : nullptr
		};
		const char* Channels[] = { "Color.R", "Color.G", "Color.B", "Alpha.A" };
		const ImU32 Colors[] = {
			IM_COL32(255, 70, 70, 255),
			IM_COL32(70, 230, 70, 255),
			IM_COL32(80, 130, 255, 255),
			IM_COL32(255, 230, 70, 255)
		};
		if (!Curves[0] && !Curves[1] && !Curves[2] && !Curves[3]) return false;
		ImGui::TextUnformatted("Initial Color");
		const float GraphHeight = (std::max)(180.0f, ImGui::GetContentRegionAvail().y - 46.0f);
		bChanged |= DrawCurveGraph("InitialColorCombinedGraph", Curves, Channels, Colors, 4, CurveSourceInitialColorRGB,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey, GraphHeight);
		return true;
	};

	if (UParticleModuleSpawn* Spawn = Cast<UParticleModuleSpawn>(Module))
	{
		bHasCurve |= IsFloatCurveDistribution(Spawn->RateDistribution);
		bChanged |= DrawFloatCurveDistributionPanel("Spawn Rate", Spawn->RateDistribution, CurveSourceSpawnRate,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
		bHasCurve |= IsFloatCurveDistribution(Spawn->RateScaleDistribution);
		bChanged |= DrawFloatCurveDistributionPanel("Spawn Rate Scale", Spawn->RateScaleDistribution, CurveSourceSpawnRateScale,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
	}
	else if (UParticleModuleLifetime* Lifetime = Cast<UParticleModuleLifetime>(Module))
	{
		bHasCurve |= IsFloatCurveDistribution(Lifetime->LifetimeDistribution);
		bChanged |= DrawFloatCurveDistributionPanel("Lifetime", Lifetime->LifetimeDistribution, CurveSourceLifetime,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
	}
	else if (UParticleModuleLocation* Location = Cast<UParticleModuleLocation>(Module))
	{
		bHasCurve |= IsVectorCurveDistribution(Location->StartLocationDistribution);
		bChanged |= DrawVectorCurveDistributionPanel("Initial Location", Location->StartLocationDistribution, CurveSourceInitialLocation,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
	}
	else if (UParticleModuleVelocity* Velocity = Cast<UParticleModuleVelocity>(Module))
	{
		bHasCurve |= IsVectorCurveDistribution(Velocity->StartVelocityDistribution);
		bChanged |= DrawVectorCurveDistributionPanel("Initial Velocity", Velocity->StartVelocityDistribution, CurveSourceInitialVelocity,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
	}
	else if (UParticleModuleAcceleration* Acceleration = Cast<UParticleModuleAcceleration>(Module))
	{
		bHasCurve |= IsVectorCurveDistribution(Acceleration->AccelerationDistribution);
		bChanged |= DrawVectorCurveDistributionPanel("Acceleration", Acceleration->AccelerationDistribution, CurveSourceAcceleration,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
	}
	else if (UParticleModuleSize* SizeModule = Cast<UParticleModuleSize>(Module))
	{
		bHasCurve |= IsVectorCurveDistribution(SizeModule->StartSizeDistribution);
		bChanged |= DrawVectorCurveDistributionPanel("Initial Size", SizeModule->StartSizeDistribution, CurveSourceInitialSize,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
	}
	else if (UParticleModuleSizeByLife* SizeByLife = Cast<UParticleModuleSizeByLife>(Module))
	{
		bHasCurve |= IsVectorCurveDistribution(SizeByLife->LifeMultiplierDistribution);
		bChanged |= DrawVectorCurveDistributionPanel("Size By Life", SizeByLife->LifeMultiplierDistribution, CurveSourceSizeByLife,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
	}
	else if (UParticleModuleColor* Color = Cast<UParticleModuleColor>(Module))
	{
		EnsureInitialColorDistributions(Color);
		bHasCurve |= IsVectorCurveDistribution(Color->StartColorDistribution);
		bChanged |= DrawVectorCurveDistributionPanel("Initial Color", Color->StartColorDistribution, CurveSourceInitialColorRGB,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
		bHasCurve |= IsFloatCurveDistribution(Color->StartAlphaDistribution);
		bChanged |= DrawFloatCurveDistributionPanel("Initial Alpha", Color->StartAlphaDistribution, CurveSourceInitialColorAlpha,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
	}
	else if (UParticleModuleColorOverLife* ColorOverLife = Cast<UParticleModuleColorOverLife>(Module))
	{
		bHasCurve |= IsVectorCurveDistribution(ColorOverLife->ColorOverLifeDistribution);
		bChanged |= DrawVectorCurveDistributionPanel("Color Over Life", ColorOverLife->ColorOverLifeDistribution, CurveSourceColorOverLifeRGB,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
		bHasCurve |= IsFloatCurveDistribution(ColorOverLife->AlphaOverLifeDistribution);
		bChanged |= DrawFloatCurveDistributionPanel("Alpha Over Life", ColorOverLife->AlphaOverLifeDistribution, CurveSourceColorOverLifeAlpha,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
	}
	else if (UParticleModuleSubUV* SubUV = Cast<UParticleModuleSubUV>(Module))
	{
		bHasCurve |= IsFloatCurveDistribution(SubUV->SubImageIndexDistribution);
		bChanged |= DrawFloatCurveDistributionPanel("Sub Image Index", SubUV->SubImageIndexDistribution, CurveSourceSubImageIndex,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
	}
	else if (UParticleModuleSubUVMovie* SubUVMovie = Cast<UParticleModuleSubUVMovie>(Module))
	{
		bHasCurve |= IsFloatCurveDistribution(SubUVMovie->FrameRateDistribution);
		bChanged |= DrawFloatCurveDistributionPanel("SubUV Movie Frame Rate", SubUVMovie->FrameRateDistribution, CurveSourceSubUVMovieFrameRate,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
	}
	else if (UParticleModuleTypeDataBeam* Beam = Cast<UParticleModuleTypeDataBeam>(Module))
	{
		bHasCurve |= IsFloatCurveDistribution(Beam->WidthDistribution);
		bChanged |= DrawFloatCurveDistributionPanel("Beam Width", Beam->WidthDistribution, CurveSourceBeamWidth,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
		bHasCurve |= IsFloatCurveDistribution(Beam->DistanceDistribution);
		bChanged |= DrawFloatCurveDistributionPanel("Beam Distance", Beam->DistanceDistribution, CurveSourceBeamDistance,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
	}
	else if (UParticleModuleBeamSource* BeamSource = Cast<UParticleModuleBeamSource>(Module))
	{
		bHasCurve |= IsVectorCurveDistribution(BeamSource->SourceDistribution);
		bChanged |= DrawVectorCurveDistributionPanel("Beam Source", BeamSource->SourceDistribution, CurveSourceBeamSource,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
		bHasCurve |= IsVectorCurveDistribution(BeamSource->SourceTangentDistribution);
		bChanged |= DrawVectorCurveDistributionPanel("Beam Source Tangent", BeamSource->SourceTangentDistribution, CurveSourceBeamSourceTangent,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
	}
	else if (UParticleModuleBeamTarget* BeamTarget = Cast<UParticleModuleBeamTarget>(Module))
	{
		bHasCurve |= IsVectorCurveDistribution(BeamTarget->TargetDistribution);
		bChanged |= DrawVectorCurveDistributionPanel("Beam Target", BeamTarget->TargetDistribution, CurveSourceBeamTarget,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
		bHasCurve |= IsVectorCurveDistribution(BeamTarget->TargetTangentDistribution);
		bChanged |= DrawVectorCurveDistributionPanel("Beam Target Tangent", BeamTarget->TargetTangentDistribution, CurveSourceBeamTargetTangent,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
	}
	else if (UParticleModuleBeamNoise* BeamNoise = Cast<UParticleModuleBeamNoise>(Module))
	{
		bHasCurve |= IsFloatCurveDistribution(BeamNoise->NoiseRangeDistribution);
		bChanged |= DrawFloatCurveDistributionPanel("Beam Noise Range", BeamNoise->NoiseRangeDistribution, CurveSourceBeamNoiseRange,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
		bHasCurve |= IsFloatCurveDistribution(BeamNoise->FrequencyDistribution);
		bChanged |= DrawFloatCurveDistributionPanel("Beam Noise Frequency", BeamNoise->FrequencyDistribution, CurveSourceBeamNoiseFrequency,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
		bHasCurve |= IsFloatCurveDistribution(BeamNoise->NoiseSpeedDistribution);
		bChanged |= DrawFloatCurveDistributionPanel("Beam Noise Speed", BeamNoise->NoiseSpeedDistribution, CurveSourceBeamNoiseSpeed,
			SelectedCurveSource, SelectedCurveChannel, SelectedCurveKeyIndex, bDraggingCurveKey);
	}

	if (!bHasCurve)
	{
		ImGui::TextDisabled("Selected module has no Curve distribution yet.");
		ImGui::TextWrapped("In the Details panel, change Distribution Type to Distribution Float/Vector Constant Curve or Uniform Curve. Initial modules evaluate curves with SpawnTime. Over-Life modules evaluate curves with Particle RelativeTime.");
	}

	if (bChanged)
	{
		NotifyParticleAssetChanged(false);
	}

	ImGui::EndChild();
}


void FParticleEditorWidget::AddEmitter()
{
	UParticleSystem* System = GetEditedSystem();
	if (!System) return;

	UParticleEmitter* NewEmitter = System->AddEmitter();
	if (!NewEmitter) return;

	SelectedEmitterIndex = static_cast<int32>(System->Emitters.size()) - 1;
	SelectedModuleIndex = -1;
	NotifyParticleAssetChanged(true);
}

void FParticleEditorWidget::RemoveSelectedEmitter()
{
	UParticleSystem* System = GetEditedSystem();
	if (!System || SelectedEmitterIndex < 0) return;

	if (System->Emitters.size() <= 1) return;

	System->RemoveEmitter(SelectedEmitterIndex);
	if (SelectedEmitterIndex >= static_cast<int32>(System->Emitters.size()))
	{
		SelectedEmitterIndex = static_cast<int32>(System->Emitters.size()) - 1;
	}
	SelectedModuleIndex = -1;
	if (System->Emitters.empty())
	{
		SelectSystem();
	}
	NotifyParticleAssetChanged(true);
}

void FParticleEditorWidget::AddModuleToSelectedEmitter()
{
	ImGui::OpenPopup("Add Particle Module");
}

void FParticleEditorWidget::RenderAddModulePopup()
{
	UParticleEmitter* Emitter = GetSelectedEmitter();
	UParticleLODLevel* LOD = GetSelectedLOD();

	if (ImGui::BeginPopup("Add Particle Module"))
	{
		if (!Emitter || !LOD)
		{
			ImGui::TextDisabled("Select an emitter first.");
			ImGui::EndPopup();
			return;
		}

		auto AddRegular = [&](const char* Label, UParticleModule::EModuleCategory Category, auto Creator)
		{
			if (ImGui::MenuItem(Label))
			{
				UParticleModule* Module = Creator();
				if (Module && LOD->AddModule(Module))
				{
					SelectedModuleIndex = static_cast<int32>(LOD->Modules.size()) - 1;
					NotifyParticleAssetChanged(true);
				}
				else if (Module)
				{
					UObjectManager::Get().DestroyObject(Module);
				}
			}
		};

		AddRegular("Lifetime", UParticleModule::EModuleCategory::Lifetime, [&]() -> UParticleModule* { return CreateParticleModule<UParticleModuleLifetime>(LOD, Emitter); });
		AddRegular("Initial Location", UParticleModule::EModuleCategory::Location, [&]() -> UParticleModule* { return CreateParticleModule<UParticleModuleLocation>(LOD, Emitter); });
		AddRegular("Initial Velocity", UParticleModule::EModuleCategory::Velocity, [&]() -> UParticleModule* { return CreateParticleModule<UParticleModuleVelocity>(LOD, Emitter); });
		AddRegular("Local Vector Field", UParticleModule::EModuleCategory::Velocity, [&]() -> UParticleModule* { return CreateParticleModule<UParticleModuleVectorFieldLocal>(LOD, Emitter); });
		AddRegular("Vector Field Rotation", UParticleModule::EModuleCategory::Rotation, [&]() -> UParticleModule* { return CreateParticleModule<UParticleModuleVectorFieldRotation>(LOD, Emitter); });
		AddRegular("Const Acceleration", UParticleModule::EModuleCategory::Acceleration, [&]() -> UParticleModule* { return CreateParticleModule<UParticleModuleAcceleration>(LOD, Emitter); });
		AddRegular("Initial Color", UParticleModule::EModuleCategory::Color, [&]() -> UParticleModule* { return CreateParticleModule<UParticleModuleColor>(LOD, Emitter); });
		AddRegular("Color Over Life", UParticleModule::EModuleCategory::Color, [&]() -> UParticleModule* { return CreateParticleModule<UParticleModuleColorOverLife>(LOD, Emitter); });
        AddRegular(
            "Dynamic Parameter",
            UParticleModule::EModuleCategory::Color,
            [&]() -> UParticleModule*
            {
                return CreateParticleModule<UParticleModuleDynamicParameter>(LOD, Emitter);
            }
        );
		AddRegular("Initial Size", UParticleModule::EModuleCategory::Size, [&]() -> UParticleModule* { return CreateParticleModule<UParticleModuleSize>(LOD, Emitter); });
		AddRegular("Size By Life", UParticleModule::EModuleCategory::Size, [&]() -> UParticleModule* { return CreateParticleModule<UParticleModuleSizeByLife>(LOD, Emitter); });
		AddRegular("Collision", UParticleModule::EModuleCategory::Collision, [&]() -> UParticleModule* { return CreateParticleModule<UParticleModuleCollision>(LOD, Emitter); });
		AddRegular("Event Generator", UParticleModule::EModuleCategory::Event, [&]() -> UParticleModule* { return CreateParticleModule<UParticleModuleEventGenerator>(LOD, Emitter); });
		AddRegular("Event Receiver Spawn", UParticleModule::EModuleCategory::Event, [&]() -> UParticleModule* { return CreateParticleModule<UParticleModuleEventReceiverSpawn>(LOD, Emitter); });
		AddRegular("Event Receiver Kill All", UParticleModule::EModuleCategory::Event, [&]() -> UParticleModule* { return CreateParticleModule<UParticleModuleEventReceiverKillAll>(LOD, Emitter); });
		AddRegular("Sub Image Index", UParticleModule::EModuleCategory::SubUV, [&]() -> UParticleModule* { return CreateParticleModule<UParticleModuleSubUV>(LOD, Emitter); });
		AddRegular("SubUV Movie", UParticleModule::EModuleCategory::SubUV, [&]() -> UParticleModule* { return CreateParticleModule<UParticleModuleSubUVMovie>(LOD, Emitter); });
		AddRegular("Beam Source", UParticleModule::EModuleCategory::Beam, [&]() -> UParticleModule* { return CreateParticleModule<UParticleModuleBeamSource>(LOD, Emitter); });
		AddRegular("Beam Target", UParticleModule::EModuleCategory::Beam, [&]() -> UParticleModule* { return CreateParticleModule<UParticleModuleBeamTarget>(LOD, Emitter); });
		AddRegular("Beam Noise", UParticleModule::EModuleCategory::Beam, [&]() -> UParticleModule* { return CreateParticleModule<UParticleModuleBeamNoise>(LOD, Emitter); });

		ImGui::Separator();
		const bool bHasTypeData = LOD->TypeDataModule != nullptr;
		if (bHasTypeData) ImGui::BeginDisabled();
		if (ImGui::MenuItem("TypeData Mesh"))
		{
			UParticleModule* Module = CreateParticleModule<UParticleModuleTypeDataMesh>(LOD, Emitter);
			if (Module && LOD->AddModule(Module))
			{
				SelectedModuleIndex = ModuleTokenTypeData;
				NotifyParticleAssetChanged(true);
			}
			else if (Module)
			{
				UObjectManager::Get().DestroyObject(Module);
			}
		}
		if (ImGui::MenuItem("TypeData Beam"))
		{
			UParticleModule* Module = CreateParticleModule<UParticleModuleTypeDataBeam>(LOD, Emitter);
			if (Module && LOD->AddModule(Module))
			{
				SelectedModuleIndex = ModuleTokenTypeData;
				NotifyParticleAssetChanged(true);
			}
			else if (Module)
			{
				UObjectManager::Get().DestroyObject(Module);
			}
		}
		if (ImGui::MenuItem("TypeData Ribbon"))
		{
			UParticleModule* Module = CreateParticleModule<UParticleModuleTypeDataRibbon>(LOD, Emitter);
			if (Module && LOD->AddModule(Module))
			{
				SelectedModuleIndex = ModuleTokenTypeData;
				NotifyParticleAssetChanged(true);
			}
			else if (Module)
			{
				UObjectManager::Get().DestroyObject(Module);
			}
		}
		if (bHasTypeData) ImGui::EndDisabled();

		ImGui::EndPopup();
	}
}

void FParticleEditorWidget::DuplicateSelectedModule()
{
	UParticleLODLevel* LOD = GetSelectedLOD();
	UParticleModule* Module = GetSelectedModule();
	if (!LOD || !Module || SelectedModuleIndex < 0)
	{
		return;
	}

	UObject* DuplicateObject = Module->Duplicate(LOD);
	UParticleModule* DuplicateModule = Cast<UParticleModule>(DuplicateObject);
	if (!DuplicateModule)
	{
		return;
	}

	if (LOD->AddModule(DuplicateModule))
	{
		SelectedModuleIndex = static_cast<int32>(LOD->Modules.size()) - 1;
		NotifyParticleAssetChanged(true);
	}
	else
	{
		UObjectManager::Get().DestroyObject(DuplicateModule);
	}
}

void FParticleEditorWidget::RemoveSelectedModule()
{
	UParticleLODLevel* LOD = GetSelectedLOD();
	UParticleModule* Module = GetSelectedModule();
	if (!LOD)
	{
		return;
	}

	if (SelectedModuleIndex == ModuleTokenTypeData && LOD->TypeDataModule)
	{
		Module = LOD->TypeDataModule;
	}

	if (!Module || IsCoreModuleToken(SelectedModuleIndex))
	{
		return;
	}

	if (LOD->RemoveModule(Module))
	{
		ClearSelectedCurveKey();
		if (SelectedModuleIndex >= static_cast<int32>(LOD->Modules.size()))
		{
			SelectedModuleIndex = static_cast<int32>(LOD->Modules.size()) - 1;
		}
		if (SelectedModuleIndex < 0 && SelectedModuleIndex != ModuleTokenTypeData)
		{
			SelectedModuleIndex = -1;
		}
		NotifyParticleAssetChanged(true);
	}
}

void FParticleEditorWidget::SelectSystem()
{
	SelectedEmitterIndex = -1;
	SelectedModuleIndex = -1;
	ClearSelectedCurveKey();
}

void FParticleEditorWidget::SelectEmitter(int32 EmitterIndex)
{
	SelectedEmitterIndex = EmitterIndex;
	SelectedModuleIndex = -1;
	ClearSelectedCurveKey();
}

void FParticleEditorWidget::SelectModule(int32 EmitterIndex, int32 ModuleIndex)
{
	SelectedEmitterIndex = EmitterIndex;
	SelectedModuleIndex = ModuleIndex;
	ClearSelectedCurveKey();
}

void FParticleEditorWidget::SelectModuleCurve(int32 EmitterIndex, int32 ModuleIndex)
{
	SelectedEmitterIndex = EmitterIndex;
	SelectedModuleIndex = ModuleIndex;
	ClearSelectedCurveKey();
	SelectFirstCurveForModule(GetSelectedModule());
}

bool FParticleEditorWidget::SelectFirstCurveForModule(UParticleModule* Module)
{
	if (!Module)
	{
		return false;
	}

	auto TryFloatCurve = [&](UDistributionFloat* Distribution, int32 Source, int32 Channel) -> bool
	{
		if (!IsFloatCurveDistribution(Distribution)) return false;
		SelectedCurveSource = Source;
		SelectedCurveChannel = Channel;
		SelectedCurveKeyIndex = -1;
		return true;
	};

	auto TryVectorCurve = [&](UDistributionVector* Distribution, int32 Source) -> bool
	{
		if (!IsVectorCurveDistribution(Distribution)) return false;
		SelectedCurveSource = Source;
		SelectedCurveChannel = 0;
		SelectedCurveKeyIndex = -1;
		return true;
	};

	if (UParticleModuleSpawn* Spawn = Cast<UParticleModuleSpawn>(Module))
	{
		return TryFloatCurve(Spawn->RateDistribution, CurveSourceSpawnRate, 0) ||
			TryFloatCurve(Spawn->RateScaleDistribution, CurveSourceSpawnRateScale, 0);
	}
	if (UParticleModuleLifetime* Lifetime = Cast<UParticleModuleLifetime>(Module))
	{
		return TryFloatCurve(Lifetime->LifetimeDistribution, CurveSourceLifetime, 0);
	}
	if (UParticleModuleLocation* Location = Cast<UParticleModuleLocation>(Module))
	{
		return TryVectorCurve(Location->StartLocationDistribution, CurveSourceInitialLocation);
	}
	if (UParticleModuleVelocity* Velocity = Cast<UParticleModuleVelocity>(Module))
	{
		return TryVectorCurve(Velocity->StartVelocityDistribution, CurveSourceInitialVelocity);
	}
	if (UParticleModuleAcceleration* Acceleration = Cast<UParticleModuleAcceleration>(Module))
	{
		return TryVectorCurve(Acceleration->AccelerationDistribution, CurveSourceAcceleration);
	}
	if (UParticleModuleSize* SizeModule = Cast<UParticleModuleSize>(Module))
	{
		return TryVectorCurve(SizeModule->StartSizeDistribution, CurveSourceInitialSize);
	}
	if (UParticleModuleSizeByLife* SizeByLife = Cast<UParticleModuleSizeByLife>(Module))
	{
		return TryVectorCurve(SizeByLife->LifeMultiplierDistribution, CurveSourceSizeByLife);
	}
	if (UParticleModuleColor* Color = Cast<UParticleModuleColor>(Module))
	{
		EnsureInitialColorDistributions(Color);
		return TryVectorCurve(Color->StartColorDistribution, CurveSourceInitialColorRGB) ||
			TryFloatCurve(Color->StartAlphaDistribution, CurveSourceInitialColorAlpha, 0);
	}
	if (UParticleModuleColorOverLife* ColorOverLife = Cast<UParticleModuleColorOverLife>(Module))
	{
		return TryVectorCurve(ColorOverLife->ColorOverLifeDistribution, CurveSourceColorOverLifeRGB) ||
			TryFloatCurve(ColorOverLife->AlphaOverLifeDistribution, CurveSourceColorOverLifeAlpha, 0);
	}
	if (UParticleModuleSubUV* SubUV = Cast<UParticleModuleSubUV>(Module))
	{
		return TryFloatCurve(SubUV->SubImageIndexDistribution, CurveSourceSubImageIndex, 0);
	}
	if (UParticleModuleSubUVMovie* SubUVMovie = Cast<UParticleModuleSubUVMovie>(Module))
	{
		return TryFloatCurve(SubUVMovie->FrameRateDistribution, CurveSourceSubUVMovieFrameRate, 0);
	}
	if (UParticleModuleTypeDataBeam* Beam = Cast<UParticleModuleTypeDataBeam>(Module))
	{
		return TryFloatCurve(Beam->WidthDistribution, CurveSourceBeamWidth, 0) ||
			TryFloatCurve(Beam->DistanceDistribution, CurveSourceBeamDistance, 0);
	}
	if (UParticleModuleBeamSource* BeamSource = Cast<UParticleModuleBeamSource>(Module))
	{
		return TryVectorCurve(BeamSource->SourceDistribution, CurveSourceBeamSource) ||
			TryVectorCurve(BeamSource->SourceTangentDistribution, CurveSourceBeamSourceTangent);
	}
	if (UParticleModuleBeamTarget* BeamTarget = Cast<UParticleModuleBeamTarget>(Module))
	{
		return TryVectorCurve(BeamTarget->TargetDistribution, CurveSourceBeamTarget) ||
			TryVectorCurve(BeamTarget->TargetTangentDistribution, CurveSourceBeamTargetTangent);
	}
	if (UParticleModuleBeamNoise* BeamNoise = Cast<UParticleModuleBeamNoise>(Module))
	{
		return TryFloatCurve(BeamNoise->NoiseRangeDistribution, CurveSourceBeamNoiseRange, 0) ||
			TryFloatCurve(BeamNoise->FrequencyDistribution, CurveSourceBeamNoiseFrequency, 0) ||
			TryFloatCurve(BeamNoise->NoiseSpeedDistribution, CurveSourceBeamNoiseSpeed, 0);
	}

	return false;
}

void FParticleEditorWidget::RebuildPreview(bool bResetSimulation)
{
	UParticleSystem* System = GetEditedSystem();
	if (System)
	{
		System->BuildEmitters();
	}

	if (PreviewParticleComponent)
	{
		PreviewParticleComponent->SetTemplate(System);
		PreviewParticleComponent->SetCurrentLODIndex(CurrentLODIndex);
		PreviewParticleComponent->RebuildInstances(bResetSimulation);
		PreviewParticleComponent->Activate(bResetSimulation);
	}
}

void FParticleEditorWidget::RestartPreview()
{
	if (PreviewParticleComponent)
	{
		PreviewParticleComponent->SetCurrentLODIndex(CurrentLODIndex);
		PreviewParticleComponent->Activate(true);
		PreviewParticleComponent->ResetParticles();
	}
}

void FParticleEditorWidget::NotifyParticleAssetChanged(bool bResetSimulation)
{
	MarkDirty();
	RebuildPreview(bResetSimulation);
}

void FParticleEditorWidget::ApplyCurrentLODToPreview()
{
	if (UParticleSystem* System = GetEditedSystem())
	{
		System->EnsureLODDistances();
		const int32 MaxLODCount = System->GetMaxLODCount();
		if (MaxLODCount > 0)
		{
			CurrentLODIndex = std::clamp(CurrentLODIndex, 0, MaxLODCount - 1);
		}
		else
		{
			CurrentLODIndex = 0;
		}
	}
	else
	{
		if (CurrentLODIndex < 0) CurrentLODIndex = 0;
	}

	if (PreviewParticleComponent)
	{
		PreviewParticleComponent->SetCurrentLODIndex(CurrentLODIndex);
	}
}

UParticleEmitter* FParticleEditorWidget::GetSelectedEmitter() const
{
	UParticleSystem* System = GetEditedSystem();
	if (!System) return nullptr;
	return System->GetEmitter(SelectedEmitterIndex);
}

UParticleLODLevel* FParticleEditorWidget::GetSelectedLOD() const
{
	UParticleEmitter* Emitter = GetSelectedEmitter();
	if (!Emitter) return nullptr;
	return Emitter->GetCurrentLODLevel(CurrentLODIndex);
}

UParticleModule* FParticleEditorWidget::GetSelectedModule() const
{
	UParticleLODLevel* LOD = GetSelectedLOD();
	if (!LOD) return nullptr;

	switch (SelectedModuleIndex)
	{
	case ModuleTokenRequired: return LOD->RequiredModule;
	case ModuleTokenSpawn:    return LOD->SpawnModule;
	case ModuleTokenTypeData: return LOD->TypeDataModule;
	default:
		break;
	}

	if (SelectedModuleIndex >= 0 && SelectedModuleIndex < static_cast<int32>(LOD->Modules.size()))
	{
		return LOD->Modules[SelectedModuleIndex];
	}
	return nullptr;
}

void FParticleEditorWidget::ClearSelectedCurveKey()
{
	SelectedCurveSource = CurveSourceNone;
	SelectedCurveChannel = -1;
	SelectedCurveKeyIndex = -1;
	bDraggingCurveKey = false;
}

FFloatCurve* FParticleEditorWidget::GetSelectedCurve(FString* OutCurveName, FString* OutChannelName) const
{
	if (OutCurveName) *OutCurveName = "None";
	if (OutChannelName) *OutChannelName = "None";

	UParticleModule* Module = GetSelectedModule();
	if (!Module || SelectedCurveSource == CurveSourceNone || SelectedCurveChannel < 0)
	{
		return nullptr;
	}

	auto SelectVectorChannel = [&](UDistributionVector* Distribution, const FString& Name) -> FFloatCurve*
	{
		if (auto* UniformCurveDistribution = Cast<UDistributionVectorUniformCurve>(Distribution))
		{
			if (OutCurveName) *OutCurveName = Name;
			switch (SelectedCurveChannel)
			{
			case 0:
				if (OutChannelName) *OutChannelName = "Min.X";
				return &UniformCurveDistribution->GetMinXCurve();
			case 1:
				if (OutChannelName) *OutChannelName = "Min.Y";
				return &UniformCurveDistribution->GetMinYCurve();
			case 2:
				if (OutChannelName) *OutChannelName = "Min.Z";
				return &UniformCurveDistribution->GetMinZCurve();
			case 3:
				if (OutChannelName) *OutChannelName = "Max.X";
				return &UniformCurveDistribution->GetMaxXCurve();
			case 4:
				if (OutChannelName) *OutChannelName = "Max.Y";
				return &UniformCurveDistribution->GetMaxYCurve();
			case 5:
				if (OutChannelName) *OutChannelName = "Max.Z";
				return &UniformCurveDistribution->GetMaxZCurve();
			default:
				break;
			}
			return nullptr;
		}

		auto* CurveDistribution = Cast<UDistributionVectorCurve>(Distribution);
		if (!CurveDistribution) return nullptr;

		if (OutCurveName) *OutCurveName = Name;
		switch (SelectedCurveChannel)
		{
		case 0:
			if (OutChannelName) *OutChannelName = "X / R";
			return &CurveDistribution->GetXCurve();
		case 1:
			if (OutChannelName) *OutChannelName = "Y / G";
			return &CurveDistribution->GetYCurve();
		case 2:
			if (OutChannelName) *OutChannelName = "Z / B";
			return &CurveDistribution->GetZCurve();
		default:
			break;
		}
		return nullptr;
	};

	auto SelectFloatCurveAtChannel = [&](UDistributionFloat* Distribution, const FString& Name, const FString& Channel, int32 ExpectedChannel) -> FFloatCurve*
	{
		if (auto* UniformCurveDistribution = Cast<UDistributionFloatUniformCurve>(Distribution))
		{
			if (SelectedCurveChannel != ExpectedChannel && SelectedCurveChannel != ExpectedChannel + 1) return nullptr;
			if (OutCurveName) *OutCurveName = Name;
			if (SelectedCurveChannel == ExpectedChannel)
			{
				if (OutChannelName) *OutChannelName = "Min";
				return &UniformCurveDistribution->GetMinCurve();
			}
			if (OutChannelName) *OutChannelName = "Max";
			return &UniformCurveDistribution->GetMaxCurve();
		}

		if (SelectedCurveChannel != ExpectedChannel) return nullptr;
		auto* CurveDistribution = Cast<UDistributionFloatCurve>(Distribution);
		if (!CurveDistribution) return nullptr;
		if (OutCurveName) *OutCurveName = Name;
		if (OutChannelName) *OutChannelName = Channel;
		return &CurveDistribution->GetCurve();
	};

	auto SelectFloatCurve = [&](UDistributionFloat* Distribution, const FString& Name, const FString& Channel) -> FFloatCurve*
	{
		return SelectFloatCurveAtChannel(Distribution, Name, Channel, 0);
	};

	switch (SelectedCurveSource)
	{
	case CurveSourceSpawnRate:
		if (UParticleModuleSpawn* Spawn = Cast<UParticleModuleSpawn>(Module))
		{
			if (FFloatCurve* Curve = SelectFloatCurveAtChannel(Spawn->RateDistribution, "Spawn Rate", "Rate", 0)) return Curve;
			if (FFloatCurve* Curve = SelectFloatCurveAtChannel(Spawn->RateScaleDistribution, "Spawn Rate Scale", "Rate Scale", 1)) return Curve;
		}
		break;
	case CurveSourceSpawnRateScale:
		if (UParticleModuleSpawn* Spawn = Cast<UParticleModuleSpawn>(Module))
		{
			return SelectFloatCurve(Spawn->RateScaleDistribution, "Spawn Rate Scale", "Value");
		}
		break;
	case CurveSourceLifetime:
		if (UParticleModuleLifetime* Lifetime = Cast<UParticleModuleLifetime>(Module))
		{
			return SelectFloatCurve(Lifetime->LifetimeDistribution, "Lifetime", "Value");
		}
		break;
	case CurveSourceInitialLocation:
		if (UParticleModuleLocation* Location = Cast<UParticleModuleLocation>(Module))
		{
			return SelectVectorChannel(Location->StartLocationDistribution, "Initial Location");
		}
		break;
	case CurveSourceInitialVelocity:
		if (UParticleModuleVelocity* Velocity = Cast<UParticleModuleVelocity>(Module))
		{
			return SelectVectorChannel(Velocity->StartVelocityDistribution, "Initial Velocity");
		}
		break;
	case CurveSourceAcceleration:
		if (UParticleModuleAcceleration* Acceleration = Cast<UParticleModuleAcceleration>(Module))
		{
			return SelectVectorChannel(Acceleration->AccelerationDistribution, "Acceleration");
		}
		break;
	case CurveSourceInitialSize:
		if (UParticleModuleSize* Size = Cast<UParticleModuleSize>(Module))
		{
			return SelectVectorChannel(Size->StartSizeDistribution, "Initial Size");
		}
		break;
	case CurveSourceSizeByLife:
		if (UParticleModuleSizeByLife* SizeByLife = Cast<UParticleModuleSizeByLife>(Module))
		{
			return SelectVectorChannel(SizeByLife->LifeMultiplierDistribution, "Size By Life");
		}
		break;
	case CurveSourceInitialColorRGB:
		if (UParticleModuleColor* Color = Cast<UParticleModuleColor>(Module))
		{
			if (FFloatCurve* Curve = SelectVectorChannel(Color->StartColorDistribution, "Initial Color")) return Curve;
			if (FFloatCurve* Curve = SelectFloatCurveAtChannel(Color->StartAlphaDistribution, "Initial Alpha", "Alpha.A", 3)) return Curve;
		}
		break;
	case CurveSourceInitialColorAlpha:
		if (UParticleModuleColor* Color = Cast<UParticleModuleColor>(Module))
		{
			return SelectFloatCurve(Color->StartAlphaDistribution, "Initial Alpha", "Alpha");
		}
		break;
	case CurveSourceColorOverLifeRGB:
		if (UParticleModuleColorOverLife* ColorOverLife = Cast<UParticleModuleColorOverLife>(Module))
		{
			if (FFloatCurve* Curve = SelectVectorChannel(ColorOverLife->ColorOverLifeDistribution, "Color Over Life")) return Curve;
			if (FFloatCurve* Curve = SelectFloatCurveAtChannel(ColorOverLife->AlphaOverLifeDistribution, "Alpha Over Life", "Alpha.A", 3)) return Curve;
		}
		break;
	case CurveSourceColorOverLifeAlpha:
		if (UParticleModuleColorOverLife* ColorOverLife = Cast<UParticleModuleColorOverLife>(Module))
		{
			return SelectFloatCurve(ColorOverLife->AlphaOverLifeDistribution, "Color Over Life Alpha", "Alpha");
		}
		break;
	case CurveSourceSubImageIndex:
		if (UParticleModuleSubUV* SubUV = Cast<UParticleModuleSubUV>(Module))
		{
			return SelectFloatCurve(SubUV->SubImageIndexDistribution, "Sub Image Index", "Value");
		}
		break;
	case CurveSourceSubUVMovieFrameRate:
		if (UParticleModuleSubUVMovie* SubUVMovie = Cast<UParticleModuleSubUVMovie>(Module))
		{
			return SelectFloatCurve(SubUVMovie->FrameRateDistribution, "SubUV Movie Frame Rate", "FPS");
		}
		break;
	case CurveSourceBeamWidth:
		if (UParticleModuleTypeDataBeam* Beam = Cast<UParticleModuleTypeDataBeam>(Module))
		{
			return SelectFloatCurve(Beam->WidthDistribution, "Beam Width", "Width");
		}
		break;
	case CurveSourceBeamDistance:
		if (UParticleModuleTypeDataBeam* Beam = Cast<UParticleModuleTypeDataBeam>(Module))
		{
			return SelectFloatCurve(Beam->DistanceDistribution, "Beam Distance", "Distance");
		}
		break;
	case CurveSourceBeamSource:
		if (UParticleModuleBeamSource* BeamSource = Cast<UParticleModuleBeamSource>(Module))
		{
			return SelectVectorChannel(BeamSource->SourceDistribution, "Beam Source");
		}
		break;
	case CurveSourceBeamSourceTangent:
		if (UParticleModuleBeamSource* BeamSource = Cast<UParticleModuleBeamSource>(Module))
		{
			return SelectVectorChannel(BeamSource->SourceTangentDistribution, "Beam Source Tangent");
		}
		break;
	case CurveSourceBeamTarget:
		if (UParticleModuleBeamTarget* BeamTarget = Cast<UParticleModuleBeamTarget>(Module))
		{
			return SelectVectorChannel(BeamTarget->TargetDistribution, "Beam Target");
		}
		break;
	case CurveSourceBeamTargetTangent:
		if (UParticleModuleBeamTarget* BeamTarget = Cast<UParticleModuleBeamTarget>(Module))
		{
			return SelectVectorChannel(BeamTarget->TargetTangentDistribution, "Beam Target Tangent");
		}
		break;
	case CurveSourceBeamNoiseRange:
		if (UParticleModuleBeamNoise* BeamNoise = Cast<UParticleModuleBeamNoise>(Module))
		{
			return SelectFloatCurve(BeamNoise->NoiseRangeDistribution, "Beam Noise Range", "Range");
		}
		break;
	case CurveSourceBeamNoiseFrequency:
		if (UParticleModuleBeamNoise* BeamNoise = Cast<UParticleModuleBeamNoise>(Module))
		{
			return SelectFloatCurve(BeamNoise->FrequencyDistribution, "Beam Noise Frequency", "Frequency");
		}
		break;
	case CurveSourceBeamNoiseSpeed:
		if (UParticleModuleBeamNoise* BeamNoise = Cast<UParticleModuleBeamNoise>(Module))
		{
			return SelectFloatCurve(BeamNoise->NoiseSpeedDistribution, "Beam Noise Speed", "Speed");
		}
		break;
	default:
		break;
	}

	return nullptr;
}


UParticleSystem* FParticleEditorWidget::GetEditedSystem() const
{
	return Cast<UParticleSystem>(EditedObject);
}
