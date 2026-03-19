#include "USphere.h"
#include <cmath>

int USphere::count = 1;

USphere::USphere()
{
    //enum ImageName
    //{
    //    ROCKET,
    //    BLACK_SPHERE,
    //    WHITE_SPHERE,
    //    JUPITER,
    //    SATURN,
    //    SUN,
    //    GOAL,
    //    COUNT
    //};
    imageName = static_cast<ImageName>(count);
    count++;
    if (count >= ImageName::GOAL) {
        count = ImageName::PLANET1;
    }
}

DirectX::XMMATRIX USphere::GetTransformMatrix()
{
	//DirectX::XMMATRIX ret = DirectX::XMMatrixScaling(0.1f, 0.1f, 0.1f);
	//return ret;

        // 1. 속도가 너무 작으면 이전 상태 유지 (정규화 에러 방지)
    //if (Velocity.Size() < 1.e-4f) return;

    // 2. atan2f를 이용해 각도(Radian) 계산
    // atan2f(y, x)는 x축 정방향(1, 0)을 0도로 기준 삼아 각도를 반환합니다.
    //float angle = atan2f(Velocity.y, Velocity.x);

    // 3. DirectXMath를 이용한 2D 행렬 조립
    // Z축을 중심으로 회전시키면 XY 평면상에서 회전합니다.
    DirectX::XMMATRIX scaleMat = DirectX::XMMatrixScaling(Scale, Scale, Scale);
    //DirectX::XMMATRIX rotationMat = DirectX::XMMatrixRotationZ(angle);
    DirectX::XMMATRIX translationMat = DirectX::XMMatrixTranslation(Location.x, Location.y, 0.0f);

    // 4. 최종 월드 행렬 (SRT: Scale * Rotation * Translation 순서)
    worldMatrix =  scaleMat * translationMat;

    return DirectX::XMMatrixTranspose(worldMatrix);
}

ImageName USphere::getImageName()
{
	return imageName;
}

void USphere::checkCount()
{
    count++;
    if (count >= ImageName::GOAL) {
        count = ImageName::PLANET1;
    }
}

