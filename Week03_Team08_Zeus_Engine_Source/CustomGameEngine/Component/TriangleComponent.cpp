#include "TriangleComponent.h"
#include "ResourceManager.h"

UTriangleComp::UTriangleComp()
{
	type = EPrimitiveType::Triangle;
	ResourceManager* RM = ResourceManager::GetInstance();
	VResource = &RM->GetGeometry("Triangle");

	LocalBoundingBox = FBoundingBox::FromPoints(VResource->Vertices);
	Name = FName("Triangle");
}

UTriangleComp::~UTriangleComp()
{
}

REGISTER_CLASS(UTriangleComp);
