#include "PhysicsAssetEditorWidget.h"

#include "Asset/AssetRegistry.h"
#include "Animation/Skeleton/Skeleton.h"
#include "Component/Debug/PhysicsAssetPreviewComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Core/Types/RayTypes.h"
#include "Debug/DrawDebugHelpers.h"
#include "Editor/EditorEngine.h"
#include "GameFramework/World.h"
#include "Math/MathUtils.h"
#include "Mesh/MeshManager.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Object/Object.h"
#include "Physics/IPhysicsScene.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetAutoBodyGenerator.h"
#include "Physics/PhysicsAssetInstance.h"
#include "Physics/PhysicsAssetManager.h"
#include "Physics/PhysicsAssetPreviewUtils.h"
#include "Physics/PhysicsQueryTypes.h"
#include "Physics/PhysicsRuntime.h"
#include "Platform/Paths.h"
#include "Render/Types/ViewTypes.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>

#include <imgui.h>
#include "imgui_node_editor.h"
#include <SimpleJSON/json.hpp>

namespace ed = ax::NodeEditor;

namespace
{
    constexpr float LeftPanelWidth = 310.0f;
    constexpr float DetailsPanelMinWidth = 360.0f;
    constexpr int32 DebugCircleSegments = 24;
    constexpr int32 DebugHalfCircleSegments = 12;
    constexpr float PhysicsEditorTreeIndentSpacing = 10.0f;
	constexpr float ShapeSizeDragSpeed = 0.001f;
    constexpr float EditorRagdollGrabMaxDistance = 1000.0f;
    constexpr float EditorRagdollGrabFollowSpeed = 28.0f;
    constexpr float EditorRagdollGrabMaxLinearVelocity = 240.0f;
    constexpr float EditorRagdollGrabAngularDamping = 0.85f;

    FTransform ComposePreviewDebugTransforms(const FTransform& ParentWorld, const FTransform& Local)
    {
        FTransform Result = Local;
        Result.Location = ParentWorld.Location + ParentWorld.Rotation.RotateVector(Local.Location);
        Result.Rotation = (ParentWorld.Rotation * Local.Rotation).GetNormalized();
        Result.Scale = FVector::OneVector;
        return Result;
    }

    FColor ConstraintLimitSwingRed(uint32 Alpha)
    {
        return FColor(0xc5, 0x00, 0x00, Alpha);
    }

    FColor ConstraintLimitTwistGreen(uint32 Alpha)
    {
        return FColor(0x00, 0x80, 0x20, Alpha);
    }

    FColor SelectedConstraintSwingColor(uint32 Alpha)
    {
        return FColor(0xff, 0xcd, 0x37, Alpha);
    }

    FColor SelectedConstraintTwistColor(uint32 Alpha)
    {
        return FColor(0x50, 0xdc, 0xff, Alpha);
    }

    void DrawDebugCircle(
        UWorld* World,
        const FVector& Center,
        const FVector& AxisA,
        const FVector& AxisB,
        float Radius,
        const FColor& Color)
    {
        if (!World || Radius <= 0.0f)
        {
            return;
        }

        const float Step = 2.0f * FMath::Pi / static_cast<float>(DebugCircleSegments);
        FVector Prev = Center + AxisA * Radius;
        for (int32 i = 1; i <= DebugCircleSegments; ++i)
        {
            const float Angle = Step * static_cast<float>(i);
            const FVector Next = Center + (AxisA * cosf(Angle) + AxisB * sinf(Angle)) * Radius;
            DrawDebugLine(World, Prev, Next, Color, 0.0f);
            Prev = Next;
        }
    }

    void DrawDebugHalfCircle(
        UWorld* World,
        const FVector& Center,
        const FVector& AxisA,
        const FVector& AxisB,
        float Radius,
        int32 Segments,
        float StartAngle,
        const FColor& Color)
    {
        if (!World || Radius <= 0.0f || Segments < 3)
        {
            return;
        }

        const float Step = FMath::Pi / static_cast<float>(Segments);
        FVector Prev = Center + (AxisA * cosf(StartAngle) + AxisB * sinf(StartAngle)) * Radius;
        for (int32 i = 1; i <= Segments; ++i)
        {
            const float Angle = StartAngle + Step * static_cast<float>(i);
            const FVector Next = Center + (AxisA * cosf(Angle) + AxisB * sinf(Angle)) * Radius;
            DrawDebugLine(World, Prev, Next, Color, 0.0f);
            Prev = Next;
        }
    }

    void DrawDebugArc(
        UWorld* World,
        const FVector& Center,
        const FVector& AxisA,
        const FVector& AxisB,
        float Radius,
        float StartAngle,
        float EndAngle,
        int32 Segments,
        const FColor& Color)
    {
        if (!World || Radius <= 0.0f || Segments < 2)
        {
            return;
        }

        const float AngleRange = EndAngle - StartAngle;
        FVector Prev = Center + (AxisA * cosf(StartAngle) + AxisB * sinf(StartAngle)) * Radius;
        for (int32 i = 1; i <= Segments; ++i)
        {
            const float Alpha = static_cast<float>(i) / static_cast<float>(Segments);
            const float Angle = StartAngle + AngleRange * Alpha;
            const FVector Next = Center + (AxisA * cosf(Angle) + AxisB * sinf(Angle)) * Radius;
            DrawDebugLine(World, Prev, Next, Color, 0.0f);
            Prev = Next;
        }
    }

    void DrawDebugFrameAxes(
        UWorld* World,
        const FTransform& FrameWorld,
        float Length,
        uint32 Alpha)
    {
        if (!World || Length <= 0.0f)
        {
            return;
        }

        const FQuat Rotation = FrameWorld.Rotation.GetNormalized();
        const FVector Origin = FrameWorld.Location;
        DrawDebugLine(World, Origin, Origin + Rotation.RotateVector(FVector(1.0f, 0.0f, 0.0f)) * Length, FColor(255, 80, 80, Alpha), 0.0f);
        DrawDebugLine(World, Origin, Origin + Rotation.RotateVector(FVector(0.0f, 1.0f, 0.0f)) * Length, FColor(80, 255, 80, Alpha), 0.0f);
        DrawDebugLine(World, Origin, Origin + Rotation.RotateVector(FVector(0.0f, 0.0f, 1.0f)) * Length, FColor(80, 130, 255, Alpha), 0.0f);
    }

    float ClampLimitDegreesForDebug(float Degrees)
    {
        return FMath::Clamp(Degrees, 0.0f, 85.0f);
    }

    float MotionLimitDegreesForDebug(EConstraintMotion Motion, float LimitedDegrees)
    {
        switch (Motion)
        {
        case EConstraintMotion::Free:
            return 85.0f;
        case EConstraintMotion::Limited:
            return ClampLimitDegreesForDebug(LimitedDegrees);
        case EConstraintMotion::Locked:
        default:
            return 0.0f;
        }
    }

    int32 AngularArcSegments(float AngleRangeRadians)
    {
        const float Normalized = (std::max)(std::fabs(AngleRangeRadians) / (2.0f * FMath::Pi), 0.08f);
        return (std::max)(4, static_cast<int32>(ceilf(32.0f * Normalized)));
    }

	    float ComputeConstraintLimitDrawRadius(const FTransform& ParentFrameWorld, const FTransform& ChildFrameWorld)
	    {
	        const float FrameDistance = FVector::Distance(ParentFrameWorld.Location, ChildFrameWorld.Location);
	        return FMath::Clamp(FrameDistance * 0.25f + 0.15f, 0.15f, 1.2f);
	    }

    void DrawConstraintSwingLimitDebug(
        UWorld* World,
        const FTransform& ParentFrameWorld,
        const FConstraintLimitDesc& Limits,
        float Radius,
        const FColor& Color)
    {
        if (!World || Radius <= 0.0f)
        {
            return;
        }

        const FQuat Rotation = ParentFrameWorld.Rotation.GetNormalized();
        const FVector Center = ParentFrameWorld.Location;
        const FVector AxisX = Rotation.RotateVector(FVector(1.0f, 0.0f, 0.0f));
        const FVector AxisY = Rotation.RotateVector(FVector(0.0f, 1.0f, 0.0f));
        const FVector AxisZ = Rotation.RotateVector(FVector(0.0f, 0.0f, 1.0f));

        const float Swing1Degrees = MotionLimitDegreesForDebug(Limits.Swing1, Limits.Swing1LimitDegrees);
        const float Swing2Degrees = MotionLimitDegreesForDebug(Limits.Swing2, Limits.Swing2LimitDegrees);
        const float Swing1Radians = Swing1Degrees * FMath::DegToRad;
        const float Swing2Radians = Swing2Degrees * FMath::DegToRad;

        const FVector DiskCenter = Center + AxisX * Radius;
        const float DiskRadiusY = (std::max)(Radius * sinf(Swing1Radians), Radius * 0.025f);
        const float DiskRadiusZ = (std::max)(Radius * sinf(Swing2Radians), Radius * 0.025f);

        if (Limits.Swing1 == EConstraintMotion::Locked && Limits.Swing2 == EConstraintMotion::Locked)
        {
            const float CrossSize = Radius * 0.12f;
            DrawDebugLine(World, DiskCenter + AxisY * CrossSize, DiskCenter - AxisY * CrossSize, Color, 0.0f);
            DrawDebugLine(World, DiskCenter + AxisZ * CrossSize, DiskCenter - AxisZ * CrossSize, Color, 0.0f);
            return;
        }

        FVector Prev = DiskCenter + AxisY * DiskRadiusY;
        for (int32 i = 1; i <= DebugCircleSegments; ++i)
        {
            const float Angle = 2.0f * FMath::Pi * static_cast<float>(i) / static_cast<float>(DebugCircleSegments);
            const FVector Next = DiskCenter + AxisY * (cosf(Angle) * DiskRadiusY) + AxisZ * (sinf(Angle) * DiskRadiusZ);
            DrawDebugLine(World, Prev, Next, Color, 0.0f);
            Prev = Next;
        }

        DrawDebugLine(World, DiskCenter, DiskCenter + AxisY * DiskRadiusY, ConstraintLimitSwingRed(Color.A), 0.0f);
        DrawDebugLine(World, DiskCenter, DiskCenter + AxisZ * DiskRadiusZ, ConstraintLimitTwistGreen(Color.A), 0.0f);
    }

    void DrawConstraintTwistLimitDebug(
        UWorld* World,
        const FTransform& ParentFrameWorld,
        const FTransform& ChildFrameWorld,
        const FConstraintLimitDesc& Limits,
        float Radius,
        const FColor& Color)
    {
        if (!World || Radius <= 0.0f)
        {
            return;
        }

        const FQuat ParentRotation = ParentFrameWorld.Rotation.GetNormalized();
        const FQuat ChildRotation = ChildFrameWorld.Rotation.GetNormalized();
        const FVector Center = ParentFrameWorld.Location + ParentRotation.RotateVector(FVector(1.0f, 0.0f, 0.0f)) * (Radius * 0.12f);
        const FVector AxisY = ParentRotation.RotateVector(FVector(0.0f, 1.0f, 0.0f));
        const FVector AxisZ = ParentRotation.RotateVector(FVector(0.0f, 0.0f, 1.0f));
        const FVector ChildY = ChildRotation.RotateVector(FVector(0.0f, 1.0f, 0.0f));
        const float RingRadius = Radius * 0.32f;

        if (Limits.Twist == EConstraintMotion::Free)
        {
            DrawDebugCircle(World, Center, AxisY, AxisZ, RingRadius, ConstraintLimitTwistGreen(Color.A));
        }
        else if (Limits.Twist == EConstraintMotion::Locked)
        {
            DrawDebugLine(World, Center - AxisY * RingRadius, Center + AxisY * RingRadius, ConstraintLimitTwistGreen(Color.A), 0.0f);
            DrawDebugLine(World, Center - AxisZ * RingRadius, Center + AxisZ * RingRadius, ConstraintLimitTwistGreen(Color.A), 0.0f);
        }
        else
        {
            float MinAngle = Limits.TwistLimitMinDegrees * FMath::DegToRad;
            float MaxAngle = Limits.TwistLimitMaxDegrees * FMath::DegToRad;
            if (MinAngle > MaxAngle)
            {
                std::swap(MinAngle, MaxAngle);
            }
            DrawDebugArc(World, Center, AxisY, AxisZ, RingRadius, MinAngle, MaxAngle, AngularArcSegments(MaxAngle - MinAngle), ConstraintLimitTwistGreen(Color.A));
            DrawDebugLine(World, Center, Center + (AxisY * cosf(MinAngle) + AxisZ * sinf(MinAngle)) * RingRadius, ConstraintLimitTwistGreen(Color.A), 0.0f);
            DrawDebugLine(World, Center, Center + (AxisY * cosf(MaxAngle) + AxisZ * sinf(MaxAngle)) * RingRadius, ConstraintLimitTwistGreen(Color.A), 0.0f);
        }

        DrawDebugLine(World, Center, Center + ChildY * RingRadius, ConstraintLimitSwingRed(Color.A), 0.0f);
    }

	    void DrawConstraintAngularLimitDebug(
	        UWorld* World,
	        const FTransform& ParentFrameWorld,
	        const FTransform& ChildFrameWorld,
	        const FConstraintLimitDesc& Limits,
	        bool bSelected)
	    {
	        if (!World)
	        {
	            return;
	        }

	        const uint32 Alpha = bSelected ? 230u : 145u;
	        const float Radius = ComputeConstraintLimitDrawRadius(ParentFrameWorld, ChildFrameWorld);
	        DrawDebugFrameAxes(World, ParentFrameWorld, Radius * 0.32f, Alpha);
        DrawDebugFrameAxes(World, ChildFrameWorld, Radius * 0.22f, Alpha);
        DrawConstraintSwingLimitDebug(
            World,
            ParentFrameWorld,
            Limits,
            Radius,
            bSelected ? SelectedConstraintSwingColor(Alpha) : ConstraintLimitSwingRed(Alpha));
        DrawConstraintTwistLimitDebug(
            World,
            ParentFrameWorld,
            ChildFrameWorld,
            Limits,
            Radius,
            bSelected ? SelectedConstraintTwistColor(Alpha) : ConstraintLimitTwistGreen(Alpha));
    }

    void DrawDebugOrientedBox(
        UWorld* World,
        const FVector& Center,
        const FVector& HalfExtent,
        const FQuat& Rotation,
        const FColor& Color)
    {
        if (!World)
        {
            return;
        }

        const FVector ClampedHalfExtent(
            (std::max)(HalfExtent.X, 0.001f),
            (std::max)(HalfExtent.Y, 0.001f),
            (std::max)(HalfExtent.Z, 0.001f));

        FVector Corners[8];
        for (int32 i = 0; i < 8; ++i)
        {
            const FVector LocalCorner(
                (i & 1) ? ClampedHalfExtent.X : -ClampedHalfExtent.X,
                (i & 2) ? ClampedHalfExtent.Y : -ClampedHalfExtent.Y,
                (i & 4) ? ClampedHalfExtent.Z : -ClampedHalfExtent.Z);
            Corners[i] = Center + Rotation.RotateVector(LocalCorner);
        }

        DrawDebugBox(
            World,
            Corners[0],
            Corners[1],
            Corners[3],
            Corners[2],
            Corners[4],
            Corners[5],
            Corners[7],
            Corners[6],
            Color,
            0.0f);
    }

    void DrawDebugCapsuleZAxis(
        UWorld* World,
        const FVector& Center,
        float Radius,
        float HalfHeight,
        const FQuat& Rotation,
        const FColor& Color)
    {
        if (!World)
        {
            return;
        }

        Radius = (std::max)(Radius, 0.001f);
        HalfHeight = (std::max)(HalfHeight, Radius);
        const float CylinderHalf = (std::max)(0.0f, HalfHeight - Radius);

        const FVector AxisX = Rotation.RotateVector(FVector(1.0f, 0.0f, 0.0f));
        const FVector AxisY = Rotation.RotateVector(FVector(0.0f, 1.0f, 0.0f));
        const FVector AxisZ = Rotation.RotateVector(FVector(0.0f, 0.0f, 1.0f));

        const FVector TopCenter = Center + AxisZ * CylinderHalf;
        const FVector BottomCenter = Center - AxisZ * CylinderHalf;

        DrawDebugCircle(World, TopCenter, AxisX, AxisY, Radius, Color);
        DrawDebugCircle(World, BottomCenter, AxisX, AxisY, Radius, Color);

        DrawDebugLine(World, TopCenter + AxisX * Radius, BottomCenter + AxisX * Radius, Color, 0.0f);
        DrawDebugLine(World, TopCenter - AxisX * Radius, BottomCenter - AxisX * Radius, Color, 0.0f);
        DrawDebugLine(World, TopCenter + AxisY * Radius, BottomCenter + AxisY * Radius, Color, 0.0f);
        DrawDebugLine(World, TopCenter - AxisY * Radius, BottomCenter - AxisY * Radius, Color, 0.0f);

        DrawDebugHalfCircle(World, TopCenter, AxisX, AxisZ, Radius, DebugHalfCircleSegments, 0.0f, Color);
        DrawDebugHalfCircle(World, TopCenter, AxisY, AxisZ, Radius, DebugHalfCircleSegments, 0.0f, Color);
        DrawDebugHalfCircle(World, BottomCenter, AxisX, AxisZ, Radius, DebugHalfCircleSegments, FMath::Pi, Color);
        DrawDebugHalfCircle(World, BottomCenter, AxisY, AxisZ, Radius, DebugHalfCircleSegments, FMath::Pi, Color);
    }

    const char* ShapeTypeText(EPhysicsAssetShapeType Type)
    {
        switch (Type)
        {
        case EPhysicsAssetShapeType::Box: return "Box";
        case EPhysicsAssetShapeType::Sphere: return "Sphere";
        case EPhysicsAssetShapeType::Capsule: return "Capsule";
        default: return "Unknown";
        }
    }

    const char* MotionText(EConstraintMotion Motion)
    {
        switch (Motion)
        {
        case EConstraintMotion::Free: return "Free";
        case EConstraintMotion::Limited: return "Limited";
        case EConstraintMotion::Locked: return "Locked";
        default: return "Unknown";
        }
    }

    const char* StateText(UPhysicsAsset::EEditorSetupState State)
    {
        switch (State)
        {
        case UPhysicsAsset::EEditorSetupState::RuntimeReady: return "Ready";
        case UPhysicsAsset::EEditorSetupState::Placeholder: return "Placeholder";
        case UPhysicsAsset::EEditorSetupState::Invalid: return "Invalid";
        default: return "Unknown";
        }
    }

    bool InputString(const char* Label, FString& Value)
    {
        char Buffer[512] = {};
        std::snprintf(Buffer, sizeof(Buffer), "%s", Value.c_str());
        if (ImGui::InputText(Label, Buffer, sizeof(Buffer)))
        {
            Value = Buffer;
            return true;
        }
        return false;
    }

    bool InputName(const char* Label, FName& Name)
    {
        FString Text = (Name == FName::None) ? FString() : Name.ToString();
        if (InputString(Label, Text))
        {
            Name = Text.empty() ? FName::None : FName(Text);
            return true;
        }
        return false;
    }

    bool DragVec3(const char* Label, FVector& Value, float Speed = 0.1f)
    {
        float V[3] = { Value.X, Value.Y, Value.Z };
        if (ImGui::DragFloat3(Label, V, Speed))
        {
            Value = FVector(V[0], V[1], V[2]);
            return true;
        }
        return false;
    }

    bool DragMinFloat(const char* Label, float& Value, float Speed = 0.1f, float MinValue = 0.0f)
    {
        float Tmp = Value;
        if (ImGui::DragFloat(Label, &Tmp, Speed, MinValue, 0.0f))
        {
            Value = (std::max)(Tmp, MinValue);
            return true;
        }
        return false;
    }

    bool InputMinInt(const char* Label, int32& Value, int32 MinValue = 1)
    {
        int Tmp = static_cast<int>(Value);
        if (ImGui::InputInt(Label, &Tmp))
        {
            Value = static_cast<int32>((std::max)(Tmp, static_cast<int>(MinValue)));
            return true;
        }
        return false;
    }

    bool EditTransform(const char* Label, FTransform& Transform)
    {
        bool bChanged = false;
        if (ImGui::TreeNodeEx(Label, ImGuiTreeNodeFlags_DefaultOpen))
        {
            bChanged |= DragVec3("Location", Transform.Location, 0.1f);

            FRotator Rot = Transform.GetRotator();
            float R[3] = { Rot.Pitch, Rot.Yaw, Rot.Roll };
            if (ImGui::DragFloat3("Rotation", R, 0.1f))
            {
                Transform.SetRotation(FRotator(R[0], R[1], R[2]));
                bChanged = true;
            }

            bChanged |= DragVec3("Scale", Transform.Scale, 0.01f);
            ImGui::TreePop();
        }
        return bChanged;
    }

