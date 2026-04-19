#pragma once

#include "Singleton.h"
#include "EngineTypes.h"
#include "RenderObject.h"

class FLevel : public ISingleton<FLevel>
{
	friend class ISingleton<FLevel>;

public:
	void RegisterRenderObject(RenderObject* RenderObj, class USceneComponent* Owner = nullptr);
	void UnregisterRenderObject(RenderObject* RenderObj);
	void Clear();
	TArray<RenderObject*> RenderObjects;
};

