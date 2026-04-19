#pragma once

#include "EngineTypes.h"
#include "Math/Matrix.h"
#include "Math/Color.h"
#include "RenderState.h"
#include <d3d11.h>

struct FMaterial;
struct FGeometry;
struct FSubUV;

struct RenderObject
{
public:
	bool bIsDirty;
	virtual bool Render(ID3D11DeviceContext* Context);
	virtual ~RenderObject() = default;

public:
	FGeometry* Geometry;//Mesh
	FMaterial* Material;//Material
	FMatrix World;
	FColor Color = FColor(1.0f, 1.0f, 1.0f, 1.0f);

	EShowFlag ShowFlag = EShowFlag::StaticMesh;

	//Texture
	FSubUV* SubUV = nullptr;

	bool bIsSelected = false;
	bool bDepthEnabled = true;
	bool bIsVisible = true;
	bool bBackfaceCulling = true;

	D3D11_PRIMITIVE_TOPOLOGY PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};