    bool ComboShapeType(const char* Label, EPhysicsAssetShapeType& Type)
    {
        bool bChanged = false;
        if (ImGui::BeginCombo(Label, ShapeTypeText(Type)))
        {
            const EPhysicsAssetShapeType Options[] = { EPhysicsAssetShapeType::Box, EPhysicsAssetShapeType::Sphere, EPhysicsAssetShapeType::Capsule };
            for (EPhysicsAssetShapeType Candidate : Options)
            {
                const bool bSelected = Type == Candidate;
                if (ImGui::Selectable(ShapeTypeText(Candidate), bSelected))
                {
                    Type = Candidate;
                    bChanged = true;
                }
                if (bSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return bChanged;
    }

    bool ComboMotion(const char* Label, EConstraintMotion& Motion)
    {
        bool bChanged = false;
        if (ImGui::BeginCombo(Label, MotionText(Motion)))
        {
            const EConstraintMotion Options[] = { EConstraintMotion::Free, EConstraintMotion::Limited, EConstraintMotion::Locked };
            for (EConstraintMotion Candidate : Options)
            {
                const bool bSelected = Motion == Candidate;
                if (ImGui::Selectable(MotionText(Candidate), bSelected))
                {
                    Motion = Candidate;
                    bChanged = true;
                }
                if (bSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return bChanged;
    }

    FString BodyLabel(const FPhysicsAssetBodySetup& Body, int32 Index)
    {
        const FString Bone = Body.BoneName == FName::None ? FString("<None>") : Body.BoneName.ToString();
        const int32 ShapeCount = static_cast<int32>(Body.Shapes.size());
        char Buffer[256] = {};
        std::snprintf(
            Buffer,
            sizeof(Buffer),
            "Body %d  %s  (%d %s)",
            Index,
            Bone.c_str(),
            ShapeCount,
            ShapeCount == 1 ? "shape" : "shapes");
        return Buffer;
    }

    FString ConstraintLabel(const FPhysicsAssetConstraintSetup& Constraint, int32 Index, UPhysicsAsset::EEditorSetupState State)
    {
        const FString Parent = Constraint.ParentBoneName == FName::None ? FString("<None>") : Constraint.ParentBoneName.ToString();
        const FString Child = Constraint.ChildBoneName == FName::None ? FString("<None>") : Constraint.ChildBoneName.ToString();
        char Buffer[384] = {};
        std::snprintf(Buffer, sizeof(Buffer), "Constraint %d  %s -> %s  [%s]", Index, Parent.c_str(), Child.c_str(), StateText(State));
        return Buffer;
    }

    bool HasBoneName(const FName& BoneName)
    {
        return BoneName.IsValid() && BoneName != FName::None;
    }

    bool IsBoneInReferenceSkeleton(const FReferenceSkeleton& RefSkeleton, const FName& BoneName)
    {
        return HasBoneName(BoneName) && RefSkeleton.FindBoneIndex(BoneName.ToString()) >= 0;
    }

    bool HasChildBone(const FReferenceSkeleton& RefSkeleton, int32 BoneIndex)
    {
        for (int32 Index = BoneIndex + 1; Index < RefSkeleton.GetNumBones(); ++Index)
        {
            if (RefSkeleton.Bones[Index].ParentIndex == BoneIndex)
            {
                return true;
            }
        }
        return false;
    }

    bool BoneSubtreeHasPhysicsBody(
        UPhysicsAsset* PhysicsAsset,
        const FReferenceSkeleton& RefSkeleton,
        int32 BoneIndex)
    {
        if (!PhysicsAsset || BoneIndex < 0 || BoneIndex >= RefSkeleton.GetNumBones())
        {
            return false;
        }

        if (PhysicsAsset->HasBodySetupForBone(FName(RefSkeleton.Bones[BoneIndex].Name)))
        {
            return true;
        }

        for (int32 ChildIndex = BoneIndex + 1; ChildIndex < RefSkeleton.GetNumBones(); ++ChildIndex)
        {
            if (RefSkeleton.Bones[ChildIndex].ParentIndex == BoneIndex &&
                BoneSubtreeHasPhysicsBody(PhysicsAsset, RefSkeleton, ChildIndex))
            {
                return true;
            }
        }
        return false;
    }

    bool HasVisiblePhysicsBoneChild(
        UPhysicsAsset* PhysicsAsset,
        const FReferenceSkeleton& RefSkeleton,
        int32 BoneIndex,
        bool bOnlyBonesWithBodies)
    {
        if (!bOnlyBonesWithBodies)
        {
            return HasChildBone(RefSkeleton, BoneIndex);
        }

        for (int32 ChildIndex = BoneIndex + 1; ChildIndex < RefSkeleton.GetNumBones(); ++ChildIndex)
        {
            if (RefSkeleton.Bones[ChildIndex].ParentIndex == BoneIndex &&
                BoneSubtreeHasPhysicsBody(PhysicsAsset, RefSkeleton, ChildIndex))
            {
                return true;
            }
        }
        return false;
    }

    int32 FindNearestParentBodyBoneIndex(
        UPhysicsAsset* PhysicsAsset,
        const FReferenceSkeleton& RefSkeleton,
        int32 BoneIndex)
    {
        if (!PhysicsAsset || BoneIndex < 0 || BoneIndex >= RefSkeleton.GetNumBones())
        {
            return -1;
        }

        int32 ParentIndex = RefSkeleton.Bones[BoneIndex].ParentIndex;
        while (ParentIndex >= 0 && ParentIndex < RefSkeleton.GetNumBones())
        {
            if (PhysicsAsset->HasBodySetupForBone(FName(RefSkeleton.Bones[ParentIndex].Name)))
            {
                return ParentIndex;
            }
            ParentIndex = RefSkeleton.Bones[ParentIndex].ParentIndex;
        }
        return -1;
    }

    int32 FindConstraintToNearestParentBodyIndex(
        UPhysicsAsset* PhysicsAsset,
        const FReferenceSkeleton& RefSkeleton,
        int32 BoneIndex)
    {
        if (!PhysicsAsset || BoneIndex < 0 || BoneIndex >= RefSkeleton.GetNumBones())
        {
            return -1;
        }

        const int32 ParentBodyIndex = FindNearestParentBodyBoneIndex(PhysicsAsset, RefSkeleton, BoneIndex);
        if (ParentBodyIndex < 0)
        {
            return -1;
        }

        return PhysicsAsset->FindConstraintSetupIndex(
            FName(RefSkeleton.Bones[ParentBodyIndex].Name),
            FName(RefSkeleton.Bones[BoneIndex].Name));
    }

    FString MakeBoneTreeLabel(
        UPhysicsAsset* PhysicsAsset,
        const FReferenceSkeleton& RefSkeleton,
        int32 BoneIndex)
    {
        (void)PhysicsAsset;
        return RefSkeleton.Bones[BoneIndex].Name;
    }

    constexpr uint32 PhysicsBodyNodeBase = 100000;
    constexpr uint32 PhysicsConstraintNodeBase = 200000;
    constexpr uint32 PhysicsBodyInputPinBase = 300000;
    constexpr uint32 PhysicsBodyOutputPinBase = 400000;
    constexpr uint32 PhysicsConstraintInputPinBase = 500000;
    constexpr uint32 PhysicsConstraintOutputPinBase = 600000;
    constexpr uint32 PhysicsParentLinkBase = 700000;
    constexpr uint32 PhysicsChildLinkBase = 800000;

    inline ed::NodeId ToPhysicsNodeId(uint32 Id) { return static_cast<ed::NodeId>(Id); }
    inline ed::PinId ToPhysicsPinId(uint32 Id) { return static_cast<ed::PinId>(Id); }
    inline ed::LinkId ToPhysicsLinkId(uint32 Id) { return static_cast<ed::LinkId>(Id); }
    inline uint32 PhysicsNodeIdToU32(ed::NodeId Id) { return static_cast<uint32>(Id.Get()); }
    inline uint32 PhysicsPinIdToU32(ed::PinId Id) { return static_cast<uint32>(Id.Get()); }
    inline uint32 PhysicsLinkIdToU32(ed::LinkId Id) { return static_cast<uint32>(Id.Get()); }

    uint32 MakeBodyNodeId(int32 BodyIndex) { return PhysicsBodyNodeBase + static_cast<uint32>(BodyIndex); }
    uint32 MakeConstraintNodeId(int32 ConstraintIndex) { return PhysicsConstraintNodeBase + static_cast<uint32>(ConstraintIndex); }
    uint32 MakeBodyInputPinId(int32 BodyIndex) { return PhysicsBodyInputPinBase + static_cast<uint32>(BodyIndex); }
    uint32 MakeBodyOutputPinId(int32 BodyIndex) { return PhysicsBodyOutputPinBase + static_cast<uint32>(BodyIndex); }
    uint32 MakeConstraintInputPinId(int32 ConstraintIndex) { return PhysicsConstraintInputPinBase + static_cast<uint32>(ConstraintIndex); }
    uint32 MakeConstraintOutputPinId(int32 ConstraintIndex) { return PhysicsConstraintOutputPinBase + static_cast<uint32>(ConstraintIndex); }
    uint32 MakeParentLinkId(int32 ConstraintIndex) { return PhysicsParentLinkBase + static_cast<uint32>(ConstraintIndex); }
    uint32 MakeChildLinkId(int32 ConstraintIndex) { return PhysicsChildLinkBase + static_cast<uint32>(ConstraintIndex); }

    bool DecodeBodyInputPin(uint32 PinId, int32& OutBodyIndex)
    {
        if (PinId < PhysicsBodyInputPinBase || PinId >= PhysicsBodyOutputPinBase)
        {
            return false;
        }
        OutBodyIndex = static_cast<int32>(PinId - PhysicsBodyInputPinBase);
        return true;
    }

    bool DecodeBodyOutputPin(uint32 PinId, int32& OutBodyIndex)
    {
        if (PinId < PhysicsBodyOutputPinBase || PinId >= PhysicsConstraintInputPinBase)
        {
            return false;
        }
        OutBodyIndex = static_cast<int32>(PinId - PhysicsBodyOutputPinBase);
        return true;
    }

    bool DecodeBodyNode(uint32 NodeId, int32& OutBodyIndex)
    {
        if (NodeId < PhysicsBodyNodeBase || NodeId >= PhysicsConstraintNodeBase)
        {
            return false;
        }
        OutBodyIndex = static_cast<int32>(NodeId - PhysicsBodyNodeBase);
        return true;
    }

    bool DecodeConstraintNode(uint32 NodeId, int32& OutConstraintIndex)
    {
        if (NodeId < PhysicsConstraintNodeBase || NodeId >= PhysicsBodyInputPinBase)
        {
            return false;
        }
        OutConstraintIndex = static_cast<int32>(NodeId - PhysicsConstraintNodeBase);
        return true;
    }

    bool DecodeConstraintLink(uint32 LinkId, int32& OutConstraintIndex)
    {
        if (LinkId >= PhysicsParentLinkBase && LinkId < PhysicsChildLinkBase)
        {
            OutConstraintIndex = static_cast<int32>(LinkId - PhysicsParentLinkBase);
            return true;
        }
        if (LinkId >= PhysicsChildLinkBase)
        {
            OutConstraintIndex = static_cast<int32>(LinkId - PhysicsChildLinkBase);
            return true;
        }
        return false;
    }

    void HashCombine(uint64& Seed, uint64 Value)
    {
        Seed ^= Value + 0x9e3779b97f4a7c15ull + (Seed << 6) + (Seed >> 2);
    }

    void HashName(uint64& Seed, const FName& Name)
    {
        const FString Text = Name.ToString();
        for (char Ch : Text)
        {
            HashCombine(Seed, static_cast<uint64>(static_cast<unsigned char>(Ch)));
        }
    }

    constexpr float ConstraintGraphBodyStartX = 20.0f;
    constexpr float ConstraintGraphBodyStartY = 30.0f;
    constexpr float ConstraintGraphBodyColumnWidth = 460.0f;
    constexpr float ConstraintGraphRowHeight = 125.0f;
    constexpr float ConstraintGraphConstraintFallbackX = 275.0f;
    constexpr float ConstraintGraphConstraintOffsetX = 220.0f;
    constexpr float ConstraintGraphConstraintOffsetY = 15.0f;

    bool ContainsBodyIndex(const TArray<int32>& Indices, int32 BodyIndex)
    {
        return std::find(Indices.begin(), Indices.end(), BodyIndex) != Indices.end();
    }

    void ArrangeConstraintGraphNodes(UPhysicsAsset* PhysicsAsset)
    {
        if (!PhysicsAsset)
        {
            return;
        }

        const TArray<FPhysicsAssetBodySetup>& Bodies = PhysicsAsset->GetBodySetups();
        const TArray<FPhysicsAssetConstraintSetup>& Constraints = PhysicsAsset->GetConstraintSetups();
        const int32 BodyCount = static_cast<int32>(Bodies.size());
        const int32 ConstraintCount = static_cast<int32>(Constraints.size());
        if (BodyCount <= 0)
        {
            return;
        }

        TArray<TArray<int32>> ChildrenByBody;
        TArray<int32> IncomingCounts;
        TArray<int32> Depths;
        TArray<int32> RootBodies;
        TArray<int32> VisitStates;
        TArray<float> BodyRows;
        ChildrenByBody.resize(BodyCount);
        IncomingCounts.assign(BodyCount, 0);
        Depths.assign(BodyCount, 0);
        VisitStates.assign(BodyCount, 0);
        BodyRows.assign(BodyCount, -1.0f);
        RootBodies.reserve(BodyCount);

        for (const FPhysicsAssetConstraintSetup& Constraint : Constraints)
        {
            const int32 ParentBodyIndex = PhysicsAsset->FindBodySetupIndexByBoneName(Constraint.ParentBoneName);
            const int32 ChildBodyIndex = PhysicsAsset->FindBodySetupIndexByBoneName(Constraint.ChildBoneName);
            if (ParentBodyIndex < 0 || ChildBodyIndex < 0 || ParentBodyIndex == ChildBodyIndex)
            {
                continue;
            }

            TArray<int32>& Children = ChildrenByBody[ParentBodyIndex];
            if (!ContainsBodyIndex(Children, ChildBodyIndex))
            {
                Children.push_back(ChildBodyIndex);
                ++IncomingCounts[ChildBodyIndex];
            }
        }

        for (int32 BodyIndex = 0; BodyIndex < BodyCount; ++BodyIndex)
        {
            if (IncomingCounts[BodyIndex] == 0)
            {
                RootBodies.push_back(BodyIndex);
            }
        }

        if (RootBodies.empty())
        {
            for (int32 BodyIndex = 0; BodyIndex < BodyCount; ++BodyIndex)
            {
                RootBodies.push_back(BodyIndex);
            }
        }

        int32 NextLeafRow = 0;
        std::function<float(int32, int32)> ArrangeBodyTree = [&](int32 BodyIndex, int32 Depth) -> float
        {
            if (BodyIndex < 0 || BodyIndex >= BodyCount)
            {
                return static_cast<float>(NextLeafRow);
            }

            Depths[BodyIndex] = (std::max)(Depths[BodyIndex], Depth);

            if (VisitStates[BodyIndex] == 2)
            {
                return BodyRows[BodyIndex];
            }

            if (VisitStates[BodyIndex] == 1)
            {
                if (BodyRows[BodyIndex] < 0.0f)
                {
                    BodyRows[BodyIndex] = static_cast<float>(NextLeafRow++);
                }
                return BodyRows[BodyIndex];
            }

            VisitStates[BodyIndex] = 1;

            float ChildRowSum = 0.0f;
            int32 ChildRowCount = 0;
            for (int32 ChildBodyIndex : ChildrenByBody[BodyIndex])
            {
                if (ChildBodyIndex < 0 || ChildBodyIndex >= BodyCount || ChildBodyIndex == BodyIndex)
                {
                    continue;
                }

                if (VisitStates[ChildBodyIndex] == 1)
                {
                    continue;
                }

                const float ChildRow = ArrangeBodyTree(ChildBodyIndex, Depth + 1);
                ChildRowSum += ChildRow;
                ++ChildRowCount;
            }

            const float Row = (ChildRowCount > 0)
                ? ChildRowSum / static_cast<float>(ChildRowCount)
                : static_cast<float>(NextLeafRow++);
            BodyRows[BodyIndex] = Row;
            VisitStates[BodyIndex] = 2;
            return Row;
        };

        for (int32 RootBodyIndex : RootBodies)
        {
            ArrangeBodyTree(RootBodyIndex, 0);
        }

        for (int32 BodyIndex = 0; BodyIndex < BodyCount; ++BodyIndex)
        {
            if (VisitStates[BodyIndex] != 2)
            {
                ArrangeBodyTree(BodyIndex, Depths[BodyIndex]);
            }
        }

        TArray<ImVec2> BodyPositions;
        BodyPositions.resize(BodyCount);
        for (int32 BodyIndex = 0; BodyIndex < BodyCount; ++BodyIndex)
        {
            const float Row = (BodyRows[BodyIndex] >= 0.0f) ? BodyRows[BodyIndex] : static_cast<float>(BodyIndex);
            const float X = ConstraintGraphBodyStartX + static_cast<float>(Depths[BodyIndex]) * ConstraintGraphBodyColumnWidth;
            const float Y = ConstraintGraphBodyStartY + Row * ConstraintGraphRowHeight;
            BodyPositions[BodyIndex] = ImVec2(X, Y);
            ed::SetNodePosition(ToPhysicsNodeId(MakeBodyNodeId(BodyIndex)), BodyPositions[BodyIndex]);
        }

        for (int32 ConstraintIndex = 0; ConstraintIndex < ConstraintCount; ++ConstraintIndex)
        {
            const FPhysicsAssetConstraintSetup& Constraint = Constraints[ConstraintIndex];
            const int32 ParentBodyIndex = PhysicsAsset->FindBodySetupIndexByBoneName(Constraint.ParentBoneName);
            const int32 ChildBodyIndex = PhysicsAsset->FindBodySetupIndexByBoneName(Constraint.ChildBoneName);

            ImVec2 Position(
                ConstraintGraphConstraintFallbackX,
                ConstraintGraphBodyStartY + static_cast<float>(ConstraintIndex) * ConstraintGraphRowHeight + ConstraintGraphConstraintOffsetY);

            if (ParentBodyIndex >= 0 && ParentBodyIndex < BodyCount && ChildBodyIndex >= 0 && ChildBodyIndex < BodyCount)
            {
                const ImVec2 ParentPosition = BodyPositions[ParentBodyIndex];
                const ImVec2 ChildPosition = BodyPositions[ChildBodyIndex];
                Position.x = (ParentPosition.x + ChildPosition.x) * 0.5f;
                Position.y = (ParentPosition.y + ChildPosition.y) * 0.5f + ConstraintGraphConstraintOffsetY;

                if (std::fabs(ParentPosition.x - ChildPosition.x) < 1.0f)
                {
                    Position.x += ConstraintGraphConstraintOffsetX;
                }
            }
            else if (ParentBodyIndex >= 0 && ParentBodyIndex < BodyCount)
            {
                const ImVec2 ParentPosition = BodyPositions[ParentBodyIndex];
                Position.x = ParentPosition.x + ConstraintGraphConstraintOffsetX;
                Position.y = ParentPosition.y + ConstraintGraphConstraintOffsetY;
            }
            else if (ChildBodyIndex >= 0 && ChildBodyIndex < BodyCount)
            {
                const ImVec2 ChildPosition = BodyPositions[ChildBodyIndex];
                Position.x = ChildPosition.x - ConstraintGraphConstraintOffsetX;
                Position.y = ChildPosition.y + ConstraintGraphConstraintOffsetY;
            }

            ed::SetNodePosition(ToPhysicsNodeId(MakeConstraintNodeId(ConstraintIndex)), Position);
        }
    }

    bool IsValidBodyIndex(const UPhysicsAsset* PhysicsAsset, int32 BodyIndex)
    {
        return PhysicsAsset && BodyIndex >= 0 && BodyIndex < static_cast<int32>(PhysicsAsset->GetBodySetups().size());
    }

    bool IsValidConstraintIndex(const UPhysicsAsset* PhysicsAsset, int32 ConstraintIndex)
    {
        return PhysicsAsset && ConstraintIndex >= 0 && ConstraintIndex < static_cast<int32>(PhysicsAsset->GetConstraintSetups().size());
    }

    FString SafeNameString(const FName& Name)
    {
        return Name == FName::None ? FString() : Name.ToString();
    }

    json::JSON MakeVectorJson(const FVector& Value)
    {
        json::JSON Result = json::Array();
        Result.append(Value.X);
        Result.append(Value.Y);
        Result.append(Value.Z);
        return Result;
    }

    json::JSON MakeQuatJson(const FQuat& Value)
    {
        json::JSON Result = json::Array();
        Result.append(Value.X);
        Result.append(Value.Y);
        Result.append(Value.Z);
        Result.append(Value.W);
        return Result;
    }

    json::JSON MakeImVec2Json(const ImVec2& Value)
    {
        json::JSON Result = json::Array();
        Result.append(Value.x);
        Result.append(Value.y);
        return Result;
    }

    json::JSON MakeTransformJson(const FTransform& Transform)
    {
        const FRotator Rotation = Transform.GetRotator();

        json::JSON Result = json::Object();
        Result["Location"] = MakeVectorJson(Transform.Location);
        Result["RotationQuat"] = MakeQuatJson(Transform.Rotation);
        Result["RotationEuler"] = MakeVectorJson(FVector(Rotation.Roll, Rotation.Pitch, Rotation.Yaw));
        Result["Scale"] = MakeVectorJson(Transform.Scale);
        return Result;
    }

    json::JSON MakeShapeJson(const FPhysicsAssetShapeSetup& Shape)
    {
        json::JSON Result = json::Object();
        Result["Type"] = ShapeTypeText(Shape.Type);
        Result["LocalTransform"] = MakeTransformJson(Shape.LocalTransform);
        Result["BoxHalfExtent"] = MakeVectorJson(Shape.BoxHalfExtent);
        Result["SphereRadius"] = Shape.SphereRadius;
        Result["CapsuleRadius"] = Shape.CapsuleRadius;
        Result["CapsuleHalfHeight"] = Shape.CapsuleHalfHeight;
        Result["ConvexVertexData"] = json::Array();
        return Result;
    }

    json::JSON MakeBodyJson(const FPhysicsAssetBodySetup& Body, int32 BodyIndex)
    {
        json::JSON Result = json::Object();
        Result["Index"] = BodyIndex;
        Result["TargetBoneName"] = SafeNameString(Body.BoneName);
        Result["BodyType"] = "Dynamic";
        Result["BodyLocalFrame"] = MakeTransformJson(Body.BodyLocalFrame);
        Result["Mass"] = Body.Mass;
        Result["CenterOfMassLocalOffset"] = MakeVectorJson(Body.CenterOfMassLocalOffset);
        Result["LinearDamping"] = Body.LinearDamping;
        Result["AngularDamping"] = Body.AngularDamping;
        Result["MaxAngularVelocity"] = Body.MaxAngularVelocity;
        Result["PositionSolverIterationCount"] = Body.PositionSolverIterationCount;
        Result["VelocitySolverIterationCount"] = Body.VelocitySolverIterationCount;
        Result["EnableCCD"] = Body.bEnableCCD;
        Result["EnableGravity"] = Body.bEnableGravity;
        Result["CollisionComplexity"] = "Simple";
        Result["PhysicalMaterialPath"] = "";

        json::JSON Locks = json::Object();
        Locks["LinearX"] = Body.bLockLinearX;
        Locks["LinearY"] = Body.bLockLinearY;
        Locks["LinearZ"] = Body.bLockLinearZ;
        Locks["AngularX"] = Body.bLockAngularX;
        Locks["AngularY"] = Body.bLockAngularY;
        Locks["AngularZ"] = Body.bLockAngularZ;
        Result["Locks"] = Locks;

        json::JSON Shapes = json::Array();
        for (int32 ShapeIndex = 0; ShapeIndex < static_cast<int32>(Body.Shapes.size()); ++ShapeIndex)
        {
            json::JSON ShapeJson = MakeShapeJson(Body.Shapes[ShapeIndex]);
            ShapeJson["Index"] = ShapeIndex;
            Shapes.append(ShapeJson);
        }
        Result["Shapes"] = Shapes;
        return Result;
    }

    json::JSON MakeConstraintMotionJson(const FConstraintLimitDesc& Limits)
    {
        json::JSON Result = json::Object();
        Result["X"] = MotionText(Limits.LinearX);
        Result["Y"] = MotionText(Limits.LinearY);
        Result["Z"] = MotionText(Limits.LinearZ);
        Result["Swing1"] = MotionText(Limits.Swing1);
        Result["Swing2"] = MotionText(Limits.Swing2);
        Result["Twist"] = MotionText(Limits.Twist);
        return Result;
    }

    json::JSON MakeConstraintLimitsJson(const FConstraintLimitDesc& Limits)
    {
        json::JSON Result = json::Object();
        Result["Motions"] = MakeConstraintMotionJson(Limits);
        Result["TwistLimitMinDegrees"] = Limits.TwistLimitMinDegrees;
        Result["TwistLimitMaxDegrees"] = Limits.TwistLimitMaxDegrees;
        Result["Swing1LimitDegrees"] = Limits.Swing1LimitDegrees;
        Result["Swing2LimitDegrees"] = Limits.Swing2LimitDegrees;
        Result["EnableProjection"] = Limits.bEnableProjection;
        return Result;
    }

    json::JSON MakeConstraintJson(const FPhysicsAssetConstraintSetup& Constraint, int32 ConstraintIndex)
    {
        json::JSON Result = json::Object();
        Result["Index"] = ConstraintIndex;
        Result["Name"] = FString("Constraint_") + std::to_string(ConstraintIndex);
        Result["ParentBoneName"] = SafeNameString(Constraint.ParentBoneName);
        Result["ChildBoneName"] = SafeNameString(Constraint.ChildBoneName);
        Result["JointType"] = "Generic6DOF";
        Result["ParentLocalFrame"] = MakeTransformJson(Constraint.ParentLocalFrame);
        Result["ChildLocalFrame"] = MakeTransformJson(Constraint.ChildLocalFrame);
        Result["Limits"] = MakeConstraintLimitsJson(Constraint.Limits);
        Result["Motions"] = MakeConstraintMotionJson(Constraint.Limits);

        json::JSON PhysicalProperties = json::Object();
        PhysicalProperties["Stiffness"] = 0.0f;
        PhysicalProperties["Damping"] = 0.0f;
        PhysicalProperties["DisableCollisionBetweenBodies"] = Constraint.bDisableCollisionBetweenBodies;
        Result["PhysicalProperties"] = PhysicalProperties;
        return Result;
    }


}

FPhysicsAssetEditorWidget::~FPhysicsAssetEditorWidget()
{
    DestroyConstraintGraphEditor();
}

void FPhysicsAssetEditorWidget::Close()
{
    DestroyConstraintGraphEditor();
    if (PreviewSkeletalMeshComponent)
    {
        StopEditorSimulation(PreviewSimulationWorld, PreviewSkeletalMeshComponent, true);
    }
    PreviewSkeletalMesh = nullptr;
    PreviewSkeletalMeshComponent = nullptr;
    PreviewSimulationWorld = nullptr;
    SelectedBodyIndex = -1;
    SelectedShapeIndex = -1;
    SelectedConstraintIndex = -1;
    SelectedTreeBoneIndex = -1;
    bPendingClose = false;
    bEditorSimulationActive = false;
    bEditorSimulationPaused = false;
    bEditorSimulationRestartRequested = false;
    bConstraintGraphLayoutDirty = true;
    ConstraintGraphTopologyHash = 0;
    FAssetEditorWidget::Close();
}

bool FPhysicsAssetEditorWidget::CanEdit(UObject* Object) const
{
    return Object && Object->IsA<UPhysicsAsset>();
}

bool FPhysicsAssetEditorWidget::IsEditingObject(UObject* Object) const
{
    const UPhysicsAsset* A = Cast<UPhysicsAsset>(Object);
    const UPhysicsAsset* B = GetEditedPhysicsAsset();
    if (!A || !B || !IsOpen()) return false;

    const FString APath = A->GetAssetPathFileName();
    const FString BPath = B->GetAssetPathFileName();
    return !APath.empty() && APath != "None" && APath == BPath;
}

void FPhysicsAssetEditorWidget::Open(UObject* Object)
{
    if (!CanEdit(Object)) return;

    EditedObject = Object;
    bOpen = true;
    bPendingClose = false;
    bConstraintGraphLayoutDirty = true;
    ConstraintGraphTopologyHash = 0;
    SelectedBodyIndex = -1;
    SelectedShapeIndex = -1;
    SelectedConstraintIndex = -1;
    SelectedTreeBoneIndex = -1;
    PreviewSkeletalMesh = nullptr;
    PreviewSkeletalMeshComponent = nullptr;
    PreviewSimulationWorld = nullptr;
    bEditorSimulationActive = false;
    bEditorSimulationPaused = false;
    bEditorSimulationRestartRequested = false;
    ValidationIssues.clear();
    ValidationMeshPath.clear();
    bValidationRan = false;
    ClearDirty();
    RequestFocus();
}

void FPhysicsAssetEditorWidget::Render(float DeltaTime)
{
    if (!IsOpen() || !EditedObject) return;

    bool bWindowOpen = true;
    FString Title = GetDocumentTitle();
    if (IsDirty()) Title += " *";
    Title += "###PhysicsAssetEditor";

    ImGui::SetNextWindowSize(ImVec2(960.0f, 620.0f), ImGuiCond_Once);
    if (!ImGui::Begin(Title.c_str(), &bWindowOpen))
    {
        ImGui::End();
        if (!bWindowOpen) Close();
        return;
    }

    RenderDocument(DeltaTime);
    ImGui::End();
    if (!bWindowOpen) bPendingClose = true;
}

void FPhysicsAssetEditorWidget::OpenEmbedded(UPhysicsAsset* PhysicsAsset)
{
    Open(PhysicsAsset);
}

void FPhysicsAssetEditorWidget::RenderEmbedded(UPhysicsAsset* PhysicsAsset, float DeltaTime)
{
    RenderEmbedded(PhysicsAsset, nullptr, DeltaTime);
}

void FPhysicsAssetEditorWidget::RenderEmbedded(UPhysicsAsset* PhysicsAsset, USkeletalMesh* PreviewMesh, float DeltaTime)
{
    PreviewSkeletalMesh = PreviewMesh;

    if (!PhysicsAsset)
    {
        Close();
        ImGui::TextDisabled("No PhysicsAsset selected.");
        return;
    }

    if (!IsEditingObject(PhysicsAsset))
    {
        UWorld* SavedPreviewWorld = PreviewSimulationWorld;
        USkeletalMeshComponent* SavedPreviewComponent = PreviewSkeletalMeshComponent;
        OpenEmbedded(PhysicsAsset);
        PreviewSkeletalMesh = PreviewMesh;
        PreviewSimulationWorld = SavedPreviewWorld;
        PreviewSkeletalMeshComponent = SavedPreviewComponent;
    }

    RenderDocument(DeltaTime);
}

void FPhysicsAssetEditorWidget::RenderEmbeddedToolbar(UPhysicsAsset* PhysicsAsset, USkeletalMesh* PreviewMesh, float DeltaTime)
{
    (void)DeltaTime;
    if (!PrepareEmbeddedRender(PhysicsAsset, PreviewMesh))
    {
        return;
    }

    ClampSelection(PhysicsAsset);
    RenderToolbar(PhysicsAsset);
}

void FPhysicsAssetEditorWidget::RenderEmbeddedTreeAndGraph(UPhysicsAsset* PhysicsAsset, USkeletalMesh* PreviewMesh, float DeltaTime)
{
    (void)DeltaTime;
    if (!PrepareEmbeddedRender(PhysicsAsset, PreviewMesh))
    {
        return;
    }

    ClampSelection(PhysicsAsset);
    RenderTreeAndGraphPanel(PhysicsAsset);
}

void FPhysicsAssetEditorWidget::RenderEmbeddedDetails(UPhysicsAsset* PhysicsAsset, USkeletalMesh* PreviewMesh, float DeltaTime)
{
    (void)DeltaTime;
    if (!PrepareEmbeddedRender(PhysicsAsset, PreviewMesh))
    {
        return;
    }

    ClampSelection(PhysicsAsset);
    RenderDetailsAndValidationPanel(PhysicsAsset);
}

bool FPhysicsAssetEditorWidget::SaveEditedPhysicsAsset()
{
    UPhysicsAsset* PhysicsAsset = GetEditedPhysicsAsset();
    if (!PhysicsAsset)
    {
        return false;
    }

    if (FPhysicsAssetManager::Get().SavePhysicsAssetPreservingMetadata(PhysicsAsset))
    {
        ClearDirty();
        bValidationRan = false;
        return true;
    }
    return false;
}

bool FPhysicsAssetEditorWidget::ExportPhysicsAssetDebugJson(UPhysicsAsset* PhysicsAsset, FString* OutPath)
{
    if (!PhysicsAsset)
    {
        return false;
    }

    FPaths::CreateDir(FPaths::SaveDir());
    const std::wstring OutputPath = FPaths::Combine(FPaths::SaveDir(), L"PhysicsAsset_Debug.json");
    if (OutPath)
    {
        *OutPath = FPaths::ToUtf8(OutputPath);
    }

    json::JSON Root = json::Object();
    Root["ClassName"] = "PhysicsAsset";
    Root["Format"] = "PhysicsAssetDebugExport";
    Root["Version"] = 1;
    Root["AssetPath"] = PhysicsAsset->GetAssetPathFileName();
    Root["PreviewSkeletalMeshPath"] = PreviewSkeletalMesh ? PreviewSkeletalMesh->GetAssetPathFileName() : FString();

    const FSkeletonBinding& Binding = PhysicsAsset->GetSkeletonBinding();
    Root["SkeletonPath"] = Binding.SkeletonPath;

    json::JSON EditorState = json::Object();
    EditorState["SelectedBodyIndex"] = SelectedBodyIndex;
    EditorState["SelectedShapeIndex"] = SelectedShapeIndex;
    EditorState["SelectedConstraintIndex"] = SelectedConstraintIndex;
    EditorState["SelectedTreeBoneIndex"] = SelectedTreeBoneIndex;
    EditorState["TreePanelMode"] = bPhysicsTreePanelShowsBodies ? "Bodies" : "Tree";
    EditorState["ShowOnlyBonesWithBodies"] = bShowOnlyBonesWithBodies;
    EditorState["ShowPreviewBodies"] = bShowPreviewBodies;
    EditorState["ShowPreviewConstraints"] = bShowPreviewConstraints;
    EditorState["ShowPreviewBodySkeleton"] = bShowPreviewBodySkeleton;
    EditorState["ShowConstraintLimitAngles"] = bShowConstraintLimitAngles;
    EditorState["ShowConstraintLimitSurfaces"] = bShowConstraintLimitSurfaces;
    EditorState["ShowOnlySelectedConstraintLimitAngles"] = bShowOnlySelectedConstraintLimitAngles;
    Root["EditorState"] = EditorState;

    json::JSON GraphViewState = json::Object();
    GraphViewState["Zoom"] = 1.0f;
    GraphViewState["Pan"] = MakeImVec2Json(ImVec2(0.0f, 0.0f));
    json::JSON NodeLayouts = json::Array();
    if (ConstraintGraphContext)
    {
        ed::SetCurrentEditor(ConstraintGraphContext);
        GraphViewState["Zoom"] = ed::GetCurrentZoom();
        GraphViewState["Pan"] = MakeImVec2Json(ed::ScreenToCanvas(ImVec2(0.0f, 0.0f)));

        const TArray<FPhysicsAssetBodySetup>& Bodies = PhysicsAsset->GetBodySetups();
        for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(Bodies.size()); ++BodyIndex)
        {
            json::JSON Node = json::Object();
            Node["Kind"] = "Body";
            Node["Index"] = BodyIndex;
            Node["NodeId"] = MakeBodyNodeId(BodyIndex);
            Node["BoneName"] = SafeNameString(Bodies[BodyIndex].BoneName);
            Node["Position"] = MakeImVec2Json(ed::GetNodePosition(ToPhysicsNodeId(MakeBodyNodeId(BodyIndex))));
            NodeLayouts.append(Node);
        }

        const TArray<FPhysicsAssetConstraintSetup>& Constraints = PhysicsAsset->GetConstraintSetups();
        for (int32 ConstraintIndex = 0; ConstraintIndex < static_cast<int32>(Constraints.size()); ++ConstraintIndex)
        {
            json::JSON Node = json::Object();
            Node["Kind"] = "Constraint";
            Node["Index"] = ConstraintIndex;
            Node["NodeId"] = MakeConstraintNodeId(ConstraintIndex);
            Node["ParentBoneName"] = SafeNameString(Constraints[ConstraintIndex].ParentBoneName);
            Node["ChildBoneName"] = SafeNameString(Constraints[ConstraintIndex].ChildBoneName);
            Node["Position"] = MakeImVec2Json(ed::GetNodePosition(ToPhysicsNodeId(MakeConstraintNodeId(ConstraintIndex))));
            NodeLayouts.append(Node);
        }
    }
    GraphViewState["NodeLayouts"] = NodeLayouts;
    Root["GraphViewState"] = GraphViewState;

    json::JSON BodiesJson = json::Array();
    const TArray<FPhysicsAssetBodySetup>& Bodies = PhysicsAsset->GetBodySetups();
    for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(Bodies.size()); ++BodyIndex)
    {
        BodiesJson.append(MakeBodyJson(Bodies[BodyIndex], BodyIndex));
    }
    Root["Bodies"] = BodiesJson;

    json::JSON ConstraintsJson = json::Array();
    const TArray<FPhysicsAssetConstraintSetup>& Constraints = PhysicsAsset->GetConstraintSetups();
    for (int32 ConstraintIndex = 0; ConstraintIndex < static_cast<int32>(Constraints.size()); ++ConstraintIndex)
    {
        ConstraintsJson.append(MakeConstraintJson(Constraints[ConstraintIndex], ConstraintIndex));
    }
    Root["Constraints"] = ConstraintsJson;

    std::ofstream File(OutputPath, std::ios::out | std::ios::trunc);
    if (!File.is_open())
    {
        return false;
    }

    File << Root.dump(4);
    return true;
}

void FPhysicsAssetEditorWidget::NotifyViewportGizmoModified()
{
    MarkPhysicsAssetDirty();
}

void FPhysicsAssetEditorWidget::SelectPhysicsShapeFromViewport(UPhysicsAsset* PhysicsAsset, int32 BodyIndex, int32 ShapeIndex)
{
    if (!PhysicsAsset || !IsValidBodyIndex(PhysicsAsset, BodyIndex))
    {
        return;
    }

    if (!IsEditingObject(PhysicsAsset))
    {
        OpenEmbedded(PhysicsAsset);
    }

    const TArray<FPhysicsAssetBodySetup>& Bodies = PhysicsAsset->GetBodySetups();
    const FPhysicsAssetBodySetup& Body = Bodies[BodyIndex];

    SelectedBodyIndex = BodyIndex;
    SelectedShapeIndex =
        ShapeIndex >= 0 && ShapeIndex < static_cast<int32>(Body.Shapes.size())
            ? ShapeIndex
            : -1;
    SelectedConstraintIndex = -1;
    SelectedTreeBoneIndex = FindPreviewBoneIndexByName(Body.BoneName);
    bPhysicsTreePanelShowsBodies = false;
    bPendingScrollSelectedTreeBoneIntoView = SelectedTreeBoneIndex >= 0;
    bPendingConstraintGraphNavigateToSelection = true;
    if (bEditorSimulationActive && bEditorSimulationSelectedOnly)
    {
        RequestEditorSimulationRestart();
    }
}

void FPhysicsAssetEditorWidget::SelectPhysicsConstraintFromViewport(UPhysicsAsset* PhysicsAsset, int32 ConstraintIndex)
{
    if (!PhysicsAsset || !IsValidConstraintIndex(PhysicsAsset, ConstraintIndex))
    {
        return;
    }

    if (!IsEditingObject(PhysicsAsset))
    {
        OpenEmbedded(PhysicsAsset);
    }

    SelectConstraintSetup(PhysicsAsset, ConstraintIndex, -1);
    bPendingConstraintGraphNavigateToSelection = true;
    if (bEditorSimulationActive && bEditorSimulationSelectedOnly)
    {
        RequestEditorSimulationRestart();
    }
}

bool FPhysicsAssetEditorWidget::ConsumeConstraintGraphViewportFocusRequest()
{
    const bool bWasRequested = bPendingConstraintGraphViewportFocusRequest;
    bPendingConstraintGraphViewportFocusRequest = false;
    return bWasRequested;
}

bool FPhysicsAssetEditorWidget::DeleteSelectedPhysicsAssetElement(UPhysicsAsset* PhysicsAsset)
{
    if (!PhysicsAsset)
    {
        return false;
    }

    if (IsValidConstraintIndex(PhysicsAsset, SelectedConstraintIndex))
    {
        if (!PhysicsAsset->RemoveConstraintSetupByIndex(SelectedConstraintIndex))
        {
            return false;
        }

        SelectedConstraintIndex = -1;
        SelectedBodyIndex = -1;
        SelectedShapeIndex = -1;
        MarkPhysicsAssetDirty();
        return true;
    }

    if (IsValidBodyIndex(PhysicsAsset, SelectedBodyIndex))
    {
        if (!PhysicsAsset->RemoveBodySetupByIndex(SelectedBodyIndex))
        {
            return false;
        }

        SelectedBodyIndex = -1;
        SelectedShapeIndex = -1;
        SelectedConstraintIndex = -1;
        MarkPhysicsAssetDirty();
        return true;
    }

    return false;
}

void FPhysicsAssetEditorWidget::RenderDocument(float DeltaTime)
{
    (void)DeltaTime;
    if (bPendingClose)
    {
        Close();
        bPendingClose = false;
        return;
    }

    UPhysicsAsset* PhysicsAsset = GetEditedPhysicsAsset();
    if (!PhysicsAsset)
    {
        ImGui::TextDisabled("No PhysicsAsset selected.");
        return;
    }

    ClampSelection(PhysicsAsset);
    RenderToolbar(PhysicsAsset);
    ImGui::Separator();

    const float TotalWidth = ImGui::GetContentRegionAvail().x;
    const float LeftWidth = (std::min)(LeftPanelWidth, (std::max)(240.0f, TotalWidth * 0.36f));
    const float RightWidth = (std::max)(DetailsPanelMinWidth, TotalWidth - LeftWidth - ImGui::GetStyle().ItemSpacing.x);

    ImGui::BeginChild("##PhysicsAssetLeft", ImVec2(LeftWidth, 0.0f), true);
    RenderTreeAndGraphPanel(PhysicsAsset);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##PhysicsAssetRight", ImVec2(RightWidth, 0.0f), true);
    RenderDetailsAndValidationPanel(PhysicsAsset);
    ImGui::EndChild();
}

FString FPhysicsAssetEditorWidget::GetDocumentTitle() const
{
    const UPhysicsAsset* Asset = GetEditedPhysicsAsset();
    const FString Path = Asset ? Asset->GetAssetPathFileName() : FString();
    return Path.empty() || Path == "None" ? FString("Physics Asset") : FString("Physics Asset - " + Path);
}

FString FPhysicsAssetEditorWidget::GetDocumentPayloadId() const
{
    const UPhysicsAsset* Asset = GetEditedPhysicsAsset();
    const FString Path = Asset ? Asset->GetAssetPathFileName() : FString();
    return Path.empty() || Path == "None" ? FAssetEditorWidget::GetDocumentPayloadId() : Path;
}

UPhysicsAsset* FPhysicsAssetEditorWidget::GetEditedPhysicsAsset() const
{
    return Cast<UPhysicsAsset>(EditedObject);
}

bool FPhysicsAssetEditorWidget::PrepareEmbeddedRender(UPhysicsAsset* PhysicsAsset, USkeletalMesh* PreviewMesh)
{
    PreviewSkeletalMesh = PreviewMesh;

    if (!PhysicsAsset)
    {
        Close();
        ImGui::TextDisabled("No PhysicsAsset selected.");
        return false;
    }

    if (!IsEditingObject(PhysicsAsset))
    {
        UWorld* SavedPreviewWorld = PreviewSimulationWorld;
        USkeletalMeshComponent* SavedPreviewComponent = PreviewSkeletalMeshComponent;
        OpenEmbedded(PhysicsAsset);
        PreviewSkeletalMesh = PreviewMesh;
        PreviewSimulationWorld = SavedPreviewWorld;
        PreviewSkeletalMeshComponent = SavedPreviewComponent;
    }

    return true;
}

void FPhysicsAssetEditorWidget::RenderTreeAndGraphPanel(UPhysicsAsset* PhysicsAsset)
{
    const float AvailableHeight = ImGui::GetContentRegionAvail().y;
    const float SeparatorHeight = ImGui::GetStyle().ItemSpacing.y + 4.0f;
    constexpr float DesiredTreePanelRatio = 0.45f;
    constexpr float MinTreePanelHeight = 160.0f;
    constexpr float MinGraphPanelHeight = 260.0f;
    const float DesiredTreeHeight = (std::max)(MinTreePanelHeight, AvailableHeight * DesiredTreePanelRatio);
    const float MaxTreeHeight = (std::max)(MinTreePanelHeight, AvailableHeight - MinGraphPanelHeight - SeparatorHeight);
    const float TreeHeight = (std::min)(DesiredTreeHeight, MaxTreeHeight);

    ImGui::BeginChild(
        "##PhysicsAssetTreeArea",
        ImVec2(0.0f, TreeHeight),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (PreviewSkeletalMesh && PreviewSkeletalMesh->GetSkeleton())
    {
        RenderSkeletonPhysicsTree(PhysicsAsset, PreviewSkeletalMesh);
    }
    else
    {
        ImGui::TextDisabled("No preview Skeleton. Falling back to raw setup lists.");
        ImGui::Separator();
        RenderBodyList(PhysicsAsset);
        ImGui::Separator();
        RenderConstraintList(PhysicsAsset);
    }
    ImGui::EndChild();

    ImGui::Separator();
    RenderConstraintGraphPanel(PhysicsAsset);
}

void FPhysicsAssetEditorWidget::RenderDetailsAndValidationPanel(UPhysicsAsset* PhysicsAsset)
{
    RenderDetailsPanel(PhysicsAsset);

    // Validation is currently unused in the Physics Editor UI.
    // Keep the implementation available, but do not expose the panel while the toolbar action is hidden.
    // ImGui::Separator();
    // RenderValidationPanel();
}

void FPhysicsAssetEditorWidget::RenderToolbar(UPhysicsAsset* PhysicsAsset)
{
    // Physics assets are saved through the owning Mesh Editor, so the duplicated Save button is hidden here.
    // Validation is also currently unused; keep the code path available for future re-enable.
    // const bool bCanSave = PhysicsAsset && !PhysicsAsset->GetAssetPathFileName().empty() && PhysicsAsset->GetAssetPathFileName() != "None";
    // if (!bCanSave) ImGui::BeginDisabled();
    // if (ImGui::Button(IsDirty() ? "Save*" : "Save", ImVec2(72.0f, 0.0f)))
    // {
    //     SaveEditedPhysicsAsset();
    // }
    // if (!bCanSave) ImGui::EndDisabled();

    // ImGui::SameLine();
    // if (ImGui::Button("Validate", ImVec2(86.0f, 0.0f)))
    // {
    //     RunValidation(PhysicsAsset);
    // }

    RenderSimulationControls(PhysicsAsset);
    ImGui::SameLine();
    RenderRegenerateBodiesControls(PhysicsAsset);
    ImGui::SameLine();
    if (ImGui::Button("Export JSON", ImVec2(110.0f, 0.0f)))
    {
        FString ExportPath;
        const bool bExported = ExportPhysicsAssetDebugJson(PhysicsAsset, &ExportPath);
        LastDebugExportMessage = bExported
            ? FString("Exported: ") + ExportPath
            : FString("Export JSON failed");
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Export the current PhysicsAsset editor/debug state to Saves/PhysicsAsset_Debug.json.");
    }

    if (!LastDebugExportMessage.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", LastDebugExportMessage.c_str());
    }
}

void FPhysicsAssetEditorWidget::RenderSimulationControls(UPhysicsAsset* PhysicsAsset)
{
    const bool bCanSimulate = PhysicsAsset && PreviewSkeletalMesh && PreviewSkeletalMeshComponent && PreviewSimulationWorld;

    if (!bCanSimulate)
    {
        ImGui::BeginDisabled();
    }

    if (!bEditorSimulationActive)
    {
        if (ImGui::Button("Simulate", ImVec2(96.0f, 0.0f)))
        {
            StartEditorSimulation(PhysicsAsset, PreviewSimulationWorld, PreviewSkeletalMeshComponent);
        }
    }
    else
    {
        if (ImGui::Button("Stop", ImVec2(72.0f, 0.0f)))
        {
            StopEditorSimulation(PreviewSimulationWorld, PreviewSkeletalMeshComponent, true);
        }
        ImGui::SameLine();
        if (ImGui::Button(bEditorSimulationPaused ? "Resume" : "Pause", ImVec2(78.0f, 0.0f)))
        {
            bEditorSimulationPaused = !bEditorSimulationPaused;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset", ImVec2(68.0f, 0.0f)))
        {
            StopEditorSimulation(PreviewSimulationWorld, PreviewSkeletalMeshComponent, true);
            StartEditorSimulation(PhysicsAsset, PreviewSimulationWorld, PreviewSkeletalMeshComponent);
        }
    }

    ImGui::SameLine();
    const bool bOldNoGravity = bEditorSimulationNoGravity;
    if (ImGui::Checkbox("No Gravity", &bEditorSimulationNoGravity) && bOldNoGravity != bEditorSimulationNoGravity)
    {
        RequestEditorSimulationRestart();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Run the preview with gravity disabled. Useful for checking interpenetration and constraint limits without falling motion.");
    }

    ImGui::SameLine();
    const bool bOldSelectedOnly = bEditorSimulationSelectedOnly;
    if (ImGui::Checkbox("Selected Simulation", &bEditorSimulationSelectedOnly) &&
        bOldSelectedOnly != bEditorSimulationSelectedOnly)
    {
        RequestEditorSimulationRestart();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Simulate the selected body and its descendant body chain. Other bodies are kept kinematic as anchors.");
    }

    if (!bCanSimulate)
    {
        ImGui::EndDisabled();
    }

    if (bEditorSimulationSelectedOnly && GetSelectedSimulationRootBoneName(PhysicsAsset) == FName::None)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("Select a body first");
    }
}

void FPhysicsAssetEditorWidget::RenderRegenerateBodiesControls(UPhysicsAsset* PhysicsAsset)
{
    constexpr const char* RegenerateBodiesPopupName = "Regenerate Bodies Options";

    const bool bCanRegenerate = PhysicsAsset &&
        PreviewSkeletalMesh &&
        PreviewSkeletalMesh->GetSkeleton() &&
        PreviewSkeletalMesh->GetSkeletalMeshAsset();

    if (!bCanRegenerate) ImGui::BeginDisabled();
    if (ImGui::Button("Regenerate Bodies", ImVec2(150.0f, 0.0f)))
    {
        ImGui::OpenPopup(RegenerateBodiesPopupName);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Rebuild PhysicsAsset bodies from the preview skeletal mesh bind pose.");
    }
    if (!bCanRegenerate) ImGui::EndDisabled();

    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(RegenerateBodiesPopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Regenerate physics bodies from the preview skeletal mesh.");
        ImGui::Separator();

        if (ImGui::Checkbox("Use PCA Analysis", &bRegenerateUsePCAAnalysis))
        {
            if (bRegenerateUsePCAAnalysis)
            {
                bRegenerateUseBoneAxis = false;
            }
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Fit each capsule to weighted skinned vertices with principal component analysis. Bone-axis fallback is optional.");
        }

        if (ImGui::Checkbox("Use Bone Axis", &bRegenerateUseBoneAxis))
        {
            if (bRegenerateUseBoneAxis)
            {
                bRegenerateUsePCAAnalysis = false;
            }
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Use the bone-to-child direction as the generated capsule axis. This is the common simple bone-axis fitting mode.");
        }

        if (!bRegenerateUsePCAAnalysis && !bRegenerateUseBoneAxis)
        {
            bRegenerateUseBoneAxis = true;
        }

        const char* PrimitiveTypeItems[] = { "Capsule", "Box", "Sphere" };
        RegeneratePrimitiveTypeIndex = (std::min)((std::max)(RegeneratePrimitiveTypeIndex, 0), 2);
        ImGui::SetNextItemWidth(150.0f);
        ImGui::Combo("Primitive", &RegeneratePrimitiveTypeIndex, PrimitiveTypeItems, IM_ARRAYSIZE(PrimitiveTypeItems));
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Select the primitive generated for each fitted body. Capsule keeps the old behavior; Box and Sphere use the same fitted bounds.");
        }

        ImGui::Checkbox("Merge Small Bones", &bRegenerateMergeSmallBones);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Small bones do not receive their own bodies; their weighted vertices are folded into the nearest parent body candidate.");
        }

        if (!bRegenerateMergeSmallBones) ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputFloat("Min Bone Size", &RegenerateMinBoneSize, 0.0f, 0.0f, "%.2f"))
        {
            RegenerateMinBoneSize = (std::max)(RegenerateMinBoneSize, 0.0f);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Bones with a merged bounds size below this value are skipped and merged into their parent. This is scale-dependent.");
        }

        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputFloat("Min Weld Size", &RegenerateMinWeldSize, 0.0f, 0.0f, "%.4f"))
        {
            RegenerateMinWeldSize = (std::max)(RegenerateMinWeldSize, 0.0f);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Tiny or empty fits below this size are ignored instead of being propagated upward.");
        }
        if (!bRegenerateMergeSmallBones) ImGui::EndDisabled();

        ImGui::Checkbox("Constraints", &bRegenerateCreateConstraints);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Create parent-child constraints between the generated bodies.");
        }

        if (!bRegenerateCreateConstraints) ImGui::BeginDisabled();
        ImGui::Checkbox("Disable Adjacent Pair Collision", &bRegenerateDisableConstrainedBodyCollision);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Disable collision between each generated adjacent parent-child constraint pair.");
        }
        if (!bRegenerateCreateConstraints) ImGui::EndDisabled();

