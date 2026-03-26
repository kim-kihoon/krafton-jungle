#pragma once

#include "SceneComponent.h"
#include "Renderer/RenderObject.h"
#include "BoundingBox.h"
#include "EngineTypes.h"

class URenderer;
struct FGeometry;

enum class EPrimitiveType
{
	Sphere,
	Cube,
	Triangle,
	Plane,
	Text,
	ParticleSubUV,
};

class UPrimitiveComponent : public USceneComponent
{
	DECLARE_OBJECT(UPrimitiveComponent, USceneComponent)
public:
	UPrimitiveComponent();
	virtual ~UPrimitiveComponent() override;

	EPrimitiveType GetType();
	void SetType(EPrimitiveType);

	void SetRelativeLocation(const FVector& newLocation) override;
	void SetRelativeRotation(const FRotator& newRotation) override;
	void SetRelativeScale3D(const FVector& newScale) override;
	void SetRelativeQuaternion(FQuat newQuat) override;

	virtual void OnComponentAdded() override;
	virtual void Update(float DeltaTime);
	virtual void CreateRenderObjects();
	virtual void UpdateRenderObjects();
	virtual const TArray<RenderObject*>& GetRenderObjects();
	void SetMaterial(int Handle) { MaterialHandle = Handle; }
	void MarkRenderStateDirty();
	void MarkRenderStateDirty(int index);
	void ResolveRenderStateDirty();

	virtual json::JSON Serialize() override;
	virtual void Deserialize(json::JSON) override;

	FGeometry* GetGeometry() { return VResource; }

	FBoundingBox GetBoundingBox() { return WorldBoundingBox; }
	FBoundingBox ComputeWorldBoundingBox();

	bool Pickable = true;

protected:
	FGeometry* VResource;
	EPrimitiveType type;
	TArray<RenderObject*> RenderObjs;
	
	FBoundingBox WorldBoundingBox;
	FBoundingBox LocalBoundingBox;

private:
	int MaterialHandle = -1;
};
