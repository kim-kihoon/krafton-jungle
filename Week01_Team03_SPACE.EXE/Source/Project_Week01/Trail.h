#pragma once
#include "UPrimitive.h"
class Trail : public UPrimitive
{
public:
	Trail();
	virtual ~Trail();
	virtual DirectX::XMMATRIX GetTransformMatrix();
};