        ImGui::Checkbox("Replace", &bRegenerateReplaceExisting);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Clear existing bodies and constraints before regeneration. Disable this to fill only missing bodies.");
        }

        ImGui::Checkbox("Skip Helper Bones", &bRegenerateSkipHelperBones);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Skip root, IK, socket, twist, control, dummy, and similar helper bones when regenerating bodies.");
        }

        ImGui::Checkbox("Allow Bone-Axis Fallback", &bRegenerateAllowBoneAxisFallback);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("When a bone has too few weighted vertices or PCA fails, generate a rough body from the bone axis instead of skipping it.");
        }

        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputFloat("Min Weight", &RegenerateMinInfluenceWeight, 0.0f, 0.0f, "%.2f"))
        {
            RegenerateMinInfluenceWeight = (std::min)((std::max)(RegenerateMinInfluenceWeight, 0.0f), 1.0f);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Minimum skin weight required for a vertex to be used by the selected bone. The vertex no longer needs to be dominated by that bone.");
        }

        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputInt("Min Vertices", &RegenerateMinWeightedVertices))
        {
            RegenerateMinWeightedVertices = (std::max)(RegenerateMinWeightedVertices, 1);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Minimum weighted vertices required before generating a body for a bone.");
        }

        ImGui::Separator();
        if (!bCanRegenerate) ImGui::BeginDisabled();
        if (ImGui::Button("Regenerate", ImVec2(110.0f, 0.0f)))
        {
            RegenerateBodies(PhysicsAsset, PreviewSkeletalMesh);
            ImGui::CloseCurrentPopup();
        }
        if (!bCanRegenerate) ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void FPhysicsAssetEditorWidget::RenderAssetSummary(UPhysicsAsset* PhysicsAsset)
{
    const FSkeletonBinding& Binding = PhysicsAsset->GetSkeletonBinding();
    ImGui::TextUnformatted("Physics Asset");
    ImGui::TextDisabled("Skeleton: %s", Binding.SkeletonPath.c_str());
    ImGui::TextDisabled("Bodies: %d  Constraints: %d", static_cast<int32>(PhysicsAsset->GetBodySetups().size()), static_cast<int32>(PhysicsAsset->GetConstraintSetups().size()));
}

