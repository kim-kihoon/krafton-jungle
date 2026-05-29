#include "MaterialEditorWidget.h"

#include "Component/Light/DirectionalLightComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Editor/UI/ContentBrowser/ContentItem.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Light/DirectionalLightActor.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Mesh/MeshManager.h"
#include "Mesh/StaticMesh.h"
#include "Object/ObjectFactory.h"
#include "Platform/Paths.h"
#include "Runtime/Engine.h"
#include "Settings/EditorSettings.h"
#include "Slate/SlateApplication.h"
#include "Texture/Texture2D.h"
#include "Render/Shader/Shader.h"
#include "UI/Toolbar/ViewportToolbar.h"
#include "Viewport/Viewport.h"

#include <algorithm>
#include <cstdio>
#include <imgui.h>

namespace
{
	static uint32 GNextMaterialEditorInstanceId = 0;

	const char* GBlendModeNames[] =
	{
		"Opaque",
		"AlphaBlend",
		"Additive"
	};

	bool IsColorTextureSlot(const FString& SlotName)
	{
		return SlotName == "DiffuseTexture"
			|| SlotName == "EmissiveTexture"
			|| SlotName == "Custom0Texture"
			|| SlotName == "Custom1Texture";
	}

	TArray<FMaterialTextureBindingInfo> GetEditableTextureBindings(UMaterial* Material)
	{
		TArray<FMaterialTextureBindingInfo> Bindings;
		if (!Material)
		{
			return Bindings;
		}

		if (FShader* Shader = Material->GetShader())
		{
			Bindings = Shader->GetTextureBindings();
		}

		if (!Bindings.empty())
		{
			return Bindings;
		}

		if (TMap<FString, UTexture2D*>* Textures = Material->GetTexture())
		{
			uint32 Index = 0;
			for (const auto& Pair : *Textures)
			{
				FMaterialTextureBindingInfo Info;
				Info.Name = Pair.first;
				Info.SlotIndex = Index++;
				Bindings.push_back(Info);
			}

			std::sort(Bindings.begin(), Bindings.end(),
				[](const FMaterialTextureBindingInfo& A, const FMaterialTextureBindingInfo& B)
				{
					return A.Name < B.Name;
				});
		}

		return Bindings;
	}

	bool AcceptPNGTextureDrop(const FString& SlotName, UMaterial* Material, UStaticMeshComponent* PreviewMeshComponent)
	{
		if (!Material || !ImGui::BeginDragDropTarget())
		{
			return false;
		}

		bool bChanged = false;
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("PNGElement"))
		{
			const FContentItem* Item = static_cast<const FContentItem*>(Payload->Data);
			if (Item)
			{
				const FString TexturePath = FPaths::MakeProjectRelative(
					FPaths::ToUtf8(Item->Path.generic_wstring()));
				ID3D11Device* Device = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDevice() : nullptr;
				UTexture2D* NewTexture = UTexture2D::LoadFromFile(
					TexturePath,
					Device,
					IsColorTextureSlot(SlotName) ? ETextureColorSpace::SRGB : ETextureColorSpace::Linear);

				if (NewTexture)
				{
					Material->SetTextureParameter(SlotName, NewTexture);
					Material->RebuildCachedSRVs();
					if (PreviewMeshComponent)
					{
						PreviewMeshComponent->SetMaterial(0, Material);
					}
					bChanged = true;
				}
			}
		}

		ImGui::EndDragDropTarget();
		return bChanged;
	}
}

FMaterialEditorWidget::FMaterialEditorWidget()
	: InstanceId(GNextMaterialEditorInstanceId++)
{
	const FString Id = std::to_string(InstanceId);
	PreviewWorldHandle = FName("MaterialEditorPreview_" + Id);
	WindowIdSuffix = "###MaterialEditor_" + Id;
}

bool FMaterialEditorWidget::CanEdit(UObject* Object) const
{
	return Object && Object->IsA<UMaterial>();
}

bool FMaterialEditorWidget::IsEditingObject(UObject* Object) const
{
	if (FAssetEditorWidget::IsEditingObject(Object))
	{
		return true;
	}

	const UMaterial* CurrentMaterial = Cast<UMaterial>(EditedObject);
	const UMaterial* RequestedMaterial = Cast<UMaterial>(Object);
	if (!IsOpen() || !CurrentMaterial || !RequestedMaterial)
	{
		return false;
	}

	const FString& CurrentPath = CurrentMaterial->GetAssetPathFileName();
	return !CurrentPath.empty()
		&& CurrentPath != "None"
		&& CurrentPath == RequestedMaterial->GetAssetPathFileName();
}

