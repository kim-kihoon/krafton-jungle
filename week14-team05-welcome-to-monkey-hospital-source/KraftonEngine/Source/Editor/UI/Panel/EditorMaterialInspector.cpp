#include "EditorMaterialInspector.h"
#include "Materials/MaterialManager.h"
#include "Resource/ResourceManager.h"
#include "Editor/UI/ContentBrowser/ContentItem.h"
#include "Editor/UI/Util/EditorTextureManager.h"
#include "Engine/Materials/Material.h"
#include "Engine/Materials/MaterialDomain.h"
#include "Engine/Runtime/Engine.h"
#include "Engine/Texture/Texture2D.h"

FEditorMaterialInspector::FEditorMaterialInspector(std::filesystem::path InPath)
{
	MaterialPath = InPath;
	CachedMaterial = FMaterialManager::Get().GetOrCreateMaterial(
		FPaths::ToUtf8(InPath.lexically_relative(FPaths::RootDir()).generic_wstring())
	);
	FMaterialManager::Get().ScanShaderPaths();  // 셰이더 드롭다운 목록
	FMaterialManager::Get().ScanTexturePaths(); // 텍스처 드롭다운 목록
}

void FEditorMaterialInspector::Render()
{
	bool bIsValid = ImGui::Begin("MaterialInspector");
	bIsValid &= std::filesystem::exists(MaterialPath);
	bIsValid &= (MaterialPath.extension() == ".uasset" || MaterialPath.extension() == ".mat");

	if (!bIsValid)
	{
		ImGui::End();
		return;
	}

	// 바이너리(.uasset) 머티리얼은 JSON 로드가 불가하므로 객체에서 직접 경로를 표시.
	// (파라미터/텍스처 편집은 아래에서 CachedMaterial 객체 기반으로 동작.)
	const FString PathLabel = CachedMaterial ? CachedMaterial->GetAssetPathFileName() : FString();
	ImGui::Selectable(PathLabel.c_str());

	RenderRenderStateSection();
	RenderShaderParameter();
	RenderTextureSection();

	if (CachedMaterial)
	{
		ImGui::Separator();
		if (ImGui::Button("Save"))
		{
			const FString SavePath = CachedMaterial->GetAssetPathFileName();
			if (!SavePath.empty())
			{
				FMaterialManager::Get().SaveMaterial(CachedMaterial, SavePath);
			}
		}
	}

	ImGui::End();
}

namespace
{
	struct FDomainOption { const char* Label; EMaterialDomain Value; };
	struct FBlendOption  { const char* Label; EBlendMode Value; };

	// enum 정렬 순서대로 — 인덱스 = enum 값.
	const FDomainOption GDomainOptions[] = {
		{ "Surface",     EMaterialDomain::Surface },
		{ "PostProcess", EMaterialDomain::PostProcess },
		{ "UI",          EMaterialDomain::UI },
		{ "Decal",       EMaterialDomain::Decal },
	};
	const FBlendOption GBlendOptions[] = {
		{ "Opaque",      EBlendMode::Opaque },
		{ "Masked",      EBlendMode::Masked },
		{ "Transparent", EBlendMode::Transparent },
		{ "Additive",    EBlendMode::Additive },
		{ "Modulate",    EBlendMode::Modulate },
	};
}