void FPhysicsAssetEditorWidget::RenderSkeletonPhysicsTree(UPhysicsAsset* PhysicsAsset, USkeletalMesh* PreviewMesh)
{
    USkeleton* Skeleton = PreviewMesh ? PreviewMesh->GetSkeleton() : nullptr;
    if (!Skeleton)
    {
        RenderBodyList(PhysicsAsset);
        ImGui::Separator();
        RenderConstraintList(PhysicsAsset);
        return;
    }

    const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("Skeleton Tree");
	ImGui::SetWindowFontScale(1.0f);
    ImGui::SameLine();
    ImGui::Checkbox("Show Only Bones with Bodies", &bShowOnlyBonesWithBodies);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Show only bones that have physics bodies, plus the parent chain needed to reach them.");
    }

    const float ToggleButtonWidth = (std::max)(
        70.0f,
        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);

    const bool bTreeTabActive = !bPhysicsTreePanelShowsBodies;
    if (bTreeTabActive)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    if (ImGui::Button("Tree", ImVec2(ToggleButtonWidth, 0.0f)))
    {
        bPhysicsTreePanelShowsBodies = false;
    }
    if (bTreeTabActive)
    {
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();

    const bool bBodiesTabActive = bPhysicsTreePanelShowsBodies;
    if (bBodiesTabActive)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    if (ImGui::Button("Bodies", ImVec2(ToggleButtonWidth, 0.0f)))
    {
        bPhysicsTreePanelShowsBodies = true;
    }
    if (bBodiesTabActive)
    {
        ImGui::PopStyleColor();
    }

    ImGui::TextDisabled(
        "Bones: %d  Bodies: %d  Constraints: %d",
        RefSkeleton.GetNumBones(),
        static_cast<int32>(PhysicsAsset->GetBodySetups().size()),
        static_cast<int32>(PhysicsAsset->GetConstraintSetups().size()));
    ImGui::Separator();

    if (bPhysicsTreePanelShowsBodies)
    {
        ImGui::BeginChild("##PhysicsAssetBodiesTab", ImVec2(0.0f, 0.0f), true);
        RenderBodyListContent(PhysicsAsset);
        ImGui::EndChild();
        return;
    }

    const float ActionPanelHeight = ImGui::GetFrameHeightWithSpacing();
    const float TreeHeight = (std::max)(
        80.0f,
        ImGui::GetContentRegionAvail().y
            - ActionPanelHeight
            - ImGui::GetStyle().ItemSpacing.y);
    ImGui::BeginChild("##PhysicsAssetSkeletonTree", ImVec2(0.0f, TreeHeight), true);
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, PhysicsEditorTreeIndentSpacing);
    if (RefSkeleton.GetNumBones() <= 0)
    {
        ImGui::TextDisabled("Skeleton has no bones.");
    }
    else
    {
        for (int32 Index = 0; Index < RefSkeleton.GetNumBones(); ++Index)
        {
            if (RefSkeleton.Bones[Index].ParentIndex == -1)
            {
                RenderPhysicsBoneTree(PhysicsAsset, RefSkeleton, Index);
            }
        }
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();

    RenderSelectedBoneActionPanel(PhysicsAsset, RefSkeleton);

    RenderUnboundPhysicsSetups(PhysicsAsset, RefSkeleton);
}