void FMaterialEditorWidget::Open(UObject* Object)
{
	FAssetEditorWidget::Open(Object);
	if (!IsOpen())
	{
		return;
	}

	UMaterial* Material = Cast<UMaterial>(EditedObject);
	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
	EditingMaterial = Material ? Material->CreateEditableCopy(Device) : nullptr;
	if (!EditingMaterial)
	{
		Close();
		return;
	}

	FWorldContext& WorldContext = GEngine->CreateWorldContext(EWorldType::EditorPreview, PreviewWorldHandle);
	WorldContext.World->SetWorldType(EWorldType::EditorPreview);
	WorldContext.World->InitWorld();

	AActor* Actor = WorldContext.World->SpawnActor<AActor>();
	PreviewMeshComponent = Actor->AddComponent<UStaticMeshComponent>();
	if (UStaticMesh* SphereMesh = FMeshManager::LoadStaticMesh("Asset/Mesh/BasicShape/Sphere_StaticMesh.uasset", Device))
	{
		PreviewMeshComponent->SetStaticMesh(SphereMesh);
	}
	PreviewMeshComponent->SetMaterial(0, EditingMaterial);
	Actor->SetRootComponent(PreviewMeshComponent);
	Actor->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));

	ADirectionalLightActor* LightActor = WorldContext.World->SpawnActor<ADirectionalLightActor>();
	LightActor->InitDefaultComponents();
	LightActor->SetActorRotation(FVector(0.0f, 45.0f, -45.0f));
	if (UDirectionalLightComponent* LightComp = LightActor->GetComponentByClass<UDirectionalLightComponent>())
	{
		LightComp->SetShadowBias(0.0f);
		LightComp->PushToScene();
	}

	ViewportClient.Initialize(Device, 640, 480);
	ViewportClient.SetPreviewWorld(WorldContext.World);
	ViewportClient.SetPreviewActor(Actor);
	ViewportClient.SetPreviewMeshComponent(PreviewMeshComponent);
	ViewportClient.ResetCameraToPreviewBounds();

	WorldContext.World->SetEditorPOVProvider(&ViewportClient);
	FSlateApplication::Get().RegisterViewport(&MaterialViewportWindow, &ViewportClient);
}

void FMaterialEditorWidget::Close()
{
	FAssetEditorWidget::Close();

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
	PreviewMeshComponent = nullptr;

	if (EditingMaterial)
	{
		GUObjectArray.DestroyObject(EditingMaterial);
		EditingMaterial = nullptr;
	}
}

void FMaterialEditorWidget::Tick(float DeltaTime)
{
	if (ViewportClient.IsRenderable())
	{
		ViewportClient.Tick(DeltaTime);
	}
}

void FMaterialEditorWidget::CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const
{
	if (IsOpen())
	{
		OutClients.push_back(const_cast<FStaticMeshEditorViewportClient*>(&ViewportClient));
	}
}

