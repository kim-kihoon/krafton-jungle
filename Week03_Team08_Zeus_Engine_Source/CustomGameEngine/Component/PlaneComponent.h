#pragma once

#include "PrimitiveComponent.h"

class UPlaneComp : public UPrimitiveComponent {
	DECLARE_OBJECT(UPlaneComp, UPrimitiveComponent)
public:
	UPlaneComp();
	virtual ~UPlaneComp() override;
};