void FEditorMaterialInspector::RenderRenderStateSection()
{
	if (!CachedMaterial)
		return;

	// Domain 드롭다운
	const EMaterialDomain CurDomain = CachedMaterial->GetDomain();
	const char* DomainLabel = "";
	for (const FDomainOption& Opt : GDomainOptions)
		if (Opt.Value == CurDomain) { DomainLabel = Opt.Label; break; }

	if (ImGui::BeginCombo("Domain", DomainLabel))
	{
		for (const FDomainOption& Opt : GDomainOptions)
		{
			const bool bSelected = (Opt.Value == CurDomain);
			if (ImGui::Selectable(Opt.Label, bSelected))
				CachedMaterial->SetDomainBlend(Opt.Value, CachedMaterial->GetBlendMode());
			if (bSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	// BlendMode 드롭다운
	const EBlendMode CurBlend = CachedMaterial->GetBlendMode();
	const char* BlendLabel = "";
	for (const FBlendOption& Opt : GBlendOptions)
		if (Opt.Value == CurBlend) { BlendLabel = Opt.Label; break; }

	if (ImGui::BeginCombo("Blend Mode", BlendLabel))
	{
		for (const FBlendOption& Opt : GBlendOptions)
		{
			const bool bSelected = (Opt.Value == CurBlend);
			if (ImGui::Selectable(Opt.Label, bSelected))
				CachedMaterial->SetDomainBlend(CachedMaterial->GetDomain(), Opt.Value);
			if (bSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	// 양면 렌더 토글 (Raster NoCull override). 인스턴스도 렌더상태 override 가능하므로 활성.
	bool bTwoSided = CachedMaterial->IsTwoSided();
	if (ImGui::Checkbox("Two Sided", &bTwoSided))
		CachedMaterial->SetTwoSided(bTwoSided);

	// 셰이더 선택 (= 레이아웃 소스 & custom 강제 대상) — 변경 시 템플릿/CB 재구성.
	const bool bIsInstance = CachedMaterial->IsMaterialInstance();
	ImGui::BeginDisabled(bIsInstance);

	const FString& CurShaderPath = CachedMaterial->GetShaderPathForSerialize();
	const char* ShaderPreview = CurShaderPath.empty() ? "(none)" : CurShaderPath.c_str();
	if (ImGui::BeginCombo("Shader", ShaderPreview))
	{
		for (const FString& Path : FMaterialManager::Get().GetAvailableShaderPaths())
		{
			const bool bSelected = (Path == CurShaderPath);
			if (ImGui::Selectable(Path.c_str(), bSelected) && !bSelected)
				FMaterialManager::Get().SetMaterialShader(CachedMaterial, Path);
			if (bSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	// Custom shader 토글 — ON 이면 위 셰이더를 강제(퍼뮤테이션 도출 우회), OFF면 엔진이 도출.
	bool bUseCustom = CachedMaterial->WasCustomShaderRequested();
	if (ImGui::Checkbox("Use Custom Shader", &bUseCustom))
		CachedMaterial->SetUseCustomShader(bUseCustom);

	ImGui::EndDisabled();

	if (bIsInstance)
		ImGui::TextDisabled("(Material Instance: 셰이더는 부모에서 상속)");

	ImGui::Separator();
}

void FEditorMaterialInspector::RenderShaderParameter()
{
	const auto& Layout = CachedMaterial->GetParameterInfo();

	for (const auto& [ParamName, Info] : Layout)
	{
		ImGui::PushID(ParamName.c_str()); // 파라미터별 ID 스코프 — 동일 타입 다중 파라미터 ID 충돌 방지
		ImGui::TextUnformatted(ParamName.c_str());
		
		switch (Info->Size)
		{
			case sizeof(float) : // 4바이트 - Scalar
			{
				float Param = 0.0f;
				CachedMaterial->GetScalarParameter(ParamName, Param);
				if (ImGui::DragFloat("##scalar", &Param))
					CachedMaterial->SetScalarParameter(ParamName, Param);
				break;
			}
			case sizeof(float) * 3: // 12바이트 - Vector3
			{
				FVector Param;
				CachedMaterial->GetVector3Parameter(ParamName, Param);
				if (ImGui::DragFloat3("##vec3", &Param.X))
					CachedMaterial->SetVector3Parameter(ParamName, Param);
				break;
			}
			case sizeof(float) * 4: // 16바이트 - Vector4
			{
				FVector4 Param;
				CachedMaterial->GetVector4Parameter(ParamName, Param);
				if (ImGui::DragFloat4("##vec4", &Param.X))
					CachedMaterial->SetVector4Parameter(ParamName, Param);
				break;
			}
			case sizeof(float) * 16: // 64바이트 - Matrix
			{
				FMatrix Param;
				CachedMaterial->GetMatrixParameter(ParamName, Param);
				bool bChanged = false;
				bChanged |= ImGui::DragFloat4("##row0", Param.Data + 0);
				bChanged |= ImGui::DragFloat4("##row1", Param.Data + 4);
				bChanged |= ImGui::DragFloat4("##row2", Param.Data + 8);
				bChanged |= ImGui::DragFloat4("##row3", Param.Data + 12);
				if (bChanged)
					CachedMaterial->SetMatrixParameter(ParamName, Param);
				break;
			}
			default:
				break; // uint, bool 등 특수 케이스는 별도 처리 필요
		}

		ImGui::PopID();
	}

}

void FEditorMaterialInspector::RenderTextureSection()
{
	if (!CachedMaterial)
		return;

	// 셰이더 리플렉션된 텍스처 슬롯(t0~t7) 전체를 노출 — 빈 슬롯도 드롭 타겟으로 표시.
	const TArray<FShaderTextureBinding>& Bindings = CachedMaterial->GetTextureBindings();
	if (Bindings.empty())
		return;

	ImGui::Separator();
	ImGui::TextUnformatted("Textures");

	for (const FShaderTextureBinding& Binding : Bindings)
	{
		const FString& SlotName = Binding.Name;
		ImGui::PushID(SlotName.c_str()); // 슬롯별 ID 스코프

		UTexture2D* Texture = nullptr;
		CachedMaterial->GetTextureParameter(SlotName, Texture);

		ImGui::Text("%s (t%u)", SlotName.c_str(), Binding.BindPoint);

		if (Texture && Texture->GetSRV())
			ImGui::Image(Texture->GetSRV(), ImVec2(100, 100));
		else
			ImGui::Button("Drop\ntexture", ImVec2(100, 100)); // 빈 슬롯 placeholder = 드롭 타겟

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PNGElement"))
			{
				FContentItem ContentItem = *reinterpret_cast<const FContentItem*>(payload->Data);
				FString NewTexturePath = FPaths::ToUtf8(
					ContentItem.Path.lexically_relative(FPaths::RootDir()).generic_wstring()
				);
				ID3D11Device* Device = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDevice() : nullptr;
				const bool bIsColorTexture = MaterialTextureSlot::IsSRGBTextureSlot(SlotName);
				UTexture2D* NewTexture = UTexture2D::LoadFromFile(
					NewTexturePath,
					Device,
					bIsColorTexture ? ETextureColorSpace::SRGB : ETextureColorSpace::Linear);
				if (NewTexture)
				{
					CachedMaterial->SetTextureParameter(SlotName, NewTexture);
					CachedMaterial->RebuildCachedSRVs();
				}
			}
			ImGui::EndDragDropTarget();
		}

		// 텍스처 선택 드롭다운 (드래그&드롭 대안) — (None) 으로 슬롯 해제 가능.
		const FString CurTexPath = Texture ? Texture->GetSourcePath() : FString();
		const char* TexPreview = CurTexPath.empty() ? "(None)" : CurTexPath.c_str();
		ImGui::SetNextItemWidth(220.0f);
		if (ImGui::BeginCombo("##texsel", TexPreview))
		{
			if (ImGui::Selectable("(None)", CurTexPath.empty()))
			{
				CachedMaterial->SetTextureParameter(SlotName, nullptr);
				CachedMaterial->RebuildCachedSRVs();
			}
			for (const FString& TexPath : FMaterialManager::Get().GetAvailableTexturePaths())
			{
				const bool bSelected = (TexPath == CurTexPath);
				if (ImGui::Selectable(TexPath.c_str(), bSelected) && !bSelected)
				{
					ID3D11Device* Device = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDevice() : nullptr;
					const bool bIsColorTexture = MaterialTextureSlot::IsSRGBTextureSlot(SlotName);
					UTexture2D* NewTexture = UTexture2D::LoadFromFile(
						TexPath, Device,
						bIsColorTexture ? ETextureColorSpace::SRGB : ETextureColorSpace::Linear);
					if (NewTexture)
					{
						CachedMaterial->SetTextureParameter(SlotName, NewTexture);
						CachedMaterial->RebuildCachedSRVs();
					}
				}
				// hover 시 PNG 썸네일 미리보기 — 어떤 텍스처인지 시각 확인
				if (ImGui::IsItemHovered())
				{
					if (ID3D11ShaderResourceView* Thumb = FEditorTextureManager::Get().GetOrLoadThumbnail(TexPath))
					{
						ImGui::BeginTooltip();
						ImGui::Image(Thumb, ImVec2(96.0f, 96.0f));
						ImGui::TextUnformatted(TexPath.c_str());
						ImGui::EndTooltip();
					}
				}
				if (bSelected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		ImGui::PopID();
	}
}
