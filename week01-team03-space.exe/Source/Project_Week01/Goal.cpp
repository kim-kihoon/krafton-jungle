#include "Goal.h"
#pragma once

DirectX::XMMATRIX Goal::GetTransformMatrix()
{

	DirectX::XMMATRIX ret = DirectX::XMMatrixTranslation(Location.x, Location.y, 0);
	
	return 	DirectX::XMMatrixTranspose(ret);
	;
}