void FPhysicsAssetEditorWidget::RenderPhysicsBoneTree(
    UPhysicsAsset* PhysicsAsset,
    const FReferenceSkeleton& RefSkeleton,
    int32 BoneIndex)
{
    if (!PhysicsAsset || BoneIndex < 0 || BoneIndex >= RefSkeleton.GetNumBones())
    {
        return;
    }

    const FReferenceBone& Bone = RefSkeleton.Bones[BoneIndex];
    const FName BoneName(Bone.Name);
    const int32 BodyIndex = PhysicsAsset->FindBodySetupIndexByBoneName(BoneName);
    const int32 ConstraintIndex = FindConstraintToNearestParentBodyIndex(PhysicsAsset, RefSkeleton, BoneIndex);
    if (bShowOnlyBonesWithBodies && !BoneSubtreeHasPhysicsBody(PhysicsAsset, RefSkeleton, BoneIndex))
    {
        return;
    }

    const bool bHasSkeletonChildren = HasVisiblePhysicsBoneChild(
        PhysicsAsset,
        RefSkeleton,
        BoneIndex,
        bShowOnlyBonesWithBodies);
    const bool bHasBody = BodyIndex >= 0;
    const bool bSelected =
        SelectedTreeBoneIndex == BoneIndex ||
        (BodyIndex >= 0 && SelectedBodyIndex == BodyIndex && SelectedConstraintIndex < 0) ||
        (ConstraintIndex >= 0 && SelectedConstraintIndex == ConstraintIndex);

    ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!bHasSkeletonChildren)
    {
        Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (bSelected)
    {
        Flags |= ImGuiTreeNodeFlags_Selected;
    }

    const FString Label = MakeBoneTreeLabel(PhysicsAsset, RefSkeleton, BoneIndex);
    ImGui::PushID(BoneIndex);
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    const bool bOpen = ImGui::TreeNodeEx("##PhysicsBone", Flags, "%s", Label.c_str());

    if (bHasBody)
    {
        const ImVec2 ItemMin = ImGui::GetItemRectMin();
        const ImVec2 ItemMax = ImGui::GetItemRectMax();
        constexpr float Radius = 3.5f;
        const ImVec2 Center(
            ItemMax.x - Radius - 6.0f,
            (ItemMin.y + ItemMax.y) * 0.5f);

        ImGui::GetWindowDrawList()->AddCircleFilled(
            Center,
            Radius,
            IM_COL32(80, 210, 190, 255));

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Body");
        }
    }

    if (bSelected && bPendingScrollSelectedTreeBoneIntoView)
    {
        ImGui::SetScrollHereY(0.5f);
        bPendingScrollSelectedTreeBoneIntoView = false;
    }

    if (ImGui::IsItemClicked())
    {
        SelectBoneInPhysicsTree(PhysicsAsset, RefSkeleton, BoneIndex);
    }

    RenderPhysicsBoneContextMenu(PhysicsAsset, RefSkeleton, BoneIndex);

    if (bOpen && !bHasSkeletonChildren)
    {
        ImGui::PopID();
        return;
    }

    if (bOpen)
    {
        for (int32 ChildIndex = BoneIndex + 1; ChildIndex < RefSkeleton.GetNumBones(); ++ChildIndex)
        {
            if (RefSkeleton.Bones[ChildIndex].ParentIndex == BoneIndex &&
                (!bShowOnlyBonesWithBodies || BoneSubtreeHasPhysicsBody(PhysicsAsset, RefSkeleton, ChildIndex)))
            {
                RenderPhysicsBoneTree(PhysicsAsset, RefSkeleton, ChildIndex);
            }
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void FPhysicsAssetEditorWidget::RenderPhysicsBoneContextMenu(
    UPhysicsAsset* PhysicsAsset,
    const FReferenceSkeleton& RefSkeleton,
    int32 BoneIndex)
{
    if (!PhysicsAsset || BoneIndex < 0 || BoneIndex >= RefSkeleton.GetNumBones())
    {
        return;
    }

    if (!ImGui::BeginPopupContextItem("##PhysicsBoneContext"))
    {
        return;
    }

    const FReferenceBone& Bone = RefSkeleton.Bones[BoneIndex];
    const FName BoneName(Bone.Name);
    const int32 BodyIndex = PhysicsAsset->FindBodySetupIndexByBoneName(BoneName);
    const int32 ParentBodyBoneIndex = FindNearestParentBodyBoneIndex(PhysicsAsset, RefSkeleton, BoneIndex);
    const FName ParentBodyBoneName = ParentBodyBoneIndex >= 0 ? FName(RefSkeleton.Bones[ParentBodyBoneIndex].Name) : FName::None;
    const int32 ConstraintIndex = ParentBodyBoneIndex >= 0
        ? PhysicsAsset->FindConstraintSetupIndex(ParentBodyBoneName, BoneName)
        : -1;

    ImGui::TextDisabled("Bone: %s", Bone.Name.c_str());
    ImGui::Separator();

    if (BodyIndex >= 0)
    {
        if (ImGui::MenuItem("Select Body"))
        {
            SelectedTreeBoneIndex = BoneIndex;
            SelectedBodyIndex = BodyIndex;
            SelectedShapeIndex = PhysicsAsset->GetBodySetups()[BodyIndex].Shapes.empty() ? -1 : 0;
            SelectedConstraintIndex = -1;
        }

        if (ImGui::MenuItem("Remove Body"))
        {
            PhysicsAsset->RemoveBodySetupByIndex(BodyIndex);
            SelectedTreeBoneIndex = BoneIndex;
            SelectedBodyIndex = -1;
            SelectedShapeIndex = -1;
            SelectedConstraintIndex = -1;
            MarkPhysicsAssetDirty();
        }
    }
    else
    {
        if (ImGui::MenuItem("Add Body to Bone"))
        {
            AddDefaultBodyForBone(PhysicsAsset, BoneName);
            SelectedTreeBoneIndex = BoneIndex;
        }
    }

    ImGui::Separator();
    const bool bCanCreateConstraint = BodyIndex >= 0 && ParentBodyBoneIndex >= 0 && ConstraintIndex < 0;
    if (!bCanCreateConstraint) ImGui::BeginDisabled();
    if (ImGui::MenuItem("Create Constraint to Parent Body"))
    {
        AddDefaultConstraintForBones(PhysicsAsset, ParentBodyBoneName, BoneName);
        SelectedTreeBoneIndex = BoneIndex;
    }
    if (!bCanCreateConstraint) ImGui::EndDisabled();

    if (ConstraintIndex >= 0)
    {
        if (ImGui::MenuItem("Remove Parent Constraint"))
        {
            PhysicsAsset->RemoveConstraintSetupByIndex(ConstraintIndex);
            SelectedConstraintIndex = -1;
            SelectedBodyIndex = BodyIndex;
            SelectedShapeIndex = BodyIndex >= 0 && !PhysicsAsset->GetBodySetups()[BodyIndex].Shapes.empty() ? 0 : -1;
            SelectedTreeBoneIndex = BoneIndex;
            MarkPhysicsAssetDirty();
        }
    }
    else if (ParentBodyBoneIndex >= 0)
    {
        ImGui::TextDisabled("Parent Body: %s", ParentBodyBoneName.ToString().c_str());
    }
    else
    {
        ImGui::TextDisabled("No parent body found.");
    }

    ImGui::EndPopup();
}

void FPhysicsAssetEditorWidget::RenderSelectedBoneActionPanel(
    UPhysicsAsset* PhysicsAsset,
    const FReferenceSkeleton& RefSkeleton)
{
    if (!PhysicsAsset)
    {
        return;
    }

    const bool bHasSelectedBone = SelectedTreeBoneIndex >= 0 && SelectedTreeBoneIndex < RefSkeleton.GetNumBones();
    FName BoneName = FName::None;
    FString BoneLabel = "<None>";
    int32 BodyIndex = -1;
    int32 ParentBodyBoneIndex = -1;
    FName ParentBodyBoneName = FName::None;
    int32 ConstraintIndex = -1;

    if (bHasSelectedBone)
    {
        const FReferenceBone& Bone = RefSkeleton.Bones[SelectedTreeBoneIndex];
        BoneName = FName(Bone.Name);
        BoneLabel = Bone.Name;
        BodyIndex = PhysicsAsset->FindBodySetupIndexByBoneName(BoneName);
        ParentBodyBoneIndex = FindNearestParentBodyBoneIndex(PhysicsAsset, RefSkeleton, SelectedTreeBoneIndex);
        ParentBodyBoneName = ParentBodyBoneIndex >= 0 ? FName(RefSkeleton.Bones[ParentBodyBoneIndex].Name) : FName::None;
        ConstraintIndex = ParentBodyBoneIndex >= 0
            ? PhysicsAsset->FindConstraintSetupIndex(ParentBodyBoneName, BoneName)
            : -1;
    }

    const bool bCanAddBody = bHasSelectedBone && BodyIndex < 0;
    if (!bCanAddBody) ImGui::BeginDisabled();
    const float ButtonWidth = (std::max)(80.0f, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
    if (ImGui::Button("+ Body", ImVec2(ButtonWidth, 0.0f)))
    {
        AddDefaultBodyForBone(PhysicsAsset, BoneName);
    }
    if (!bCanAddBody) ImGui::EndDisabled();

    ImGui::SameLine();
    const bool bCanCreateConstraint = bHasSelectedBone && BodyIndex >= 0 && ParentBodyBoneIndex >= 0 && ConstraintIndex < 0;
    if (!bCanCreateConstraint) ImGui::BeginDisabled();
    if (ImGui::Button("+ Constraint To Parent", ImVec2(ButtonWidth, 0.0f)))
    {
        AddDefaultConstraintForBones(PhysicsAsset, ParentBodyBoneName, BoneName);
    }
    if (!bCanCreateConstraint) ImGui::EndDisabled();
}

void FPhysicsAssetEditorWidget::RenderUnboundPhysicsSetups(UPhysicsAsset* PhysicsAsset, const FReferenceSkeleton& RefSkeleton)
{
    if (!PhysicsAsset)
    {
        return;
    }

    bool bHasUnbound = false;
    for (const FPhysicsAssetBodySetup& Body : PhysicsAsset->GetBodySetups())
    {
        if (!IsBoneInReferenceSkeleton(RefSkeleton, Body.BoneName))
        {
            bHasUnbound = true;
            break;
        }
    }
    if (!bHasUnbound)
    {
        for (const FPhysicsAssetConstraintSetup& Constraint : PhysicsAsset->GetConstraintSetups())
        {
            if (!IsBoneInReferenceSkeleton(RefSkeleton, Constraint.ParentBoneName) ||
                !IsBoneInReferenceSkeleton(RefSkeleton, Constraint.ChildBoneName))
            {
                bHasUnbound = true;
                break;
            }
        }
    }

    if (!bHasUnbound)
    {
        return;
    }

    ImGui::Separator();
    if (!ImGui::TreeNodeEx("Unbound / Invalid Setups", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }

    const TArray<FPhysicsAssetBodySetup>& Bodies = PhysicsAsset->GetBodySetups();
    for (int32 Index = 0; Index < static_cast<int32>(Bodies.size()); ++Index)
    {
        if (!IsBoneInReferenceSkeleton(RefSkeleton, Bodies[Index].BoneName))
        {
            const FString Label = BodyLabel(Bodies[Index], Index);
            if (ImGui::Selectable(Label.c_str(), SelectedBodyIndex == Index && SelectedConstraintIndex < 0))
            {
                SelectedBodyIndex = Index;
                SelectedShapeIndex = Bodies[Index].Shapes.empty() ? -1 : 0;
                SelectedConstraintIndex = -1;
                SelectedTreeBoneIndex = -1;
            }
        }
    }

    const TArray<FPhysicsAssetConstraintSetup>& Constraints = PhysicsAsset->GetConstraintSetups();
    for (int32 Index = 0; Index < static_cast<int32>(Constraints.size()); ++Index)
    {
        if (!IsBoneInReferenceSkeleton(RefSkeleton, Constraints[Index].ParentBoneName) ||
            !IsBoneInReferenceSkeleton(RefSkeleton, Constraints[Index].ChildBoneName))
        {
            const FString Label = ConstraintLabel(Constraints[Index], Index, PhysicsAsset->GetConstraintSetupEditorState(Index));
            if (ImGui::Selectable(Label.c_str(), SelectedConstraintIndex == Index))
            {
                SelectedConstraintIndex = Index;
                SelectedBodyIndex = -1;
                SelectedShapeIndex = -1;
                SelectedTreeBoneIndex = -1;
            }
        }
    }

    ImGui::TreePop();
}

void FPhysicsAssetEditorWidget::RenderBodyListContent(UPhysicsAsset* PhysicsAsset)
{
    if (!PhysicsAsset)
    {
        return;
    }

    const TArray<FPhysicsAssetBodySetup>& Bodies = PhysicsAsset->GetBodySetups();
    for (int32 i = 0; i < static_cast<int32>(Bodies.size()); ++i)
    {
        const FString Label = BodyLabel(Bodies[i], i);
        if (ImGui::Selectable(Label.c_str(), SelectedBodyIndex == i && SelectedConstraintIndex < 0))
        {
            SelectedBodyIndex = i;
            SelectedShapeIndex = Bodies[i].Shapes.empty() ? -1 : 0;
            SelectedConstraintIndex = -1;
            SelectedTreeBoneIndex = -1;
        }
    }
    if (Bodies.empty()) ImGui::TextDisabled("No bodies. Click Add Body to start authoring.");
}

void FPhysicsAssetEditorWidget::RenderBodyList(UPhysicsAsset* PhysicsAsset)
{
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("Bodies");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    ImGui::BeginChild("PhysicsAssetBodyTree", ImVec2(0.0f, 150.0f), true);
    RenderBodyListContent(PhysicsAsset);
    ImGui::EndChild();

    //if (ImGui::Button("Add Body##BodyList")) AddDefaultBody(PhysicsAsset);
    //ImGui::SameLine();
    //const bool bCanRemove = SelectedBodyIndex >= 0 && SelectedBodyIndex < static_cast<int32>(Bodies.size());
    //if (!bCanRemove) ImGui::BeginDisabled();
    //if (ImGui::Button("Remove Body"))
    //{
    //    PhysicsAsset->RemoveBodySetupByIndex(SelectedBodyIndex);
    //    SelectedBodyIndex = -1;
    //    SelectedShapeIndex = -1;
    //    MarkPhysicsAssetDirty();
    //}
    //if (!bCanRemove) ImGui::EndDisabled();
}

void FPhysicsAssetEditorWidget::RenderConstraintList(UPhysicsAsset* PhysicsAsset)
{
    ImGui::TextUnformatted("Constraints");
    ImGui::BeginChild("##PhysicsAssetConstraintList", ImVec2(0.0f, 185.0f), true);
    const TArray<FPhysicsAssetConstraintSetup>& Constraints = PhysicsAsset->GetConstraintSetups();
    for (int32 i = 0; i < static_cast<int32>(Constraints.size()); ++i)
    {
        const FString Label = ConstraintLabel(Constraints[i], i, PhysicsAsset->GetConstraintSetupEditorState(i));
        if (ImGui::Selectable(Label.c_str(), SelectedConstraintIndex == i))
        {
            SelectedConstraintIndex = i;
            SelectedBodyIndex = -1;
            SelectedShapeIndex = -1;
            SelectedTreeBoneIndex = -1;
        }
    }
    if (Constraints.empty()) ImGui::TextDisabled("No constraints. Click Add Constraint to start authoring.");
    ImGui::EndChild();

    if (ImGui::Button("Add Constraint##ConstraintList")) AddDefaultConstraint(PhysicsAsset);
    ImGui::SameLine();
    const bool bCanRemove = SelectedConstraintIndex >= 0 && SelectedConstraintIndex < static_cast<int32>(Constraints.size());
    if (!bCanRemove) ImGui::BeginDisabled();
    if (ImGui::Button("Remove Constraint"))
    {
        PhysicsAsset->RemoveConstraintSetupByIndex(SelectedConstraintIndex);
        SelectedConstraintIndex = -1;
        MarkPhysicsAssetDirty();
    }
    if (!bCanRemove) ImGui::EndDisabled();
}


void FPhysicsAssetEditorWidget::InitializeConstraintGraphEditor()
{
    if (ConstraintGraphContext)
    {
        return;
    }

    ed::Config Config;
    ConstraintGraphContext = ed::CreateEditor(&Config);
    bConstraintGraphLayoutDirty = true;
    ConstraintGraphTopologyHash = 0;
}

void FPhysicsAssetEditorWidget::DestroyConstraintGraphEditor()
{
    if (ConstraintGraphContext)
    {
        ed::DestroyEditor(ConstraintGraphContext);
        ConstraintGraphContext = nullptr;
    }
}

void FPhysicsAssetEditorWidget::RenderConstraintGraphPanel(UPhysicsAsset* PhysicsAsset)
{
	ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("Body-Constraint Graph");
	ImGui::SetWindowFontScale(1.0f);
	ImGui::SameLine();

    bool bRequestResetView = false;
    bool bRequestRearrange = false;
	if (ImGui::Button("Reset View"))
	{
        bRequestResetView = true;
	}
    ImGui::SameLine();
	if (ImGui::Button("Rearrange##ConstraintGraph"))
	{
        bConstraintGraphLayoutDirty = true;
        bRequestRearrange = true;
	}
    
	
	if (!PhysicsAsset)
    {
        ImGui::TextDisabled("No PhysicsAsset selected.");
        return;
    }

    const TArray<FPhysicsAssetBodySetup>& Bodies = PhysicsAsset->GetBodySetups();
    const TArray<FPhysicsAssetConstraintSetup>& Constraints = PhysicsAsset->GetConstraintSetups();
    if (Bodies.empty())
    {
        ImGui::BeginChild("##PhysicsConstraintGraphEmpty", ImVec2(0.0f, 0.0f), true);
        ImGui::TextDisabled("No bodies. Add bodies from the Skeleton Physics Tree first.");
        ImGui::EndChild();
        return;
    }

    InitializeConstraintGraphEditor();
    if (!ConstraintGraphContext)
    {
        ImGui::TextDisabled("Constraint graph editor is unavailable.");
        return;
    }

    uint64 TopologyHash = 0;
    HashCombine(TopologyHash, static_cast<uint64>(Bodies.size()));
    HashCombine(TopologyHash, static_cast<uint64>(Constraints.size()));
    for (const FPhysicsAssetBodySetup& Body : Bodies)
    {
        HashName(TopologyHash, Body.BoneName);
        HashCombine(TopologyHash, static_cast<uint64>(Body.Shapes.size()));
    }
    for (const FPhysicsAssetConstraintSetup& Constraint : Constraints)
    {
        HashName(TopologyHash, Constraint.ParentBoneName);
        HashName(TopologyHash, Constraint.ChildBoneName);
    }

    if (TopologyHash != ConstraintGraphTopologyHash)
    {
        ConstraintGraphTopologyHash = TopologyHash;
        bConstraintGraphLayoutDirty = true;
    }

    ImGui::BeginChild("##PhysicsConstraintGraphHost", ImVec2(0.0f, 0.0f), false);
    ed::SetCurrentEditor(ConstraintGraphContext);
    const bool bRequestViewportFocusFromGraph =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_F, false);
    ed::EnableShortcuts(!bRequestViewportFocusFromGraph);
    ed::Begin("PhysicsConstraintGraph");

    if (bConstraintGraphLayoutDirty)
    {
        ArrangeConstraintGraphNodes(PhysicsAsset);
        bConstraintGraphLayoutDirty = false;
    }

    for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(Bodies.size()); ++BodyIndex)
    {
        const FPhysicsAssetBodySetup& Body = Bodies[BodyIndex];
        const bool bSelected = SelectedBodyIndex == BodyIndex && SelectedConstraintIndex < 0;
        const FString BoneLabel = Body.BoneName == FName::None ? FString("<None>") : Body.BoneName.ToString();
        const int32 ShapeCount = static_cast<int32>(Body.Shapes.size());

        ed::BeginNode(ToPhysicsNodeId(MakeBodyNodeId(BodyIndex)));
        ed::BeginPin(ToPhysicsPinId(MakeBodyInputPinId(BodyIndex)), ed::PinKind::Input);
        ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.80f, 1.0f), "o");
        ed::EndPin();
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextColored(
            bSelected ? ImVec4(1.0f, 0.86f, 0.18f, 1.0f) : ImVec4(0.64f, 0.88f, 0.62f, 1.0f),
            "Body");
        if (ImGui::IsItemClicked())
        {
            SelectBodySetupFromConstraintGraph(PhysicsAsset, BodyIndex);
        }
        ImGui::TextUnformatted(BoneLabel.c_str());
        if (ImGui::IsItemClicked())
        {
            SelectBodySetupFromConstraintGraph(PhysicsAsset, BodyIndex);
        }
        ImGui::TextDisabled("%d %s", ShapeCount, ShapeCount == 1 ? "shape" : "shapes");
        ImGui::EndGroup();
        ImGui::SameLine();
        ed::BeginPin(ToPhysicsPinId(MakeBodyOutputPinId(BodyIndex)), ed::PinKind::Output);
        ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.80f, 1.0f), "o");
        ed::EndPin();
        ed::EndNode();
    }

    for (int32 ConstraintIndex = 0; ConstraintIndex < static_cast<int32>(Constraints.size()); ++ConstraintIndex)
    {
        const FPhysicsAssetConstraintSetup& Constraint = Constraints[ConstraintIndex];
        const bool bSelected = SelectedConstraintIndex == ConstraintIndex;
        const FString ParentLabel = Constraint.ParentBoneName == FName::None ? FString("<None>") : Constraint.ParentBoneName.ToString();
        const FString ChildLabel = Constraint.ChildBoneName == FName::None ? FString("<None>") : Constraint.ChildBoneName.ToString();

        ed::BeginNode(ToPhysicsNodeId(MakeConstraintNodeId(ConstraintIndex)));
        ed::BeginPin(ToPhysicsPinId(MakeConstraintInputPinId(ConstraintIndex)), ed::PinKind::Input);
        ImGui::TextColored(ImVec4(0.90f, 0.86f, 0.55f, 1.0f), "o");
        ed::EndPin();
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextColored(
            bSelected ? ImVec4(1.0f, 0.70f, 0.42f, 1.0f) : ImVec4(0.90f, 0.86f, 0.55f, 1.0f),
            "Constraint");
        if (ImGui::IsItemClicked())
        {
            SelectConstraintSetupFromConstraintGraph(PhysicsAsset, ConstraintIndex);
        }
        ImGui::TextDisabled("%s -> %s", ParentLabel.c_str(), ChildLabel.c_str());
        if (ImGui::IsItemClicked())
        {
            SelectConstraintSetupFromConstraintGraph(PhysicsAsset, ConstraintIndex);
        }
        ImGui::EndGroup();
        ImGui::SameLine();
        ed::BeginPin(ToPhysicsPinId(MakeConstraintOutputPinId(ConstraintIndex)), ed::PinKind::Output);
        ImGui::TextColored(ImVec4(0.90f, 0.86f, 0.55f, 1.0f), "o");
        ed::EndPin();
        ed::EndNode();
    }

    for (int32 ConstraintIndex = 0; ConstraintIndex < static_cast<int32>(Constraints.size()); ++ConstraintIndex)
    {
        const FPhysicsAssetConstraintSetup& Constraint = Constraints[ConstraintIndex];
        const int32 ParentBodyIndex = PhysicsAsset->FindBodySetupIndexByBoneName(Constraint.ParentBoneName);
        const int32 ChildBodyIndex = PhysicsAsset->FindBodySetupIndexByBoneName(Constraint.ChildBoneName);
        if (ParentBodyIndex >= 0)
        {
            ed::Link(
                ToPhysicsLinkId(MakeParentLinkId(ConstraintIndex)),
                ToPhysicsPinId(MakeBodyOutputPinId(ParentBodyIndex)),
                ToPhysicsPinId(MakeConstraintInputPinId(ConstraintIndex)),
                ImColor(130, 210, 190));
        }
        if (ChildBodyIndex >= 0)
        {
            ed::Link(
                ToPhysicsLinkId(MakeChildLinkId(ConstraintIndex)),
                ToPhysicsPinId(MakeConstraintOutputPinId(ConstraintIndex)),
                ToPhysicsPinId(MakeBodyInputPinId(ChildBodyIndex)),
                ImColor(225, 215, 145));
        }
    }

    if (ed::BeginCreate())
    {
        ed::PinId StartPinId = 0;
        ed::PinId EndPinId = 0;
        if (ed::QueryNewLink(&StartPinId, &EndPinId) && StartPinId && EndPinId)
        {
            int32 ParentBodyIndex = -1;
            int32 ChildBodyIndex = -1;
            const uint32 StartPin = PhysicsPinIdToU32(StartPinId);
            const uint32 EndPin = PhysicsPinIdToU32(EndPinId);
            const bool bForward = DecodeBodyOutputPin(StartPin, ParentBodyIndex) && DecodeBodyInputPin(EndPin, ChildBodyIndex);
            const bool bReverse = DecodeBodyOutputPin(EndPin, ParentBodyIndex) && DecodeBodyInputPin(StartPin, ChildBodyIndex);

            const bool bCanCreate =
                (bForward || bReverse) &&
                ParentBodyIndex != ChildBodyIndex &&
                IsValidBodyIndex(PhysicsAsset, ParentBodyIndex) &&
                IsValidBodyIndex(PhysicsAsset, ChildBodyIndex) &&
                HasBoneName(Bodies[ParentBodyIndex].BoneName) &&
                HasBoneName(Bodies[ChildBodyIndex].BoneName) &&
                !PhysicsAsset->HasConstraintBetweenBones(Bodies[ParentBodyIndex].BoneName, Bodies[ChildBodyIndex].BoneName);

            if (bCanCreate)
            {
                if (ed::AcceptNewItem(ImVec4(0.55f, 0.90f, 0.80f, 1.0f), 2.0f))
                {
                    AddDefaultConstraintForBones(PhysicsAsset, Bodies[ParentBodyIndex].BoneName, Bodies[ChildBodyIndex].BoneName);
                    bConstraintGraphLayoutDirty = true;
                }
            }
            else
            {
                ed::RejectNewItem(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), 2.0f);
            }
        }
    }
    ed::EndCreate();

    TArray<int32> ConstraintsToDelete;
    TArray<int32> BodiesToDelete;
    if (ed::BeginDelete())
    {
        ed::LinkId DeletedLinkId = 0;
        while (ed::QueryDeletedLink(&DeletedLinkId))
        {
            int32 ConstraintIndex = -1;
            if (DecodeConstraintLink(PhysicsLinkIdToU32(DeletedLinkId), ConstraintIndex) &&
                IsValidConstraintIndex(PhysicsAsset, ConstraintIndex))
            {
                if (ed::AcceptDeletedItem())
                {
                    ConstraintsToDelete.push_back(ConstraintIndex);
                }
            }
        }

        ed::NodeId DeletedNodeId = 0;
        while (ed::QueryDeletedNode(&DeletedNodeId))
        {
            int32 BodyIndex = -1;
            int32 ConstraintIndex = -1;
            const uint32 DeletedNode = PhysicsNodeIdToU32(DeletedNodeId);
            if (DecodeBodyNode(DeletedNode, BodyIndex) && IsValidBodyIndex(PhysicsAsset, BodyIndex))
            {
                if (ed::AcceptDeletedItem())
                {
                    BodiesToDelete.push_back(BodyIndex);
                }
            }
            else if (DecodeConstraintNode(DeletedNode, ConstraintIndex) && IsValidConstraintIndex(PhysicsAsset, ConstraintIndex))
            {
                if (ed::AcceptDeletedItem())
                {
                    ConstraintsToDelete.push_back(ConstraintIndex);
                }
            }
        }
    }
    ed::EndDelete();

    if (!ConstraintsToDelete.empty())
    {
        std::sort(ConstraintsToDelete.begin(), ConstraintsToDelete.end(), std::greater<int32>());
        ConstraintsToDelete.erase(std::unique(ConstraintsToDelete.begin(), ConstraintsToDelete.end()), ConstraintsToDelete.end());
        for (int32 ConstraintIndex : ConstraintsToDelete)
        {
            if (PhysicsAsset->RemoveConstraintSetupByIndex(ConstraintIndex))
            {
                SelectedConstraintIndex = -1;
                MarkPhysicsAssetDirty();
            }
        }
        bConstraintGraphLayoutDirty = true;
    }

    if (!BodiesToDelete.empty())
    {
        std::sort(BodiesToDelete.begin(), BodiesToDelete.end(), std::greater<int32>());
        BodiesToDelete.erase(std::unique(BodiesToDelete.begin(), BodiesToDelete.end()), BodiesToDelete.end());
        for (int32 BodyIndex : BodiesToDelete)
        {
            if (PhysicsAsset->RemoveBodySetupByIndex(BodyIndex))
            {
                SelectedBodyIndex = -1;
                SelectedShapeIndex = -1;
                SelectedConstraintIndex = -1;
                MarkPhysicsAssetDirty();
            }
        }
        bConstraintGraphLayoutDirty = true;
    }

    if (bPendingConstraintGraphNavigateToSelection)
    {
        SelectCurrentConstraintGraphNode(PhysicsAsset, true);
        bPendingConstraintGraphNavigateToSelection = false;
    }
    else
    {
        SyncConstraintGraphSelectionFromNodeEditor(PhysicsAsset);
    }

    if (bRequestResetView || bRequestRearrange)
    {
        ed::NavigateToContent(0.25f);
    }

    ed::End();
    ed::EnableShortcuts(true);
    if (bRequestViewportFocusFromGraph)
    {
        bPendingConstraintGraphViewportFocusRequest = true;
    }
    ImGui::EndChild();
}

