#include "World.h"
#include "LineBatchComponent.h"
#include "ResourceManager.h"
#include "Renderer/Renderer.h"
#include "Renderer/LineRenderObject.h"
#include <d3d11.h>

ULineBatchComponent::ULineBatchComponent()
{
	ResourceManager* RM = ResourceManager::GetInstance();
	VResource = &RM->GetGeometry("Line");

	Name = FName("LineBatchComponent");
}

ULineBatchComponent::~ULineBatchComponent()
{
	for (RenderObject* renderObj : RenderObjs)
	{
		delete renderObj;
	}
	RenderObjs.clear(); // 포인터를 지웠으니 벡터도 비워줍니다.
}

void ULineBatchComponent::Update(float DeltaTime)
{
	MarkRenderStateDirty();
}

void ULineBatchComponent::CreateRenderObjects()
{
	ResourceManager* RM = ResourceManager::GetInstance();

	RenderObject* renderObj = new LineRenderObject();
	renderObj->Geometry = VResource;
	renderObj->Material = &RM->GetMaterial(L"Asset/Shader/ShaderLine.hlsl");
	renderObj->PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
	renderObj->ShowFlag = EShowFlag::All;

	RenderObjs.push_back(renderObj);
}

void ULineBatchComponent::UpdateRenderObjects()
{
	LineRenderObject* LineRO = static_cast<LineRenderObject*>(RenderObjs[0]);
	LineRO->LineVertices.clear();

	uint32 CurrentShowFlags = GetWorld().GetRenderer()->GetShowFlags();

	for (FBatchedLine& Line : BatchedLines)
	{
		// 체크박스가 꺼져있으면 필터링
		if (!(CurrentShowFlags & (uint32)Line.Type)) continue;

		LineRO->LineVertices.push_back({ Line.Start.x, Line.Start.y, Line.Start.z, Line.Color.r, Line.Color.g, Line.Color.b, Line.Color.a  });
		LineRO->LineVertices.push_back({ Line.End.x, Line.End.y, Line.End.z, Line.Color.r, Line.Color.g, Line.Color.b, Line.Color.a });
	}

	BatchedLines.clear();
}

void ULineBatchComponent::DrawLine(const FVector& Start, const FVector& End, const FColor& Color, EShowFlag Type)
{
	BatchedLines.emplace_back(Start, End, Color, Type);
}



REGISTER_CLASS(ULineBatchComponent)