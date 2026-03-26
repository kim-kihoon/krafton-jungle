#include "PrimitiveComponent.h"
#include "Renderer/Renderer.h"
#include "ResourceManager.h"
#include "World.h"

UPrimitiveComponent::UPrimitiveComponent()
{
}

UPrimitiveComponent::~UPrimitiveComponent()
{
	for (auto& renderObj : RenderObjs)
	{
		if (renderObj != nullptr)
			delete renderObj;
	}
}

EPrimitiveType UPrimitiveComponent::GetType()
{
	return type;
}

void UPrimitiveComponent::SetType(EPrimitiveType type)
{
	this->type = type;
}

void UPrimitiveComponent::SetRelativeLocation(const FVector& newLocation)
{
	USceneComponent::SetRelativeLocation(newLocation);
	MarkRenderStateDirty();
	GetWorld().bTextLabelDirty = true;
}

void UPrimitiveComponent::SetRelativeRotation(const FRotator& newRotation)
{
	USceneComponent::SetRelativeRotation(newRotation);
	MarkRenderStateDirty();
}

void UPrimitiveComponent::SetRelativeScale3D(const FVector& newScale)
{
	USceneComponent::SetRelativeScale3D(newScale);
	MarkRenderStateDirty();
	GetWorld().bTextLabelDirty = true;
}

void UPrimitiveComponent::SetRelativeQuaternion(FQuat newQuat)
{
	USceneComponent::SetRelativeQuaternion(newQuat);
	MarkRenderStateDirty();
}

FBoundingBox UPrimitiveComponent::ComputeWorldBoundingBox()
{
	if (type == EPrimitiveType::Sphere)
		return LocalBoundingBox.EllipsoidTransform(GetRelativeMatrix());
	return LocalBoundingBox.Transform(GetRelativeMatrix());
}

void UPrimitiveComponent::OnComponentAdded()
{
	CreateRenderObjects();
	
	for (auto& renderObj : RenderObjs)
		FLevel::GetInstance().RegisterRenderObject(renderObj);
}

void UPrimitiveComponent::Update(float DeltaTime)
{
	if (!this->bIsVisible) return;
	if (!IsSelected()) return;

	UpdateRenderObjects();

	TArray<uint32> Indices = {
		0,1, 1,3, 3,2, 2,0, // 바닥
		4,5, 5,7, 7,6, 6,4, // 천장
		0,4, 1,5, 2,6, 3,7  // 기둥
	};

	// TODO: Sphere는 예외적으로 처리
	if (type == EPrimitiveType::Sphere)
	{
		WorldBoundingBox = LocalBoundingBox.EllipsoidTransform(GetRelativeMatrix());
	}
	else
	{
		WorldBoundingBox = LocalBoundingBox.Transform(GetRelativeMatrix());
	}

	TArray<FVector> corners = WorldBoundingBox.GetCorners();

	GetWorld().DrawDebugLine(corners, Indices, FColor::Red(), false, EShowFlag::Debug);
}

void UPrimitiveComponent::CreateRenderObjects()
{
	if (VResource == nullptr)
		return;

	RenderObject* renderObj = new RenderObject();
	ResourceManager* RM = ResourceManager::GetInstance();
	renderObj->Geometry = this->VResource;
	renderObj->Material = &RM->GetMaterial(L"Asset/Shader/ShaderW0.hlsl");

	RenderObjs.push_back(renderObj);
	UpdateRenderObjects();
}

void UPrimitiveComponent::UpdateRenderObjects()
{
	for (auto& renderObj : RenderObjs)
	{
		if (renderObj == nullptr)
			continue;

		renderObj->bIsDirty = false;
		renderObj->World = GetRelativeMatrix();
		renderObj->bIsSelected = bIsSelected;
		renderObj->bIsVisible = bIsVisible;
	}
}

const TArray<RenderObject*>& UPrimitiveComponent::GetRenderObjects()
{
	return RenderObjs;
}

void UPrimitiveComponent::MarkRenderStateDirty()
{
	for (auto& renderObj : RenderObjs)
	{
		if (renderObj != nullptr)
			renderObj->bIsDirty = true;
	}
}

void UPrimitiveComponent::MarkRenderStateDirty(int index)
{
	if (index >= 0 && index < RenderObjs.size() && RenderObjs[index] != nullptr)
	{
		RenderObjs[index]->bIsDirty = true;
	}
}

void UPrimitiveComponent::ResolveRenderStateDirty()
{
	UpdateRenderObjects();
	for (auto& renderObj : RenderObjs)
	{
		if (renderObj != nullptr)
			renderObj->bIsDirty = false;
	}
}

static char TextBuffer[256];

json::JSON UPrimitiveComponent::Serialize()
{
	json::JSON jsonObj = USceneComponent::Serialize();
	switch (type)
	{
	case EPrimitiveType::Sphere:
		jsonObj["Type"] = "Sphere";
		break;
	case EPrimitiveType::Cube:
		jsonObj["Type"] = "Cube";
		break;
	case EPrimitiveType::Triangle:
		jsonObj["Type"] = "Triangle";
		break;
	case EPrimitiveType::Plane:
		jsonObj["Type"] = "Plane";
		break;
	case EPrimitiveType::Text:
		jsonObj["Type"] = "Text";
		break;
	case EPrimitiveType::ParticleSubUV:
		jsonObj["Type"] = "ParticleSubUV";
		break;
	}
	return jsonObj;
}

void UPrimitiveComponent::Deserialize(json::JSON jsonObj)
{
	USceneComponent::Deserialize(jsonObj);

	if (jsonObj["Type"].ToString() == "Sphere")
		type = EPrimitiveType::Sphere;
	else if (jsonObj["Type"].ToString() == "Cube")
		type = EPrimitiveType::Cube;
	else if (jsonObj["Type"].ToString() == "Triangle")
		type = EPrimitiveType::Triangle;
	else if (jsonObj["Type"].ToString() == "Plane")
		type = EPrimitiveType::Plane;
	else if (jsonObj["Type"].ToString() == "Text")
		type = EPrimitiveType::Text;
	else if (jsonObj["Type"].ToString() == "ParticleSubUV")
		type = EPrimitiveType::ParticleSubUV;
}

REGISTER_CLASS(UPrimitiveComponent);
