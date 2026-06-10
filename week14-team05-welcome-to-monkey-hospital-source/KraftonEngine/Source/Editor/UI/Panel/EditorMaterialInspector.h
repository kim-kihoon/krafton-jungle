#pragma once
#include "imgui.h" 
#include "Platform/Paths.h"
#include "Core/Types/CoreTypes.h"
#include <fstream>
#include <filesystem>
#include <wrl/client.h>
#include <Engine/Materials/MaterialManager.h>

struct ID3D11ShaderResourceView;

class FEditorMaterialInspector final
{
public:
	FEditorMaterialInspector() = default;
	FEditorMaterialInspector(std::filesystem::path InPath);
	void Render();

private:
	void RenderRenderStateSection();
	void RenderShaderParameter();
	void RenderTextureSection();

private:
	std::filesystem::path MaterialPath;
	UMaterial* CachedMaterial = nullptr;

	TMap<FString, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> CachedSRVs;
};

