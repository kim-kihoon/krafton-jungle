#pragma once

#include "UPrimitive.h"

struct ID3D11Buffer;

class USphere : public UPrimitive
{
private:
	ImageName imageName = ImageName::PLANET1;

public:
	USphere();

	virtual ~USphere() {};

	virtual DirectX::XMMATRIX GetTransformMatrix();
	void SetImageName(ImageName name) { imageName = name; }
	ImageName getImageName();
	static int count;
	static void checkCount();
};