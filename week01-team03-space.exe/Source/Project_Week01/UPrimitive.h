#pragma once
#include <vector>
#include <directxmath.h>
#include "datatype.h"



class UPrimitive 
{
protected:
    FVector Location;
    FVector Velocity;
    float Radius;
    float Scale;
    float Mass;
    bool bIsColliding;
    DirectX::XMMATRIX worldMatrix;

public:
    UPrimitive()
        : Location(1.0f, -1.0f, 0.0f), Velocity(0.13f, 0.13f, 0.0f), Radius(0.02f), Scale(0.05f), Mass(1.0f), bIsColliding(false)        
    {
    }

    virtual ~UPrimitive()   
    {
    }

    // 1. 매니저(물리 엔진)가 사용할 함수
	void SetScale(float InScale) { Scale = InScale; }
    float GetScale() const { return Scale; }

    void SetRadius(float InRadius) { Radius = InRadius; }
    float GetRadius() const { return Radius; }
    
    void SetLocation(const FVector& InLocation);
    FVector GetLocation() const;

    void SetVelocity(const FVector& InVelocity); 
    FVector GetVelocity() const;                 

    void SetMass(float InMass);
    float GetMass() const;
     
    void SetColliding(bool bInColliding);
    bool GetColliding() const;

    // 2. 변환 행렬 생성
    virtual DirectX::XMMATRIX GetTransformMatrix() = 0;

};