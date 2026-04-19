#include "SphereComponent.h"
#include "ResourceManager.h"
#include "Logger.h"

USphereComp::USphereComp()
{
	type = EPrimitiveType::Sphere;
	ResourceManager* RM = ResourceManager::GetInstance();
	VResource = &RM->GetGeometry("Sphere");

	LocalBoundingBox = FBoundingBox::FromPoints(VResource->Vertices);
	Name = FName("Sphere");
}

USphereComp::~USphereComp()
{
}

REGISTER_CLASS(USphereComp);
