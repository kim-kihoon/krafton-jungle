#include "CubeComponent.h"
#include "ResourceManager.h"

UCubeComp::UCubeComp()
{
	type = EPrimitiveType::Cube;
	ResourceManager* RM = ResourceManager::GetInstance();
	VResource = &RM->GetGeometry("Cube");

	Name = FName("Cube");

	LocalBoundingBox = FBoundingBox::FromPoints(VResource->Vertices);
}

UCubeComp::~UCubeComp()
{
}

REGISTER_CLASS(UCubeComp);