void FPhysicsAssetEditorWidget::RenderDetailsPanel(UPhysicsAsset* PhysicsAsset)
{
    if (SelectedBodyIndex >= 0 && SelectedBodyIndex < static_cast<int32>(PhysicsAsset->GetMutableBodySetups().size()))
    {
		ImGui::SetWindowFontScale(1.5f);
        ImGui::TextUnformatted("Body Details");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::Separator();
        RenderBodyDetails(PhysicsAsset, PhysicsAsset->GetMutableBodySetups()[SelectedBodyIndex]);
        return;
    }

    if (SelectedConstraintIndex >= 0 && SelectedConstraintIndex < static_cast<int32>(PhysicsAsset->GetMutableConstraintSetups().size()))
    {
		ImGui::SetWindowFontScale(1.5f);
		ImGui::TextUnformatted("Constraint Details");
		ImGui::SetWindowFontScale(1.0f);
		RenderConstraintDetails(PhysicsAsset, PhysicsAsset->GetMutableConstraintSetups()[SelectedConstraintIndex]);
        return;
    }

    if (SelectedTreeBoneIndex >= 0 && PreviewSkeletalMesh)
    {
        RenderSelectedBoneDetails(PhysicsAsset, PreviewSkeletalMesh);
        return;
    }

    ImGui::TextDisabled("Select a bone, body, or constraint to edit details.");
}

void FPhysicsAssetEditorWidget::RenderSelectedBoneDetails(UPhysicsAsset* PhysicsAsset, USkeletalMesh* PreviewMesh)
{
    USkeleton* Skeleton = PreviewMesh ? PreviewMesh->GetSkeleton() : nullptr;
    if (!PhysicsAsset || !Skeleton)
    {
        ImGui::TextDisabled("Select a bone, body, or constraint to edit details.");
        return;
    }

    const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
    if (SelectedTreeBoneIndex < 0 || SelectedTreeBoneIndex >= RefSkeleton.GetNumBones())
    {
        ImGui::TextDisabled("Selected bone is invalid.");
        return;
    }

    const FReferenceBone& Bone = RefSkeleton.Bones[SelectedTreeBoneIndex];
    const FName BoneName(Bone.Name);
    const int32 BodyIndex = PhysicsAsset->FindBodySetupIndexByBoneName(BoneName);
    const int32 ParentBodyBoneIndex = FindNearestParentBodyBoneIndex(PhysicsAsset, RefSkeleton, SelectedTreeBoneIndex);
    const FName ParentBodyBoneName = ParentBodyBoneIndex >= 0 ? FName(RefSkeleton.Bones[ParentBodyBoneIndex].Name) : FName::None;
    const int32 ConstraintIndex = ParentBodyBoneIndex >= 0
        ? PhysicsAsset->FindConstraintSetupIndex(ParentBodyBoneName, BoneName)
        : -1;

    ImGui::TextUnformatted("Bone Details");
    ImGui::Text("Name: %s", Bone.Name.c_str());
    ImGui::Text("Index: %d", SelectedTreeBoneIndex);
    ImGui::Text("Parent Index: %d", Bone.ParentIndex);
    ImGui::Separator();

    if (BodyIndex >= 0)
    {
        ImGui::TextDisabled("This bone already has a Physics Body.");
        if (ImGui::Button("Select Body", ImVec2(140.0f, 0.0f)))
        {
            SelectedBodyIndex = BodyIndex;
            SelectedShapeIndex = PhysicsAsset->GetBodySetups()[BodyIndex].Shapes.empty() ? -1 : 0;
            SelectedConstraintIndex = -1;
        }
    }
    else
    {
        if (ImGui::Button("Add Body to Bone", ImVec2(180.0f, 0.0f)))
        {
            AddDefaultBodyForBone(PhysicsAsset, BoneName);
        }
    }

    ImGui::Spacing();
    if (ParentBodyBoneIndex >= 0)
    {
        ImGui::TextDisabled("Nearest Parent Body: %s", ParentBodyBoneName.ToString().c_str());
        if (ConstraintIndex < 0)
        {
            const bool bCanCreateConstraint = BodyIndex >= 0;
            if (!bCanCreateConstraint) ImGui::BeginDisabled();
            if (ImGui::Button("Create Constraint to Parent Body", ImVec2(240.0f, 0.0f)))
            {
                AddDefaultConstraintForBones(PhysicsAsset, ParentBodyBoneName, BoneName);
            }
            if (!bCanCreateConstraint) ImGui::EndDisabled();
            if (!bCanCreateConstraint)
            {
                ImGui::TextDisabled("Add a Body to this bone first.");
            }
        }
    }
    else
    {
        ImGui::TextDisabled("No parent body found. This bone can act as a root body.");
    }
}

void FPhysicsAssetEditorWidget::RenderBodyDetails(UPhysicsAsset* PhysicsAsset, FPhysicsAssetBodySetup& Body)
{
    bool bChanged = false;
    const int32 BoundBoneIndex = FindPreviewBoneIndexByName(Body.BoneName);
    ImGui::Text("Bone Name: %s", Body.BoneName == FName::None ? "<None>" : Body.BoneName.ToString().c_str());
    ImGui::TextDisabled("Bone Index: %d", BoundBoneIndex);
    if (BoundBoneIndex >= 0)
    {
        if (ImGui::Button("Show Bound Bone in Tree", ImVec2(190.0f, 0.0f)))
        {
            SelectedTreeBoneIndex = BoundBoneIndex;
            bPhysicsTreePanelShowsBodies = false;
            bPendingScrollSelectedTreeBoneIntoView = true;
        }
    }
    //ImGui::TextDisabled("Bodies should be bound from the Skeleton Physics Tree, not typed manually.");
    //if (PreviewSkeletalMesh && PreviewSkeletalMesh->GetSkeleton() &&
    //    SelectedTreeBoneIndex >= 0 &&
    //    SelectedTreeBoneIndex < PreviewSkeletalMesh->GetSkeleton()->GetReferenceSkeleton().GetNumBones())
    //{
    //    const FReferenceSkeleton& RefSkeleton = PreviewSkeletalMesh->GetSkeleton()->GetReferenceSkeleton();
    //    const FName SelectedBoneName(RefSkeleton.Bones[SelectedTreeBoneIndex].Name);
    //    const bool bCanBindToSelectedBone =
    //        SelectedBoneName != Body.BoneName &&
    //        !PhysicsAsset->HasBodySetupForBone(SelectedBoneName);
    //    if (!bCanBindToSelectedBone) ImGui::BeginDisabled();
    //    if (ImGui::Button("Bind Body to Selected Bone", ImVec2(-1.0f, 0.0f)))
    //    {
    //        Body.BoneName = SelectedBoneName;
    //        bChanged = true;
    //    }
    //    if (!bCanBindToSelectedBone) ImGui::EndDisabled();
    //}
    //if (SelectedShapeIndex >= 0 && SelectedShapeIndex < static_cast<int32>(Body.Shapes.size()))
    //{
    //    ImGui::TextDisabled("Viewport gizmo targets the selected shape. Clear/remove the selected shape to edit the Body Local Frame with the gizmo.");
    //}
    //else
    //{
    //    ImGui::TextDisabled("Viewport gizmo targets the Body Local Frame.");
    //}

    bChanged |= EditTransform("Body Local Frame", Body.BodyLocalFrame);
    bChanged |= DragMinFloat("Mass", Body.Mass, 0.05f, 0.001f);
    bChanged |= DragVec3("Center Of Mass Offset", Body.CenterOfMassLocalOffset, 0.1f);
    bChanged |= DragMinFloat("Linear Damping", Body.LinearDamping, 0.01f, 0.0f);
    bChanged |= DragMinFloat("Angular Damping", Body.AngularDamping, 0.01f, 0.0f);
    bChanged |= DragMinFloat("Max Angular Velocity", Body.MaxAngularVelocity, 0.1f, 0.0f);
    bChanged |= InputMinInt("Position Solver Iterations", Body.PositionSolverIterationCount, 1);
    bChanged |= InputMinInt("Velocity Solver Iterations", Body.VelocitySolverIterationCount, 1);
    bChanged |= ImGui::Checkbox("Enable CCD", &Body.bEnableCCD);
    bChanged |= ImGui::Checkbox("Enable Gravity", &Body.bEnableGravity);

	if (ImGui::TreeNodeEx("Locks", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::BeginTable("##BodyLocks", 3, ImGuiTableFlags_SizingStretchSame))
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			bChanged |= ImGui::Checkbox("Lock Linear X", &Body.bLockLinearX);
			ImGui::TableSetColumnIndex(1);
			bChanged |= ImGui::Checkbox("Lock Linear Y", &Body.bLockLinearY);
			ImGui::TableSetColumnIndex(2);
			bChanged |= ImGui::Checkbox("Lock Linear Z", &Body.bLockLinearZ);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			bChanged |= ImGui::Checkbox("Lock Angular X", &Body.bLockAngularX);
			ImGui::TableSetColumnIndex(1);
			bChanged |= ImGui::Checkbox("Lock Angular Y", &Body.bLockAngularY);
			ImGui::TableSetColumnIndex(2);
			bChanged |= ImGui::Checkbox("Lock Angular Z", &Body.bLockAngularZ);

			ImGui::EndTable();
		}
		ImGui::TreePop();
	}

    ImGui::Separator();
	ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("Shapes");
	ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    
	if (ImGui::Button("Add Shape"))
	{
		AddDefaultShape(Body);
		bChanged = true;
	}
	ImGui::SameLine();
	const bool bCanRemoveShape = SelectedShapeIndex >= 0 && SelectedShapeIndex < static_cast<int32>(Body.Shapes.size());
	if (!bCanRemoveShape) ImGui::BeginDisabled();
	if (ImGui::Button("Remove Shape"))
	{
		Body.Shapes.erase(Body.Shapes.begin() + SelectedShapeIndex);
		SelectedShapeIndex = Body.Shapes.empty() ? -1 : (std::min)(SelectedShapeIndex, static_cast<int32>(Body.Shapes.size()) - 1);
		bChanged = true;
	}
	if (!bCanRemoveShape) ImGui::EndDisabled();

	for (int32 i = 0; i < static_cast<int32>(Body.Shapes.size()); ++i)
    {
        char Label[96] = {};
        std::snprintf(Label, sizeof(Label), "%d: %s", i, ShapeTypeText(Body.Shapes[i].Type));
        if (ImGui::Selectable(Label, SelectedShapeIndex == i)) SelectedShapeIndex = i;
    }

    if (SelectedShapeIndex >= 0 && SelectedShapeIndex < static_cast<int32>(Body.Shapes.size()))
    {
        ImGui::Separator();
        RenderShapeDetails(Body.Shapes[SelectedShapeIndex]);
    }

    if (bChanged)
    {
        PhysicsAsset->UpdateBodySetup(SelectedBodyIndex, Body);
        MarkPhysicsAssetDirty();
    }
}

void FPhysicsAssetEditorWidget::RenderShapeDetails(FPhysicsAssetShapeSetup& Shape)
{
    bool bChanged = false;
    bChanged |= ComboShapeType("Shape Type", Shape.Type);
    bChanged |= EditTransform("Shape Local Transform", Shape.LocalTransform);
    switch (Shape.Type)
    {
    case EPhysicsAssetShapeType::Box:
        bChanged |= DragVec3("Box Half Extent", Shape.BoxHalfExtent, ShapeSizeDragSpeed);
        Shape.BoxHalfExtent.X = (std::max)(Shape.BoxHalfExtent.X, 0.001f);
        Shape.BoxHalfExtent.Y = (std::max)(Shape.BoxHalfExtent.Y, 0.001f);
        Shape.BoxHalfExtent.Z = (std::max)(Shape.BoxHalfExtent.Z, 0.001f);
        break;
    case EPhysicsAssetShapeType::Sphere:
        bChanged |= DragMinFloat("Sphere Radius", Shape.SphereRadius, ShapeSizeDragSpeed, 0.001f);
        break;
    case EPhysicsAssetShapeType::Capsule:
        bChanged |= DragMinFloat("Capsule Radius", Shape.CapsuleRadius, ShapeSizeDragSpeed, 0.001f);
        bChanged |= DragMinFloat("Capsule Half Height", Shape.CapsuleHalfHeight, ShapeSizeDragSpeed, 0.001f);
        break;
    default:
        break;
    }
    if (bChanged) MarkPhysicsAssetDirty();
}

void FPhysicsAssetEditorWidget::RenderConstraintDetails(UPhysicsAsset* PhysicsAsset, FPhysicsAssetConstraintSetup& Constraint)
{
    bool bChanged = false;
    ImGui::Text("Parent Bone: %s", Constraint.ParentBoneName == FName::None ? "<None>" : Constraint.ParentBoneName.ToString().c_str());
    ImGui::Text("Child Bone: %s", Constraint.ChildBoneName == FName::None ? "<None>" : Constraint.ChildBoneName.ToString().c_str());
    ImGui::TextDisabled("Constraint endpoints are created from the bone tree or graph pins.");
    ImGui::Separator();

    ImGui::TextUnformatted("Viewport Gizmo Target");
    int GizmoFrame = (SelectedConstraintGizmoFrame == EPhysicsAssetConstraintFrameTarget::Parent) ? 0 : 1;
    if (ImGui::RadioButton("Parent Frame", &GizmoFrame, 0))
    {
        SelectedConstraintGizmoFrame = EPhysicsAssetConstraintFrameTarget::Parent;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Child Frame", &GizmoFrame, 1))
    {
        SelectedConstraintGizmoFrame = EPhysicsAssetConstraintFrameTarget::Child;
    }

    ImGui::Separator();
    bChanged |= EditTransform("Parent Local Frame", Constraint.ParentLocalFrame);
    bChanged |= EditTransform("Child Local Frame", Constraint.ChildLocalFrame);

    if (ImGui::TreeNodeEx("Linear Motion", ImGuiTreeNodeFlags_DefaultOpen))
    {
        bChanged |= ComboMotion("Linear X", Constraint.Limits.LinearX);
        bChanged |= ComboMotion("Linear Y", Constraint.Limits.LinearY);
        bChanged |= ComboMotion("Linear Z", Constraint.Limits.LinearZ);
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Angular Motion", ImGuiTreeNodeFlags_DefaultOpen))
    {
        bChanged |= ComboMotion("Twist", Constraint.Limits.Twist);
        bChanged |= ComboMotion("Swing 1", Constraint.Limits.Swing1);
        bChanged |= ComboMotion("Swing 2", Constraint.Limits.Swing2);
        bChanged |= ImGui::DragFloat("Twist Min", &Constraint.Limits.TwistLimitMinDegrees, 0.1f);
        bChanged |= ImGui::DragFloat("Twist Max", &Constraint.Limits.TwistLimitMaxDegrees, 0.1f);
        bChanged |= DragMinFloat("Swing 1 Limit", Constraint.Limits.Swing1LimitDegrees, 0.1f, 0.0f);
        bChanged |= DragMinFloat("Swing 2 Limit", Constraint.Limits.Swing2LimitDegrees, 0.1f, 0.0f);
        ImGui::TextDisabled("Viewport limit debug: Twist rotates around frame X. Swing 1/2 are drawn on the frame Y/Z planes.");
        ImGui::TreePop();
    }

    bChanged |= ImGui::Checkbox("Enable Projection", &Constraint.Limits.bEnableProjection);
    bChanged |= ImGui::Checkbox("Disable Collision Between Bodies", &Constraint.bDisableCollisionBetweenBodies);

    if (Constraint.Limits.TwistLimitMinDegrees > Constraint.Limits.TwistLimitMaxDegrees)
    {
        std::swap(Constraint.Limits.TwistLimitMinDegrees, Constraint.Limits.TwistLimitMaxDegrees);
        bChanged = true;
    }

    if (bChanged)
    {
        PhysicsAsset->UpdateConstraintSetup(SelectedConstraintIndex, Constraint);
        MarkPhysicsAssetDirty();
    }
}

void FPhysicsAssetEditorWidget::RenderValidationPanel()
{
    ImGui::TextUnformatted("Validation");
    if (!bValidationRan)
    {
        ImGui::TextDisabled("Click Validate to check skeleton, bodies, and constraints.");
        return;
    }

    if (!ValidationMeshPath.empty()) ImGui::TextDisabled("Mesh: %s", ValidationMeshPath.c_str());
    if (ValidationIssues.empty())
    {
        ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.45f, 1.0f), "No validation issues.");
        return;
    }

    ImGui::BeginChild("##PhysicsAssetValidation", ImVec2(0.0f, 150.0f), true);
    for (const FPhysicsAssetValidationIssue& Issue : ValidationIssues)
    {
        const bool bWarning = Issue.Severity == FPhysicsAssetValidationIssue::ESeverity::Warning;
        ImGui::TextColored(bWarning ? ImVec4(1.0f, 0.78f, 0.32f, 1.0f) : ImVec4(1.0f, 0.42f, 0.42f, 1.0f), "%s: %s", bWarning ? "Warning" : "Error", Issue.Message.c_str());
        if (Issue.BodyIndex >= 0 || Issue.ConstraintIndex >= 0)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Select"))
            {
                if (Issue.BodyIndex >= 0)
                {
                    SelectedBodyIndex = Issue.BodyIndex;
                    SelectedConstraintIndex = -1;
                }
                else
                {
                    SelectedConstraintIndex = Issue.ConstraintIndex;
                    SelectedBodyIndex = -1;
                    SelectedShapeIndex = -1;
                }
            }
        }
    }
    ImGui::EndChild();
}

void FPhysicsAssetEditorWidget::SelectBoneInPhysicsTree(
    UPhysicsAsset* PhysicsAsset,
    const FReferenceSkeleton& RefSkeleton,
    int32 BoneIndex)
{
    if (!PhysicsAsset || BoneIndex < 0 || BoneIndex >= RefSkeleton.GetNumBones())
    {
        return;
    }

    const FName BoneName(RefSkeleton.Bones[BoneIndex].Name);
    const int32 BodyIndex = PhysicsAsset->FindBodySetupIndexByBoneName(BoneName);
    if (IsValidBodyIndex(PhysicsAsset, BodyIndex))
    {
        SelectBodySetup(PhysicsAsset, BodyIndex, BoneIndex);
        return;
    }

    SelectedTreeBoneIndex = BoneIndex;
    SelectedBodyIndex = -1;
    SelectedShapeIndex = -1;
    SelectedConstraintIndex = -1;
}

void FPhysicsAssetEditorWidget::SelectBodySetup(UPhysicsAsset* PhysicsAsset, int32 BodyIndex, int32 TreeBoneIndex)
{
    if (!IsValidBodyIndex(PhysicsAsset, BodyIndex))
    {
        return;
    }

    SelectedBodyIndex = BodyIndex;
    SelectedShapeIndex = PhysicsAsset->GetBodySetups()[BodyIndex].Shapes.empty() ? -1 : 0;
    SelectedConstraintIndex = -1;
    SelectedTreeBoneIndex = TreeBoneIndex >= 0 ? TreeBoneIndex : FindPreviewBoneIndexByName(PhysicsAsset->GetBodySetups()[BodyIndex].BoneName);
}

void FPhysicsAssetEditorWidget::SelectConstraintSetup(UPhysicsAsset* PhysicsAsset, int32 ConstraintIndex, int32 TreeBoneIndex)
{
    if (!IsValidConstraintIndex(PhysicsAsset, ConstraintIndex))
    {
        return;
    }

    SelectedConstraintIndex = ConstraintIndex;
    SelectedBodyIndex = -1;
    SelectedShapeIndex = -1;
    SelectedConstraintGizmoFrame = EPhysicsAssetConstraintFrameTarget::Child;
    if (TreeBoneIndex >= 0)
    {
        SelectedTreeBoneIndex = TreeBoneIndex;
    }
    else
    {
        SelectedTreeBoneIndex = FindPreviewBoneIndexByName(PhysicsAsset->GetConstraintSetups()[ConstraintIndex].ChildBoneName);
    }
}

