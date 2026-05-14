#pragma once
#include "Engine/Camera/CameraModifier.h"

// 독립 Amplitude + Frequency
// Bezier 감쇠는 Rotation / Location / FOV 그룹별 공유
struct FCameraShakeParams
{
    // Rotation: [0]=Pitch [1]=Yaw [2]=Roll
    float RotAmplitude[3] = { 0.3f, 0.0f, 0.0f };
    float RotFrequency[3] = { 15.0f, 15.0f, 15.0f };

    // Location: [0]=X [1]=Y [2]=Z
    float LocAmplitude[3] = { 0.0f, 0.0f, 0.0f };
    float LocFrequency[3] = { 15.0f, 15.0f, 15.0f };

    // FOV
    float FOVAmplitude = 0.0f;
    float FOVFrequency = 15.0f;

    float Duration = 0.5f;
    bool  bLoop    = false;

    // [0]=CP1x [1]=CP1y [2]=CP2x [3]=CP2y [4]=P0y(시작) [5]=P3y(끝)
    float RotBezierCP[6] = { 0.25f, 0.1f, 0.75f, 0.9f, 1.0f, 0.0f };
    float LocBezierCP[6] = { 0.25f, 0.1f, 0.75f, 0.9f, 1.0f, 0.0f };
    float FOVBezierCP[6] = { 0.25f, 0.1f, 0.75f, 0.9f, 1.0f, 0.0f };
};

class UCameraShakeModifier : public UCameraModifier
{
public:
    DECLARE_CLASS(UCameraShakeModifier, UCameraModifier)

    void StartShake(const FCameraShakeParams& InParams);
    void StopShake();

    bool ModifyCamera(float DeltaTime, FCameraViewInfo& InOutView) override;

    FCameraShakeParams Params;

    bool GetIsShaking() const { return bShaking; }

private:
    float Elapsed  = 0.0f;
    bool  bShaking = false;
};
