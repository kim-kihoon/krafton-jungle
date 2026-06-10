#include "Physics/BodySetup/BodySetup.h"

#include "Mesh/Static/StaticMeshAsset.h"
#include "Serialization/Archive.h"

#include <algorithm>

namespace
{
	FArchive& operator<<(FArchive& Ar, FKBoxElem& Elem)
	{
		Ar << Elem.Center << Elem.Rotation << Elem.X << Elem.Y << Elem.Z;
		return Ar;
	}

	FArchive& operator<<(FArchive& Ar, FKSphereElem& Elem)
	{
		Ar << Elem.Center << Elem.Radius;
		return Ar;
	}

	FArchive& operator<<(FArchive& Ar, FKSphylElem& Elem)
	{
		Ar << Elem.Center << Elem.Rotation << Elem.Radius << Elem.Length;
		return Ar;
	}
}

void UBodySetup::SerializeAggGeom(FArchive& Ar, FKAggregateGeom& AggGeom)
{
	Ar << AggGeom.SphereElems << AggGeom.BoxElems << AggGeom.SphylElems;
}

void UBodySetup::SerializeCollision(FArchive& Ar)  
{
	SerializeCore(Ar, *this);

	bool bHasCollision = HasSimpleCollision();
	Ar << bHasCollision;
	if (!bHasCollision)
	{
		return;
	}

	SerializeAggGeom(Ar, AggGeom);
}

void UBodySetup::AddBoxFromMeshBounds(UBodySetup* BodySetup, const FStaticMesh& MeshAsset)
{
	if (!BodySetup || BodySetup->HasSimpleCollision())
	{
		return;
	}

	FStaticMesh& MutableMesh = const_cast<FStaticMesh&>(MeshAsset);
	if (!MutableMesh.bBoundsValid)
	{
		MutableMesh.CacheBounds();
	}

	if (!MutableMesh.bBoundsValid)
	{
		return;
	}

	FKBoxElem Box;
	Box.Center = MutableMesh.BoundsCenter;
	Box.X = (std::max)(0.001f, MutableMesh.BoundsExtent.X * 2.0f);
	Box.Y = (std::max)(0.001f, MutableMesh.BoundsExtent.Y * 2.0f);
	Box.Z = (std::max)(0.001f, MutableMesh.BoundsExtent.Z * 2.0f);

	BodySetup->GetAggGeom().BoxElems.push_back(Box);
}