void FPhysicsAssetEditorWidget::SelectBodySetupFromConstraintGraph(UPhysicsAsset* PhysicsAsset, int32 BodyIndex)
{
    SelectBodySetup(PhysicsAsset, BodyIndex, -1);
    SelectCurrentConstraintGraphNode(PhysicsAsset, false);
}

void FPhysicsAssetEditorWidget::SelectConstraintSetupFromConstraintGraph(UPhysicsAsset* PhysicsAsset, int32 ConstraintIndex)
{
    SelectConstraintSetup(PhysicsAsset, ConstraintIndex, -1);
    SelectCurrentConstraintGraphNode(PhysicsAsset, false);
}

void FPhysicsAssetEditorWidget::SyncConstraintGraphSelectionFromNodeEditor(UPhysicsAsset* PhysicsAsset)
{
    if (!PhysicsAsset || !ConstraintGraphContext || !ed::HasSelectionChanged())
    {
        return;
    }

    ed::NodeId SelectedNodes[1] = {};
    if (ed::GetSelectedNodes(SelectedNodes, 1) <= 0)
    {
        return;
    }

    const uint32 SelectedNode = PhysicsNodeIdToU32(SelectedNodes[0]);
    int32 BodyIndex = -1;
    int32 ConstraintIndex = -1;
    if (DecodeBodyNode(SelectedNode, BodyIndex) && IsValidBodyIndex(PhysicsAsset, BodyIndex))
    {
        if (SelectedBodyIndex != BodyIndex || SelectedConstraintIndex >= 0)
        {
            SelectBodySetup(PhysicsAsset, BodyIndex, -1);
            SelectCurrentConstraintGraphNode(PhysicsAsset, false);
        }
    }
    else if (DecodeConstraintNode(SelectedNode, ConstraintIndex) && IsValidConstraintIndex(PhysicsAsset, ConstraintIndex))
    {
        if (SelectedConstraintIndex != ConstraintIndex)
        {
            SelectConstraintSetup(PhysicsAsset, ConstraintIndex, -1);
            SelectCurrentConstraintGraphNode(PhysicsAsset, false);
        }
    }
}

void FPhysicsAssetEditorWidget::SelectCurrentConstraintGraphNode(UPhysicsAsset* PhysicsAsset, bool bNavigateToSelection)
{
    if (!PhysicsAsset || !ConstraintGraphContext)
    {
        return;
    }

    ed::NodeId NodeId = 0;
    if (SelectedConstraintIndex >= 0 && IsValidConstraintIndex(PhysicsAsset, SelectedConstraintIndex))
    {
        NodeId = ToPhysicsNodeId(MakeConstraintNodeId(SelectedConstraintIndex));
    }
    else if (SelectedBodyIndex >= 0 && IsValidBodyIndex(PhysicsAsset, SelectedBodyIndex))
    {
        NodeId = ToPhysicsNodeId(MakeBodyNodeId(SelectedBodyIndex));
    }

    if (!NodeId)
    {
        return;
    }

    ed::SelectNode(NodeId, false);
    if (bNavigateToSelection)
    {
        ed::NavigateToSelection(false, 0.20f);
    }
}

int32 FPhysicsAssetEditorWidget::FindPreviewBoneIndexByName(const FName& BoneName) const
{
    if (!HasBoneName(BoneName) || !PreviewSkeletalMesh || !PreviewSkeletalMesh->GetSkeleton())
    {
        return -1;
    }

    return PreviewSkeletalMesh->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(BoneName.ToString());
}

void FPhysicsAssetEditorWidget::AddConstraintToSelectedParentBody(UPhysicsAsset* PhysicsAsset)
{
    if (!PhysicsAsset || !PreviewSkeletalMesh || !PreviewSkeletalMesh->GetSkeleton())
    {
        return;
    }

    const FReferenceSkeleton& RefSkeleton = PreviewSkeletalMesh->GetSkeleton()->GetReferenceSkeleton();
    int32 BoneIndex = SelectedTreeBoneIndex;
    if ((BoneIndex < 0 || BoneIndex >= RefSkeleton.GetNumBones()) && IsValidBodyIndex(PhysicsAsset, SelectedBodyIndex))
    {
        BoneIndex = FindPreviewBoneIndexByName(PhysicsAsset->GetBodySetups()[SelectedBodyIndex].BoneName);
    }

    if (BoneIndex < 0 || BoneIndex >= RefSkeleton.GetNumBones())
    {
        return;
    }

    const FName ChildBoneName(RefSkeleton.Bones[BoneIndex].Name);
    const int32 ChildBodyIndex = PhysicsAsset->FindBodySetupIndexByBoneName(ChildBoneName);
    const int32 ParentBodyBoneIndex = FindNearestParentBodyBoneIndex(PhysicsAsset, RefSkeleton, BoneIndex);
    if (ChildBodyIndex < 0 || ParentBodyBoneIndex < 0)
    {
        return;
    }

    const FName ParentBoneName(RefSkeleton.Bones[ParentBodyBoneIndex].Name);
    AddDefaultConstraintForBones(PhysicsAsset, ParentBoneName, ChildBoneName);
    SelectedTreeBoneIndex = BoneIndex;
}

bool FPhysicsAssetEditorWidget::RegenerateBodies(UPhysicsAsset* PhysicsAsset, USkeletalMesh* PreviewMesh)
{
    FPhysicsAssetAutoBodyGeneratorOptions Options;
    Options.Method = (bRegenerateUsePCAAnalysis && !bRegenerateUseBoneAxis)
        ? EPhysicsAssetAutoBodyMethod::PCAAnalysis
        : EPhysicsAssetAutoBodyMethod::BoneAxis;
    switch (RegeneratePrimitiveTypeIndex)
    {
    case 1:
        Options.PrimitiveType = EPhysicsAssetAutoBodyPrimitiveType::Box;
        break;
    case 2:
        Options.PrimitiveType = EPhysicsAssetAutoBodyPrimitiveType::Sphere;
        break;
    case 0:
    default:
        Options.PrimitiveType = EPhysicsAssetAutoBodyPrimitiveType::Capsule;
        break;
    }
    Options.bCreateConstraints = bRegenerateCreateConstraints;
    Options.bDisableCollisionBetweenConstrainedBodies = bRegenerateDisableConstrainedBodyCollision;
    Options.bReplaceExisting = bRegenerateReplaceExisting;
    Options.bSkipHelperBones = bRegenerateSkipHelperBones;
    Options.bAllowBoneAxisFallback = bRegenerateAllowBoneAxisFallback;
    Options.bMergeSmallBones = bRegenerateMergeSmallBones;
    Options.MinInfluenceWeight = RegenerateMinInfluenceWeight;
    Options.MinWeightedVertices = (std::max)(RegenerateMinWeightedVertices, 1);
    Options.MinBoneSize = (std::max)(RegenerateMinBoneSize, 0.0f);
    Options.MinWeldSize = (std::max)(RegenerateMinWeldSize, 0.0f);

    TArray<FMatrix> CurrentBoneGlobalMatrices;
    const TArray<FMatrix>* OverrideBoneGlobalMatrices = nullptr;
    if (PreviewSkeletalMeshComponent && PreviewSkeletalMeshComponent->GetSkeletalMesh() == PreviewMesh)
    {
        PreviewSkeletalMeshComponent->GetCurrentBoneGlobalMatrices(CurrentBoneGlobalMatrices);
        if (!CurrentBoneGlobalMatrices.empty())
        {
            OverrideBoneGlobalMatrices = &CurrentBoneGlobalMatrices;
        }
    }

    FPhysicsAssetAutoBodyGeneratorResult Result;
    if (!FPhysicsAssetAutoBodyGenerator::Regenerate(PhysicsAsset, PreviewMesh, Options, &Result, OverrideBoneGlobalMatrices))
    {
        return false;
    }

    if (Options.bReplaceExisting)
    {
        SelectedBodyIndex = -1;
        SelectedShapeIndex = -1;
        SelectedConstraintIndex = -1;
    }

    if (Result.FirstGeneratedBodyIndex >= 0)
    {
        SelectBodySetup(
            PhysicsAsset,
            Result.FirstGeneratedBodyIndex,
            FindPreviewBoneIndexByName(Result.FirstGeneratedBoneName));
    }
    else
    {
        ClampSelection(PhysicsAsset);
    }

    if (Result.bAssetChanged)
    {
        MarkPhysicsAssetDirty();
    }
    return true;
}

void FPhysicsAssetEditorWidget::AddDefaultBody(UPhysicsAsset* PhysicsAsset)
{
    AddDefaultBodyForBone(PhysicsAsset, FName::None);
}

void FPhysicsAssetEditorWidget::AddDefaultBodyForBone(UPhysicsAsset* PhysicsAsset, const FName& BoneName)
{
    if (!PhysicsAsset) return;

    if (HasBoneName(BoneName))
    {
        const int32 ExistingBodyIndex = PhysicsAsset->FindBodySetupIndexByBoneName(BoneName);
        if (ExistingBodyIndex >= 0)
        {
            SelectBodySetup(PhysicsAsset, ExistingBodyIndex, FindPreviewBoneIndexByName(BoneName));
            return;
        }
    }

    FPhysicsAssetBodySetup Body;
    Body.BoneName = BoneName;

    FPhysicsAssetShapeSetup Shape;
    Shape.Type = EPhysicsAssetShapeType::Capsule;
    Shape.BoxHalfExtent = FVector(0.8f, 0.8f, 0.8f);
    Shape.SphereRadius = 0.8f;
    Shape.CapsuleRadius = 0.4f;
    Shape.CapsuleHalfHeight = 1.2f;
    Body.Shapes.push_back(Shape);

    const int32 NewIndex = PhysicsAsset->AddBodySetup(Body);
    if (NewIndex >= 0)
    {
        SelectBodySetup(PhysicsAsset, NewIndex, FindPreviewBoneIndexByName(BoneName));
        MarkPhysicsAssetDirty();
    }
}

void FPhysicsAssetEditorWidget::AddDefaultShape(FPhysicsAssetBodySetup& Body)
{
    FPhysicsAssetShapeSetup Shape;
    Shape.Type = EPhysicsAssetShapeType::Box;
    Shape.BoxHalfExtent = FVector(0.8f, 0.8f, 0.8f);
    Shape.SphereRadius = 0.8f;
    Shape.CapsuleRadius = 0.4f;
    Shape.CapsuleHalfHeight = 1.2f;
    Body.Shapes.push_back(Shape);
    SelectedShapeIndex = static_cast<int32>(Body.Shapes.size()) - 1;
}

void FPhysicsAssetEditorWidget::AddDefaultConstraint(UPhysicsAsset* PhysicsAsset)
{
    if (!PhysicsAsset) return;

    const int32 ConstraintCountBefore = static_cast<int32>(PhysicsAsset->GetConstraintSetups().size());
    const int32 PreviousConstraintIndex = SelectedConstraintIndex;
    AddConstraintToSelectedParentBody(PhysicsAsset);
    if (static_cast<int32>(PhysicsAsset->GetConstraintSetups().size()) != ConstraintCountBefore ||
        SelectedConstraintIndex != PreviousConstraintIndex)
    {
        return;
    }

    // Fallback for assets edited without a preview skeleton: connect the first two named bodies.
    const TArray<FPhysicsAssetBodySetup>& Bodies = PhysicsAsset->GetBodySetups();
    if (Bodies.size() >= 2 && HasBoneName(Bodies[0].BoneName) && HasBoneName(Bodies[1].BoneName))
    {
        AddDefaultConstraintForBones(PhysicsAsset, Bodies[0].BoneName, Bodies[1].BoneName);
    }
}

void FPhysicsAssetEditorWidget::AddDefaultConstraintForBones(
    UPhysicsAsset* PhysicsAsset,
    const FName& ParentBoneName,
    const FName& ChildBoneName)
{
    if (!PhysicsAsset) return;
    if (!HasBoneName(ParentBoneName) || !HasBoneName(ChildBoneName) || ParentBoneName == ChildBoneName)
    {
        return;
    }
    if (!PhysicsAsset->HasBodySetupForBone(ParentBoneName) || !PhysicsAsset->HasBodySetupForBone(ChildBoneName))
    {
        return;
    }

    const int32 ExistingConstraintIndex = PhysicsAsset->FindConstraintSetupIndex(ParentBoneName, ChildBoneName);
    if (ExistingConstraintIndex >= 0)
    {
        SelectConstraintSetup(PhysicsAsset, ExistingConstraintIndex, FindPreviewBoneIndexByName(ChildBoneName));
        return;
    }

    FPhysicsAssetConstraintSetup Constraint;
    Constraint.ParentBoneName = ParentBoneName;
    Constraint.ChildBoneName = ChildBoneName;

    const int32 NewIndex = PhysicsAsset->AddConstraintSetup(Constraint);
    if (NewIndex >= 0)
    {
        SelectConstraintSetup(PhysicsAsset, NewIndex, FindPreviewBoneIndexByName(ChildBoneName));
        MarkPhysicsAssetDirty();
    }
}

void FPhysicsAssetEditorWidget::RunValidation(UPhysicsAsset* PhysicsAsset)
{
    ValidationIssues.clear();
    ValidationMeshPath.clear();
    bValidationRan = true;

    if (!PhysicsAsset)
    {
        FPhysicsAssetValidationIssue Issue;
        Issue.Message = "PhysicsAsset is missing.";
        ValidationIssues.push_back(Issue);
        return;
    }

    USkeletalMesh* ValidationMesh = nullptr;
    ID3D11Device* Device = EditorEngine ? EditorEngine->GetRenderer().GetFD3DDevice().GetDevice() : nullptr;
    const TArray<FAssetListItem> Meshes = FAssetRegistry::ListMeshesForSkeleton(PhysicsAsset->GetSkeletonBinding(), true);
    for (const FAssetListItem& Item : Meshes)
    {
        if (USkeletalMesh* Mesh = FMeshManager::LoadSkeletalMesh(Item.FullPath, Device))
        {
            ValidationMesh = Mesh;
            ValidationMeshPath = Item.FullPath;
            break;
        }
    }

    if (!ValidationMesh)
    {
        FPhysicsAssetValidationIssue Issue;
        Issue.Message = "No compatible skeletal mesh was found for this PhysicsAsset skeleton binding.";
        Issue.Severity = FPhysicsAssetValidationIssue::ESeverity::Warning;
        ValidationIssues.push_back(Issue);
        return;
    }

    FPhysicsAssetValidation::ValidateAll(ValidationMesh, PhysicsAsset, ValidationIssues);
}

void FPhysicsAssetEditorWidget::RenderPreviewDebug(
    UPhysicsAsset* PhysicsAsset,
    USkeletalMesh* PreviewMesh,
    UWorld* PreviewWorld,
    USkeletalMeshComponent* PreviewComponent,
    const FShowFlags* PreviewShowFlags)
{
    PreviewSkeletalMesh = PreviewMesh;
    PreviewSkeletalMeshComponent = PreviewComponent;
    PreviewSimulationWorld = PreviewWorld;

    if (!PhysicsAsset || !PreviewWorld || !PreviewComponent)
    {
        return;
    }

    ClampSelection(PhysicsAsset);

    const bool bShowBodies = bShowPreviewBodies && (!PreviewShowFlags || PreviewShowFlags->bPhysicsAssetShapes);
    const bool bShowConstraints = bShowPreviewConstraints && (!PreviewShowFlags || PreviewShowFlags->bPhysicsAssetConstraints);
    const bool bShowBodySkeleton = PreviewShowFlags
        ? PreviewShowFlags->bPhysicsAssetBodySkeleton
        : bShowPreviewBodySkeleton;

    FPhysicsAssetPreviewPoseCache PoseCache;
    const bool bHasPoseCache = PoseCache.Initialize(PreviewComponent, PhysicsAsset);

    if (bShowBodies)
    {
        RenderBodyDebug(PhysicsAsset, PreviewComponent, PreviewWorld, bHasPoseCache ? &PoseCache : nullptr);
    }

    if (bShowConstraints)
    {
        RenderConstraintDebug(PhysicsAsset, PreviewComponent, PreviewWorld, bHasPoseCache ? &PoseCache : nullptr);
    }

    if (bShowBodySkeleton)
    {
        RenderBodySkeletonDebug(PhysicsAsset, PreviewComponent, PreviewWorld, bHasPoseCache ? &PoseCache : nullptr);
    }
}

void FPhysicsAssetEditorWidget::RenderPhysicsPreview(
    UPhysicsAsset* PhysicsAsset,
    USkeletalMesh* PreviewMesh,
    UWorld* PreviewWorld,
    USkeletalMeshComponent* PreviewComponent,
    UPhysicsAssetPreviewComponent* SolidPreviewComponent,
    ID3D11Device* Device,
    const FShowFlags* PreviewShowFlags)
{
    PreviewSkeletalMesh = PreviewMesh;
    PreviewSkeletalMeshComponent = PreviewComponent;
    PreviewSimulationWorld = PreviewWorld;

    if (!PhysicsAsset || !PreviewWorld || !PreviewComponent)
    {
        if (SolidPreviewComponent)
        {
            SolidPreviewComponent->ClearPreview(Device);
        }
        return;
    }

    ClampSelection(PhysicsAsset);

    const bool bShowBodies = bShowPreviewBodies && (!PreviewShowFlags || PreviewShowFlags->bPhysicsAssetShapes);
    const bool bShowConstraints = bShowPreviewConstraints && (!PreviewShowFlags || PreviewShowFlags->bPhysicsAssetConstraints);
    const bool bShowBodySkeleton = PreviewShowFlags
        ? PreviewShowFlags->bPhysicsAssetBodySkeleton
        : bShowPreviewBodySkeleton;

    if (SolidPreviewComponent)
    {
        SolidPreviewComponent->UpdatePreview(
            PhysicsAsset,
            PreviewComponent,
            SelectedBodyIndex,
            SelectedShapeIndex,
            SelectedConstraintIndex,
            bShowBodies,
            bShowConstraints,
            bShowConstraintLimitAngles,
            bShowConstraints && bShowConstraintLimitAngles && bShowConstraintLimitSurfaces,
            bShowOnlySelectedConstraintLimitAngles,
            Device);
    }

    FPhysicsAssetPreviewPoseCache PoseCache;
    const bool bHasPoseCache = PoseCache.Initialize(PreviewComponent, PhysicsAsset);

    if (bShowBodies)
    {
        RenderBodyDebug(PhysicsAsset, PreviewComponent, PreviewWorld, bHasPoseCache ? &PoseCache : nullptr);
    }

    if (bShowConstraints)
    {
        RenderConstraintDebug(PhysicsAsset, PreviewComponent, PreviewWorld, bHasPoseCache ? &PoseCache : nullptr);
    }

    if (bShowBodySkeleton)
    {
        RenderBodySkeletonDebug(PhysicsAsset, PreviewComponent, PreviewWorld, bHasPoseCache ? &PoseCache : nullptr);
    }
}

void FPhysicsAssetEditorWidget::RenderViewportDebugOptions(FShowFlags* PreviewShowFlags)
{
    ImGui::Separator();
    ImGui::TextUnformatted("Physics Preview");

    bool* bShowBodies = PreviewShowFlags ? &PreviewShowFlags->bPhysicsAssetShapes : &bShowPreviewBodies;
    bool* bShowConstraints = PreviewShowFlags ? &PreviewShowFlags->bPhysicsAssetConstraints : &bShowPreviewConstraints;
    bool* bShowBodySkeleton = PreviewShowFlags ? &PreviewShowFlags->bPhysicsAssetBodySkeleton : &bShowPreviewBodySkeleton;

    ImGui::Checkbox("Physics Asset Shapes", bShowBodies);
    ImGui::Checkbox("Physics Asset Constraints", bShowConstraints);
    ImGui::Checkbox("Physics Asset Body Skeleton", bShowBodySkeleton);
    if (!*bShowConstraints) ImGui::BeginDisabled();
    ImGui::Checkbox("Constraint Limits", &bShowConstraintLimitAngles);
    if (!bShowConstraintLimitAngles) ImGui::BeginDisabled();
    ImGui::Checkbox("Constraint Limit Fill", &bShowConstraintLimitSurfaces);
    ImGui::Checkbox("Selected Constraint Limits Only", &bShowOnlySelectedConstraintLimitAngles);
    if (!bShowConstraintLimitAngles) ImGui::EndDisabled();
    if (!*bShowConstraints) ImGui::EndDisabled();
    ImGui::TextDisabled(bEditorSimulationActive
        ? (bEditorSimulationPaused ? "Simulation: paused" : "Simulation: running")
        : "Simulation: stopped");
}

void FPhysicsAssetEditorWidget::SetEditorPreviewContext(UWorld* PreviewWorld, USkeletalMeshComponent* PreviewComponent)
{
    PreviewSimulationWorld = PreviewWorld;
    PreviewSkeletalMeshComponent = PreviewComponent;
}

void FPhysicsAssetEditorWidget::TickEditorSimulation(
    UPhysicsAsset* PhysicsAsset,
    USkeletalMesh* PreviewMesh,
    UWorld* PreviewWorld,
    USkeletalMeshComponent* PreviewComponent,
    float DeltaTime,
    const FRay* ViewportMouseRay,
    bool bMouseLeftPressed,
    bool bMouseLeftHeld,
    bool bMouseLeftReleased)
{
    PreviewSkeletalMesh = PreviewMesh;
    SetEditorPreviewContext(PreviewWorld, PreviewComponent);

    if (!bEditorSimulationActive)
    {
        return;
    }

    if (!PhysicsAsset || !PreviewWorld || !PreviewComponent)
    {
        StopEditorSimulation(PreviewWorld, PreviewComponent, false);
        return;
    }

    if (bEditorSimulationRestartRequested)
    {
        StopEditorSimulation(PreviewWorld, PreviewComponent, true);
        StartEditorSimulation(PhysicsAsset, PreviewWorld, PreviewComponent);
        return;
    }

    TickEditorRagdollGrab(
        PreviewWorld,
        PreviewComponent,
        ViewportMouseRay,
        bMouseLeftPressed,
        bMouseLeftHeld,
        bMouseLeftReleased);

    if (!bEditorSimulationPaused && DeltaTime > 0.0f)
    {
        if (IPhysicsScene* PhysicsScene = PreviewWorld->GetPhysicsScene())
        {
            PhysicsScene->Tick(DeltaTime);
            PhysicsScene->DispatchPendingEvents();
        }
    }

    PreviewComponent->ApplyPhysicsAssetPose();
}

