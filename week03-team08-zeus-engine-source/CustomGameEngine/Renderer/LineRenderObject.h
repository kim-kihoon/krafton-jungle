#pragma once
#include "Renderer/RenderObject.h"
#include "VertexSimple.h"
#include "EngineTypes.h"

struct LineRenderObject : public RenderObject
{
public:
	virtual bool Render(ID3D11DeviceContext* Context) override;

	TArray<FVertexSimple> LineVertices;
};

