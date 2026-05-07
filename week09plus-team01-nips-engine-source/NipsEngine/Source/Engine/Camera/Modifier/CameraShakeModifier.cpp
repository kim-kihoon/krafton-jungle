#include "CameraShakeModifier.h"
#include "Engine/Camera/PlayerCameraManager.h"
#include "Engine/Math/Bezier.h"
#include <cmath>

DEFINE_CLASS(UCameraShakeModifier, UCameraModifier)

void UCameraShakeModifier::StartShake(const FCameraShakeParams& InParams)
{
    Params  = InParams;
    Elapsed = 0.0f;
    bShaking = true;
    EnableModifier();
}

void UCameraShakeModifier::StopShake()
{
    bShaking = false;
    DisableModifier();
}

bool UCameraShakeModifier::ModifyCamera(float DeltaTime, FCameraViewInfo& InOutView)
{
    if (!bShaking) return false;

    Elapsed += DeltaTime;

    float EnvelopeT;  // Bezier 감쇠 곡선용 (루프 시 0..1 반복)
    if (Params.bLoop)
    {
        EnvelopeT = std::fmod(Elapsed, Params.Duration) / Params.Duration;
    }
    else
    {
        if (Elapsed >= Params.Duration)
        {
            bShaking = false;
            DisableModifier();
            return false;
        }
        EnvelopeT = Elapsed / Params.Duration;
    }

    const float FadeOutTime = 0.2f;
    float SmoothAlpha = 1.0f;

    if (!Params.bLoop && (Params.Duration - Elapsed < FadeOutTime))
    {
        SmoothAlpha = (Params.Duration - Elapsed) / FadeOutTime;
    }

    // Rotation (Pitch/Yaw/Roll 독립 주파수)
    // Elapsed는 리셋하지 않아 sin/cos 위상이 연속으로 유지됨
    const float RotAlpha = Bezier::BezierValue(EnvelopeT, Params.RotBezierCP) * SmoothAlpha;
    InOutView.Rotation.X += std::sin(Elapsed * Params.RotFrequency[0]) * Params.RotAmplitude[0] * RotAlpha;
    InOutView.Rotation.Y += std::cos(Elapsed * Params.RotFrequency[1]) * Params.RotAmplitude[1] * RotAlpha;
    InOutView.Rotation.Z += std::sin(Elapsed * Params.RotFrequency[2]) * Params.RotAmplitude[2] * RotAlpha;

    // Location (X/Y/Z 독립 주파수)
    const float LocAlpha = Bezier::BezierValue(EnvelopeT, Params.LocBezierCP) * SmoothAlpha;
    InOutView.Location.X += std::sin(Elapsed * Params.LocFrequency[0]) * Params.LocAmplitude[0] * LocAlpha;
    InOutView.Location.Y += std::cos(Elapsed * Params.LocFrequency[1]) * Params.LocAmplitude[1] * LocAlpha;
    InOutView.Location.Z += std::sin(Elapsed * Params.LocFrequency[2]) * Params.LocAmplitude[2] * LocAlpha;

    // FOV
    if (Params.FOVAmplitude != 0.0f)
    {
        const float FovAlpha = Bezier::BezierValue(EnvelopeT, Params.FOVBezierCP) * SmoothAlpha;
        InOutView.FOV += std::sin(Elapsed * Params.FOVFrequency) * Params.FOVAmplitude * FovAlpha;
    }

    return true;
}