void FMaterialEditorWidget::Render(float DeltaTime)
{
	(void)DeltaTime;

	if (!IsOpen() || !EditedObject)
	{
		return;
	}

	UMaterial* OriginalMaterial = Cast<UMaterial>(EditedObject);
	UMaterial* Material = EditingMaterial;

	bool bWindowOpen = true;
	FString VisibleTitle = "Material Editor";
	const FString AssetPath = OriginalMaterial ? OriginalMaterial->GetAssetPathFileName() : FString();
	if (!AssetPath.empty())
	{
		VisibleTitle += " - ";
		VisibleTitle += AssetPath;
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

	ImGui::SetNextWindowSize(ImVec2(1040.0f, 640.0f), ImGuiCond_Once);
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

	if (ImGui::Button("Save"))
	{
		if (FMaterialManager::Get().SaveMaterial(Material))
		{
			if (!OriginalMaterial || OriginalMaterial->CopyEditableStateFrom(Material))
			{
				ClearDirty();
			}
		}
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%s", AssetPath.empty() ? "Unsaved material" : AssetPath.c_str());
	ImGui::Separator();

	static float DetailsWidth = 360.0f;
	RenderPreviewViewport(DetailsWidth);

	ImGui::SameLine();

	ImGui::BeginChild("MaterialDetails", ImVec2(DetailsWidth, 0), true);
	const bool bChanged = RenderDetailsPanel(Material);
	ImGui::EndChild();

	if (bChanged)
	{
		MarkDirty();
	}

	ImGui::End();

	if (!bWindowOpen)
	{
		Close();
	}
}

void FMaterialEditorWidget::RenderPreviewViewport(float DetailsWidth)
{
	ImGui::BeginGroup();
	{
		float AvailableWidth = ImGui::GetContentRegionAvail().x - DetailsWidth - ImGui::GetStyle().ItemSpacing.x;
		AvailableWidth = (std::max)(AvailableWidth, 220.0f);
		ImVec2 Size = ImVec2(AvailableWidth, ImGui::GetContentRegionAvail().y);

		ImVec2 ViewportPos = ImGui::GetCursorScreenPos();
		ViewportClient.SetViewportRect(ViewportPos.x, ViewportPos.y, Size.x, Size.y);

		FViewport* VP = ViewportClient.GetViewport();
		if (VP && Size.x > 0.0f && Size.y > 0.0f)
		{
			VP->RequestResize(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y));
			MaterialViewportWindow.SetRect(FRect(ViewportPos.x, ViewportPos.y, Size.x, Size.y));

			if (VP->GetSRV())
			{
				ImGui::Image((ImTextureID)VP->GetSRV(), Size);
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

bool FMaterialEditorWidget::RenderDetailsPanel(UMaterial* Material)
{
	if (!Material)
	{
		ImGui::TextDisabled("No material data.");
		return false;
	}

	bool bChanged = false;
	ImGui::TextUnformatted("Material Details");
	ImGui::Separator();

	if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
	{
		bChanged |= RenderRenderStateControls(Material);
	}

	if (ImGui::CollapsingHeader("Textures", ImGuiTreeNodeFlags_DefaultOpen))
	{
		bChanged |= RenderTextureSlots(Material);
	}

	if (ImGui::CollapsingHeader("Particle", ImGuiTreeNodeFlags_DefaultOpen))
	{
		bChanged |= RenderParticleSettings(Material);
	}

	if (ImGui::CollapsingHeader("Shader Parameters", ImGuiTreeNodeFlags_DefaultOpen))
	{
		bChanged |= RenderShaderParameters(Material);
	}

	return bChanged;
}

bool FMaterialEditorWidget::RenderRenderStateControls(UMaterial* Material)
{
	if (!Material)
	{
		return false;
	}

	int BlendMode = 0;
	switch (Material->GetBlendState())
	{
	case EBlendState::AlphaBlend:
		BlendMode = 1;
		break;
	case EBlendState::Additive:
		BlendMode = 2;
		break;
	case EBlendState::Opaque:
	default:
		BlendMode = 0;
		break;
	}

	if (!ImGui::Combo("Blend Mode", &BlendMode, GBlendModeNames, IM_ARRAYSIZE(GBlendModeNames)))
	{
		return false;
	}

	switch (BlendMode)
	{
	case 1:
		Material->SetRenderPass(ERenderPass::AlphaBlend);
		Material->SetBlendState(EBlendState::AlphaBlend);
		Material->SetDepthStencilState(EDepthStencilState::DepthReadOnly);
		break;
	case 2:
		Material->SetRenderPass(ERenderPass::AlphaBlend);
		Material->SetBlendState(EBlendState::Additive);
		Material->SetDepthStencilState(EDepthStencilState::DepthReadOnly);
		break;
	case 0:
	default:
		Material->SetRenderPass(ERenderPass::Opaque);
		Material->SetBlendState(EBlendState::Opaque);
		Material->SetDepthStencilState(EDepthStencilState::Default);
		break;
	}

	if (PreviewMeshComponent)
	{
		PreviewMeshComponent->SetMaterial(0, Material);
	}
	return true;
}

bool FMaterialEditorWidget::RenderParticleSettings(UMaterial* Material)
{
	if (!Material)
	{
		return false;
	}

	bool bChanged = false;
	FMaterialParticleSettings Settings = Material->GetParticleSettings();

	bool bUseSubUV = Settings.bUseSubUV;
	if (ImGui::Checkbox("Use SubUV Atlas", &bUseSubUV))
	{
		Settings.bUseSubUV = bUseSubUV;
		bChanged = true;
	}

	int Columns = static_cast<int>(Settings.SubUVColumns < 1u ? 1u : Settings.SubUVColumns);
	if (ImGui::DragInt("SubUV Columns", &Columns, 1.0f, 1, 64))
	{
		Settings.SubUVColumns = static_cast<uint32>(Columns < 1 ? 1 : Columns);
		bChanged = true;
	}

	int Rows = static_cast<int>(Settings.SubUVRows < 1u ? 1u : Settings.SubUVRows);
	if (ImGui::DragInt("SubUV Rows", &Rows, 1.0f, 1, 64))
	{
		Settings.SubUVRows = static_cast<uint32>(Rows < 1 ? 1 : Rows);
		bChanged = true;
	}

	if (bChanged)
	{
		Material->SetParticleSettings(Settings);
	}

	return bChanged;
}

bool FMaterialEditorWidget::RenderShaderParameters(UMaterial* Material)
{
	bool bChanged = false;
	const auto Layout = Material->GetParameterInfo();
	if (Layout.empty())
	{
		ImGui::TextDisabled("No reflected shader parameters.");
		return false;
	}

	for (const auto& Pair : Layout)
	{
		const FString& ParamName = Pair.first;
		const FMaterialParameterInfo* Info = Pair.second;
		if (!Info)
		{
			continue;
		}
		if (ParamName == "SubUVCols" || ParamName == "SubUVRows")
		{
			continue;
		}

		ImGui::PushID(ParamName.c_str());
		ImGui::TextUnformatted(ParamName.c_str());

		switch (Info->Size)
		{
		case sizeof(float):
		{
			float Value = 0.0f;
			if (Material->GetScalarParameter(ParamName, Value)
				&& ImGui::DragFloat("##Scalar", &Value, 0.01f))
			{
				Material->SetScalarParameter(ParamName, Value);
				bChanged = true;
			}
			break;
		}
		case sizeof(float) * 3:
		{
			FVector Value;
			if (Material->GetVector3Parameter(ParamName, Value)
				&& ImGui::DragFloat3("##Vector3", &Value.X, 0.01f))
			{
				Material->SetVector3Parameter(ParamName, Value);
				bChanged = true;
			}
			break;
		}
		case sizeof(float) * 4:
		{
			FVector4 Value;
			if (Material->GetVector4Parameter(ParamName, Value)
				&& ImGui::DragFloat4("##Vector4", &Value.X, 0.01f))
			{
				Material->SetVector4Parameter(ParamName, Value);
				bChanged = true;
			}
			break;
		}
		case sizeof(float) * 16:
		{
			FMatrix Value;
			if (Material->GetMatrixParameter(ParamName, Value))
			{
				bool bMatrixChanged = false;
				bMatrixChanged |= ImGui::DragFloat4("Row 0", &Value.Data[0], 0.01f);
				bMatrixChanged |= ImGui::DragFloat4("Row 1", &Value.Data[4], 0.01f);
				bMatrixChanged |= ImGui::DragFloat4("Row 2", &Value.Data[8], 0.01f);
				bMatrixChanged |= ImGui::DragFloat4("Row 3", &Value.Data[12], 0.01f);
				if (bMatrixChanged)
				{
					Material->SetMatrixParameter(ParamName, Value);
					bChanged = true;
				}
			}
			break;
		}
		default:
			ImGui::TextDisabled("Unsupported parameter size: %u", Info->Size);
			break;
		}

		ImGui::Spacing();
		ImGui::PopID();
	}

	return bChanged;
}

bool FMaterialEditorWidget::RenderTextureSlots(UMaterial* Material)
{
	bool bChanged = false;
	const ImVec2 ThumbnailSize(72.0f, 72.0f);

	const TArray<FMaterialTextureBindingInfo> Bindings = GetEditableTextureBindings(Material);
	if (Bindings.empty())
	{
		ImGui::TextDisabled("This shader exposes no material textures.");
		return false;
	}

	for (const FMaterialTextureBindingInfo& Binding : Bindings)
	{
		const FString& SlotName = Binding.Name;
		UTexture2D* Texture = nullptr;
		Material->GetTextureParameter(SlotName, Texture);

		ImGui::PushID(SlotName.c_str());
		ImGui::TextUnformatted(SlotName.c_str());

		if (Texture && Texture->GetSRV())
		{
			ImGui::Image((ImTextureID)Texture->GetSRV(), ThumbnailSize);
			bChanged |= AcceptPNGTextureDrop(SlotName, Material, PreviewMeshComponent);
		}
		else
		{
			ImGui::Button("Drop PNG", ThumbnailSize);
			bChanged |= AcceptPNGTextureDrop(SlotName, Material, PreviewMeshComponent);
		}

		ImGui::SameLine();
		ImGui::BeginGroup();
		ImGui::TextDisabled("t%u", Binding.SlotIndex);
		const FString TexturePath = Texture ? Texture->GetSourcePath() : FString("None");
		ImGui::TextWrapped("%s", TexturePath.c_str());
		ImGui::TextDisabled("PNG drag/drop target");
		ImGui::EndGroup();

		ImGui::Spacing();
		ImGui::PopID();
	}

	return bChanged;
}
