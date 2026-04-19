#pragma once

#include "PrimitiveComponent.h"

class UCubeComp : public UPrimitiveComponent
{
	DECLARE_OBJECT(UCubeComp, UPrimitiveComponent)
public:
	UCubeComp();
	virtual ~UCubeComp() override;
};
