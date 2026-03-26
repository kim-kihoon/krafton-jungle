#include "PlaneComponent.h"
#include "ResourceManager.h"

UPlaneComp::UPlaneComp()
{
	type = EPrimitiveType::Plane;
	ResourceManager* RM = ResourceManager::GetInstance();
	VResource = &RM->GetGeometry("Plane");

	LocalBoundingBox = FBoundingBox::FromPoints(VResource->Vertices);
	Name = FName("Plane");
}

UPlaneComp::~UPlaneComp()
{
}

REGISTER_CLASS(UPlaneComp);
