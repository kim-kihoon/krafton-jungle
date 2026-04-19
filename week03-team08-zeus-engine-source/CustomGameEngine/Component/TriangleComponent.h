#pragma once
#include "PrimitiveComponent.h"

class UTriangleComp : public UPrimitiveComponent
{
	DECLARE_OBJECT(UTriangleComp, UPrimitiveComponent)
public:
	UTriangleComp();
	virtual ~UTriangleComp() override;
};

