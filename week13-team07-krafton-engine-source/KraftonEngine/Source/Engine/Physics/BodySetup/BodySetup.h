#pragma once

#include "Physics/BodySetup/BodySetupCore.h"
#include "Physics/BodySetup/AggregateGeom.h"

#include "Source/Engine/Physics/BodySetup/BodySetup.generated.h"

class FArchive;
struct FStaticMesh;

// UE: UBodySetup : public UBodySetupCore
UCLASS()
class UBodySetup : public UBodySetupCore
{
public:
	GENERATED_BODY()

	FKAggregateGeom& GetAggGeom() { return AggGeom; }
	const FKAggregateGeom& GetAggGeom() const { return AggGeom; }

	bool HasSimpleCollision() const { return !AggGeom.IsEmpty(); }

	void SerializeCollision(FArchive& Ar);

	static void SerializeAggGeom(FArchive& Ar, FKAggregateGeom& AggGeom);
	static void AddBoxFromMeshBounds(UBodySetup* BodySetup, const FStaticMesh& MeshAsset);

private:
	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Aggregate Geometry", Type=Struct)
	FKAggregateGeom AggGeom;
};