void FPhysicsAssetEditorWidget::StopEditorSimulation(
    UWorld* PreviewWorld,
    USkeletalMeshComponent* PreviewComponent,
    bool bResetPose)
{
    EndEditorRagdollGrab();

    USkeletalMeshComponent* Component = PreviewComponent ? PreviewComponent : PreviewSkeletalMeshComponent;
    if (Component)
    {
        Component->DisableRagdollPhysics();
    }

    if (PreviewWorld)
    {
        if (IPhysicsScene* PhysicsScene = PreviewWorld->GetPhysicsScene())
        {
            PhysicsScene->Tick(0.0f);
            PhysicsScene->DispatchPendingEvents();
        }
    }

    if (Component && bResetPose)
    {
        Component->ApplyBoneEditBasePose();
    }

    bEditorSimulationActive = false;
    bEditorSimulationPaused = false;
    bEditorSimulationRestartRequested = false;
}

bool FPhysicsAssetEditorWidget::StartEditorSimulation(
    UPhysicsAsset* PhysicsAsset,
    UWorld* PreviewWorld,
    USkeletalMeshComponent* PreviewComponent)
{
    if (!PhysicsAsset || !PreviewWorld || !PreviewComponent)
    {
        return false;
    }

    FPhysicsAssetSimulationOptions Options;
    Options.bNoGravity = bEditorSimulationNoGravity;
    Options.bSelectedOnly = bEditorSimulationSelectedOnly;
    Options.bForceQueryAndPhysicsCollision = true;
    Options.SelectedBoneName = GetSelectedSimulationRootBoneName(PhysicsAsset);
    if (Options.bSelectedOnly && Options.SelectedBoneName == FName::None)
    {
        return false;
    }

    StopEditorSimulation(PreviewWorld, PreviewComponent, false);
    PreviewComponent->SetPhysicsAssetOverride(PhysicsAsset);

    FPhysicsAssetInstance* Instance = PreviewComponent->GetOrCreatePhysicsAssetInstance();
    if (!Instance || !Instance->CreateBodiesAndConstraints(Options))
    {
        bEditorSimulationActive = false;
        return false;
    }

    PreviewComponent->BeginPhysicsAssetPosePreview(true);
    if (IPhysicsScene* PhysicsScene = PreviewWorld->GetPhysicsScene())
    {
        PhysicsScene->Tick(0.0f);
        PhysicsScene->DispatchPendingEvents();
    }
    PreviewComponent->ApplyPhysicsAssetPose();

    bEditorSimulationActive = true;
    bEditorSimulationPaused = false;
    bEditorSimulationRestartRequested = false;
    return true;
}

bool FPhysicsAssetEditorWidget::BeginEditorRagdollGrab(
    UPhysicsAsset* PhysicsAsset,
    UWorld* PreviewWorld,
    USkeletalMeshComponent* PreviewComponent,
    const FRay& MouseRay)
{
    EndEditorRagdollGrab();

    if (!bEditorSimulationActive || !PhysicsAsset || !PreviewWorld || !PreviewComponent)
    {
        return false;
    }

    IPhysicsScene* PhysicsScene = PreviewWorld->GetPhysicsScene();
    FPhysicsAssetInstance* Instance = PreviewComponent->GetPhysicsAssetInstance();
    if (!PhysicsScene || !Instance)
    {
        return false;
    }

    FPhysicsRaycastResult Hit;
    const uint32 ObjectMask = ObjectTypeBit(PreviewComponent->GetCollisionObjectType());
    if (!PhysicsScene->RaycastPhysicsByObjectTypes(
            MouseRay.Origin,
            MouseRay.Direction,
            EditorRagdollGrabMaxDistance,
            Hit,
            ObjectMask,
            nullptr))
    {
        return false;
    }

    if (!Hit.bBlockingHit ||
        Hit.HitComponentId != PreviewComponent->GetUUID() ||
        Hit.HitDomain != EPhysicsBodyDomain::Ragdoll ||
        !Hit.HitBody.IsValid())
    {
        return false;
    }

    FName GrabBoneName = Hit.HitBoneName;
    if (GrabBoneName == FName::None)
    {
        FVector BodyWorldLocation = FVector::ZeroVector;
        if (!Instance->FindNearestBodyToWorldLocation(Hit.Location, GrabBoneName, BodyWorldLocation))
        {
            return false;
        }
    }

    const FPhysicsBodyHandle GrabBody = Hit.HitBody.IsValid()
        ? Hit.HitBody
        : Instance->GetBodyHandleByBoneName(GrabBoneName);
    IPhysicsRuntime* Runtime = PhysicsScene->GetRuntime();
    if (!GrabBody.IsValid() || !Runtime)
    {
        return false;
    }

    const FTransform BodyWorld = Runtime->GetBodyTransform(GrabBody);
    EditorRagdollGrabBody = GrabBody;
    EditorRagdollGrabBoneName = GrabBoneName;
    EditorRagdollGrabLocalOffset = BodyWorld.Rotation.Inverse().RotateVector(Hit.Location - BodyWorld.Location);
    EditorRagdollGrabDistance = Hit.Distance > 0.0f
        ? Hit.Distance
        : (Hit.Location - MouseRay.Origin).Length();
    if (EditorRagdollGrabDistance <= 0.0f)
    {
        EditorRagdollGrabDistance = (BodyWorld.Location - MouseRay.Origin).Length();
    }

    bEditorRagdollGrabActive = true;
    return true;
}

void FPhysicsAssetEditorWidget::TickEditorRagdollGrab(
    UWorld* PreviewWorld,
    USkeletalMeshComponent* PreviewComponent,
    const FRay* MouseRay,
    bool bMouseLeftPressed,
    bool bMouseLeftHeld,
    bool bMouseLeftReleased)
{
    if (!bEditorSimulationActive || bEditorSimulationPaused || !PreviewWorld || !PreviewComponent)
    {
        EndEditorRagdollGrab();
        return;
    }

    UPhysicsAsset* PhysicsAsset = GetEditedPhysicsAsset();
    if (bMouseLeftPressed && MouseRay)
    {
        BeginEditorRagdollGrab(PhysicsAsset, PreviewWorld, PreviewComponent, *MouseRay);
    }

    if (bMouseLeftReleased || !bMouseLeftHeld || !MouseRay)
    {
        EndEditorRagdollGrab();
        return;
    }

    if (!bEditorRagdollGrabActive || !EditorRagdollGrabBody.IsValid())
    {
        return;
    }

    IPhysicsScene* PhysicsScene = PreviewWorld->GetPhysicsScene();
    IPhysicsRuntime* Runtime = PhysicsScene ? PhysicsScene->GetRuntime() : nullptr;
    FPhysicsAssetInstance* Instance = PreviewComponent->GetPhysicsAssetInstance();
    if (!Runtime || !Instance)
    {
        EndEditorRagdollGrab();
        return;
    }

    const FPhysicsBodyHandle GrabBody = EditorRagdollGrabBody;
    if (!GrabBody.IsValid())
    {
        EndEditorRagdollGrab();
        return;
    }

    const FTransform BodyWorld = Runtime->GetBodyTransform(GrabBody);
    const FVector CurrentGrabWorld = BodyWorld.Location + BodyWorld.Rotation.RotateVector(EditorRagdollGrabLocalOffset);
    const FVector TargetGrabWorld = MouseRay->Origin + MouseRay->Direction * EditorRagdollGrabDistance;
    const FVector Error = TargetGrabWorld - CurrentGrabWorld;

    FVector DesiredLinearVelocity = Error * EditorRagdollGrabFollowSpeed;
    const float DesiredSpeed = DesiredLinearVelocity.Length();
    if (DesiredSpeed > EditorRagdollGrabMaxLinearVelocity && DesiredSpeed > 0.0001f)
    {
        DesiredLinearVelocity = DesiredLinearVelocity * (EditorRagdollGrabMaxLinearVelocity / DesiredSpeed);
    }

    FVector AngularVelocity = Runtime->GetBodyAngularVelocity(GrabBody) * EditorRagdollGrabAngularDamping;
    Runtime->SetBodyVelocity(GrabBody, DesiredLinearVelocity, AngularVelocity);
}

void FPhysicsAssetEditorWidget::EndEditorRagdollGrab()
{
    bEditorRagdollGrabActive = false;
    EditorRagdollGrabBody = FPhysicsBodyHandle{};
    EditorRagdollGrabBoneName = FName::None;
    EditorRagdollGrabLocalOffset = FVector::ZeroVector;
    EditorRagdollGrabDistance = 0.0f;
}

void FPhysicsAssetEditorWidget::RequestEditorSimulationRestart()
{
    if (bEditorSimulationActive)
    {
        bEditorSimulationRestartRequested = true;
    }
}

FName FPhysicsAssetEditorWidget::GetSelectedSimulationRootBoneName(UPhysicsAsset* PhysicsAsset) const
{
    if (!PhysicsAsset || !IsValidBodyIndex(PhysicsAsset, SelectedBodyIndex))
    {
        return FName::None;
    }

    const TArray<FPhysicsAssetBodySetup>& Bodies = PhysicsAsset->GetBodySetups();
    return Bodies[SelectedBodyIndex].BoneName;
}

void FPhysicsAssetEditorWidget::RenderBodySkeletonDebug(
    UPhysicsAsset* PhysicsAsset,
    USkeletalMeshComponent* PreviewComponent,
    UWorld* PreviewWorld,
    const FPhysicsAssetPreviewPoseCache* PoseCache)
{
    if (!PhysicsAsset || !PreviewComponent || !PreviewWorld)
    {
        return;
    }

    USkeletalMesh* SkeletalMesh = PreviewComponent->GetSkeletalMesh();
    USkeleton* Skeleton = SkeletalMesh ? SkeletalMesh->GetSkeleton() : nullptr;
    if (!Skeleton)
    {
        return;
    }

    const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
    const TArray<FPhysicsAssetBodySetup>& Bodies = PhysicsAsset->GetBodySetups();
    for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(Bodies.size()); ++BodyIndex)
    {
        const FPhysicsAssetBodySetup& Body = Bodies[BodyIndex];
        const int32 BoneIndex = RefSkeleton.FindBoneIndex(Body.BoneName.ToString());
        if (BoneIndex < 0)
        {
            continue;
        }

        const int32 ParentBodyBoneIndex = FindNearestParentBodyBoneIndex(PhysicsAsset, RefSkeleton, BoneIndex);
        if (ParentBodyBoneIndex < 0)
        {
            continue;
        }

        const FName ParentBodyBoneName(RefSkeleton.Bones[ParentBodyBoneIndex].Name);
        const int32 ParentBodyIndex = PhysicsAsset->FindBodySetupIndexByBoneName(ParentBodyBoneName);
        if (!IsValidBodyIndex(PhysicsAsset, ParentBodyIndex))
        {
            continue;
        }

        FTransform ParentBodyWorld;
        FTransform ChildBodyWorld;
        const bool bHasParentBodyWorld = PoseCache
            ? PoseCache->ComputeBodyWorldTransform(ParentBodyIndex, ParentBodyWorld)
            : FPhysicsAssetPreviewUtils::ComputePreviewBodyWorldTransform(
                PreviewComponent,
                PhysicsAsset,
                ParentBodyIndex,
                ParentBodyWorld);
        const bool bHasChildBodyWorld = PoseCache
            ? PoseCache->ComputeBodyWorldTransform(BodyIndex, ChildBodyWorld)
            : FPhysicsAssetPreviewUtils::ComputePreviewBodyWorldTransform(
                PreviewComponent,
                PhysicsAsset,
                BodyIndex,
                ChildBodyWorld);
        if (!bHasParentBodyWorld || !bHasChildBodyWorld)
        {
            continue;
        }

        const bool bSelectedSegment =
            SelectedBodyIndex == BodyIndex ||
            SelectedBodyIndex == ParentBodyIndex ||
            (SelectedConstraintIndex >= 0 &&
                IsValidConstraintIndex(PhysicsAsset, SelectedConstraintIndex) &&
                PhysicsAsset->GetConstraintSetups()[SelectedConstraintIndex].ChildBoneName == Body.BoneName);
        const FColor Color = bSelectedSegment
            ? FColor(255, 230, 90, 220)
            : FColor(10, 10, 200, 135);

        DrawDebugLine(PreviewWorld, ParentBodyWorld.Location, ChildBodyWorld.Location, Color, 0.0f);
        DrawDebugPoint(PreviewWorld, ParentBodyWorld.Location, bSelectedSegment ? 0.10f : 0.055f, Color, 0.0f);
        DrawDebugPoint(PreviewWorld, ChildBodyWorld.Location, bSelectedSegment ? 0.10f : 0.055f, Color, 0.0f);
    }
}

void FPhysicsAssetEditorWidget::RenderBodyDebug(
    UPhysicsAsset* PhysicsAsset,
    USkeletalMeshComponent* PreviewComponent,
    UWorld* PreviewWorld,
    const FPhysicsAssetPreviewPoseCache* PoseCache)
{
    if (!PhysicsAsset || !PreviewComponent || !PreviewWorld)
    {
        return;
    }

    const TArray<FPhysicsAssetBodySetup>& Bodies = PhysicsAsset->GetBodySetups();
    for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(Bodies.size()); ++BodyIndex)
    {
        DrawBodySetupDebug(PhysicsAsset, PreviewComponent, PreviewWorld, BodyIndex, Bodies[BodyIndex], PoseCache);
    }
}

void FPhysicsAssetEditorWidget::RenderConstraintDebug(
    UPhysicsAsset* PhysicsAsset,
    USkeletalMeshComponent* PreviewComponent,
    UWorld* PreviewWorld,
    const FPhysicsAssetPreviewPoseCache* PoseCache)
{
    if (!PhysicsAsset || !PreviewComponent || !PreviewWorld)
    {
        return;
    }

    const TArray<FPhysicsAssetConstraintSetup>& Constraints = PhysicsAsset->GetConstraintSetups();
    for (int32 ConstraintIndex = 0; ConstraintIndex < static_cast<int32>(Constraints.size()); ++ConstraintIndex)
    {
        DrawConstraintSetupDebug(
            PhysicsAsset,
            PreviewComponent,
            PreviewWorld,
            ConstraintIndex,
            Constraints[ConstraintIndex],
            PoseCache);
    }
}

void FPhysicsAssetEditorWidget::DrawBodySetupDebug(
    UPhysicsAsset* PhysicsAsset,
    USkeletalMeshComponent* PreviewComponent,
    UWorld* PreviewWorld,
    int32 BodyIndex,
    const FPhysicsAssetBodySetup& BodySetup,
    const FPhysicsAssetPreviewPoseCache* PoseCache)
{
    if (!PhysicsAsset || !PreviewComponent || !PreviewWorld)
    {
        return;
    }

    FTransform BodyWorld;
    const bool bHasBodyWorld = PoseCache
        ? PoseCache->ComputeBodyWorldTransform(BodyIndex, BodyWorld)
        : FPhysicsAssetPreviewUtils::ComputePreviewBodyWorldTransform(
            PreviewComponent,
            PhysicsAsset,
            BodyIndex,
            BodyWorld);
    if (!bHasBodyWorld)
    {
        return;
    }

    const bool bSelectedBody = SelectedBodyIndex == BodyIndex && SelectedConstraintIndex < 0;
    for (int32 ShapeIndex = 0; ShapeIndex < static_cast<int32>(BodySetup.Shapes.size()); ++ShapeIndex)
    {
        const FPhysicsAssetShapeSetup& Shape = BodySetup.Shapes[ShapeIndex];
        const FTransform ShapeWorld = ComposePreviewDebugTransforms(BodyWorld, Shape.LocalTransform);
        const bool bSelectedShape = bSelectedBody && SelectedShapeIndex == ShapeIndex;
        const FColor Color = bSelectedShape
            ? FColor(255, 245, 120, 190)
            : (bSelectedBody ? FColor(255, 180, 60, 170) : FColor(90, 210, 255, 115));

        switch (Shape.Type)
        {
        case EPhysicsAssetShapeType::Box:
        {
            FVector HalfExtent = Shape.BoxHalfExtent;
            HalfExtent.X = (std::max)(HalfExtent.X, 0.001f);
            HalfExtent.Y = (std::max)(HalfExtent.Y, 0.001f);
            HalfExtent.Z = (std::max)(HalfExtent.Z, 0.001f);
            DrawDebugOrientedBox(
                PreviewWorld,
                ShapeWorld.Location,
                HalfExtent,
                ShapeWorld.Rotation.GetNormalized(),
                Color);
            break;
        }
        case EPhysicsAssetShapeType::Sphere:
        {
            const float Radius = (std::max)(Shape.SphereRadius, 0.001f);
            DrawDebugSphere(PreviewWorld, ShapeWorld.Location, Radius, 24, Color, 0.0f);
            break;
        }
        case EPhysicsAssetShapeType::Capsule:
        {
            const float Radius = (std::max)(Shape.CapsuleRadius, 0.001f);
            const float HalfHeight = (std::max)(Shape.CapsuleHalfHeight, Radius);
            DrawDebugCapsuleZAxis(
                PreviewWorld,
                ShapeWorld.Location,
                Radius,
                HalfHeight,
                ShapeWorld.Rotation.GetNormalized(),
                Color);
            break;
        }
        default:
            break;
        }
    }
}

void FPhysicsAssetEditorWidget::DrawConstraintSetupDebug(
    UPhysicsAsset* PhysicsAsset,
    USkeletalMeshComponent* PreviewComponent,
    UWorld* PreviewWorld,
    int32 ConstraintIndex,
    const FPhysicsAssetConstraintSetup& ConstraintSetup,
    const FPhysicsAssetPreviewPoseCache* PoseCache)
{
    if (!PhysicsAsset || !PreviewComponent || !PreviewWorld)
    {
        return;
    }

    FTransform ParentFrameWorld;
    FTransform ChildFrameWorld;
    const bool bHasConstraintFrames = PoseCache
        ? PoseCache->ComputeConstraintWorldFrames(ConstraintIndex, ParentFrameWorld, ChildFrameWorld)
        : FPhysicsAssetPreviewUtils::ComputePreviewConstraintWorldFrames(
            PreviewComponent,
            PhysicsAsset,
            ConstraintIndex,
            ParentFrameWorld,
            ChildFrameWorld);
    if (!bHasConstraintFrames)
    {
        return;
    }

    const bool bSelected = SelectedConstraintIndex == ConstraintIndex;
    const FColor Color = bSelected ? SelectedConstraintSwingColor(240u) : ConstraintLimitSwingRed(145u);
    DrawDebugLine(PreviewWorld, ParentFrameWorld.Location, ChildFrameWorld.Location, Color, 0.0f);
    DrawDebugPoint(PreviewWorld, ParentFrameWorld.Location, bSelected ? 0.12f : 0.05f, Color, 0.0f);
    DrawDebugPoint(PreviewWorld, ChildFrameWorld.Location, bSelected ? 0.12f : 0.05f, Color, 0.0f);

    if (bShowConstraintLimitAngles && (!bShowOnlySelectedConstraintLimitAngles || bSelected))
    {
        DrawConstraintAngularLimitDebug(
            PreviewWorld,
            ParentFrameWorld,
            ChildFrameWorld,
            ConstraintSetup.Limits,
            bSelected);
    }
}

void FPhysicsAssetEditorWidget::MarkPhysicsAssetDirty()
{
    MarkDirty();
    bConstraintGraphLayoutDirty = true;
    ConstraintGraphTopologyHash = 0;
    bValidationRan = false;
    ValidationIssues.clear();
    ValidationMeshPath.clear();
    RequestEditorSimulationRestart();
}

void FPhysicsAssetEditorWidget::ClampSelection(UPhysicsAsset* PhysicsAsset)
{
    if (!PhysicsAsset)
    {
        SelectedBodyIndex = -1;
        SelectedShapeIndex = -1;
        SelectedConstraintIndex = -1;
        SelectedTreeBoneIndex = -1;
        return;
    }

    if (PreviewSkeletalMesh && PreviewSkeletalMesh->GetSkeleton())
    {
        const int32 BoneCount = PreviewSkeletalMesh->GetSkeleton()->GetReferenceSkeleton().GetNumBones();
        if (SelectedTreeBoneIndex >= BoneCount) SelectedTreeBoneIndex = -1;
    }
    else
    {
        SelectedTreeBoneIndex = -1;
    }

    const int32 BodyCount = static_cast<int32>(PhysicsAsset->GetBodySetups().size());
    const int32 ConstraintCount = static_cast<int32>(PhysicsAsset->GetConstraintSetups().size());
    if (SelectedBodyIndex >= BodyCount) SelectedBodyIndex = BodyCount - 1;
    if (SelectedConstraintIndex >= ConstraintCount) SelectedConstraintIndex = ConstraintCount - 1;

    if (SelectedBodyIndex < 0)
    {
        SelectedShapeIndex = -1;
    }
    else
    {
        const int32 ShapeCount = static_cast<int32>(PhysicsAsset->GetBodySetups()[SelectedBodyIndex].Shapes.size());
        if (SelectedShapeIndex >= ShapeCount) SelectedShapeIndex = ShapeCount - 1;
        if (SelectedShapeIndex < 0 && ShapeCount > 0) SelectedShapeIndex = 0;
    }
}
