#pragma once
#include "PrimitiveComponent.h"

class USphereComp : public UPrimitiveComponent
{
	DECLARE_OBJECT(USphereComp, UPrimitiveComponent)
public:
	USphereComp();
	virtual ~USphereComp() override;
};
