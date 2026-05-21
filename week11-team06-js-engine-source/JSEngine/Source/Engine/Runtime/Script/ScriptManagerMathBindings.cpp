#include "Runtime/Script/ScriptManager.h"

#include "Core/Logging/Log.h"
#include "Core/CollisionTypes.h"
#include "Geometry/Transform.h"
#include "Math/Vector.h"
#include "Object/Class.h"
#include "Object/Object.h"
#include "Object/Property.h"
#include "Object/ReflectionRegistry.h"
#include "Runtime/Script/ScriptUtils.h"
#include "ThirdParty/sol/sol.hpp"

namespace
{
    FString LuaGetObjectName(UObject& Object)
    {
        return Object.GetName();
    }

    const UClass* FindUClassByTypeName(const FString& TypeName)
    {
        if (TypeName.empty())
        {
            return nullptr;
        }

        if (const UClass* Class = FReflectionRegistry::Get().FindClass(TypeName))
        {
            return Class;
        }

        if (TypeName[0] != 'U' && TypeName[0] != 'A')
        {
            if (const UClass* Class = FReflectionRegistry::Get().FindClass(FString("U") + TypeName))
            {
                return Class;
            }

            if (const UClass* Class = FReflectionRegistry::Get().FindClass(FString("A") + TypeName))
            {
                return Class;
            }
        }

        return nullptr;
    }

    FString GetBestObjectTypeName(UObject& Object)
    {
        UClass* Class = Object.GetClass();
        const FTypeInfo* TypeInfo = Object.GetTypeInfo();

        if (!Class)
        {
            return TypeInfo && TypeInfo->name ? TypeInfo->name : "";
        }

        if (!TypeInfo || !TypeInfo->name)
        {
            return Class->GetName() ? Class->GetName() : "";
        }

        const FString TypeInfoName = TypeInfo->name;
        const FString ClassName = Class->GetName() ? Class->GetName() : "";
        if (ClassName == TypeInfoName)
        {
            return ClassName;
        }

        if (FReflectionRegistry::Get().FindClass(TypeInfoName))
        {
            return ClassName;
        }

        return TypeInfoName;
    }

    FString LuaGetObjectType(UObject& Object)
    {
        return GetBestObjectTypeName(Object);
    }

    bool LuaObjectIsA(UObject& Object, const FString& TypeName)
    {
        if (TypeName.empty())
        {
            return false;
        }

        if (const UClass* TargetClass = FindUClassByTypeName(TypeName))
        {
            if (Object.IsA(TargetClass))
            {
                return true;
            }
        }

        for (const FTypeInfo* TypeInfo = Object.GetTypeInfo(); TypeInfo; TypeInfo = TypeInfo->Parent)
        {
            const FString NativeName = TypeInfo->name;
            const FString LuaStyleName = (NativeName.size() > 1 && (NativeName[0] == 'A' || NativeName[0] == 'U'))
                ? NativeName.substr(1)
                : NativeName;

            if (TypeName == NativeName || TypeName == LuaStyleName)
            {
                return true;
            }
        }
        return false;
    }

    const FProperty* FindLuaProperty(UObject& Object, const FString& PropertyName, EPropertyFlags RequiredFlag)
    {
        UClass* Class = Object.GetClass();
        if (!Class)
        {
            return nullptr;
        }

        const FProperty* Property = Class->FindProperty(PropertyName.c_str());
        if (!Property || !HasPropertyFlag(Property->Flags, RequiredFlag))
        {
            return nullptr;
        }
        return Property;
    }

    template <typename AssetRefType>
    bool TryGetReflectedAssetPathForLua(UObject& Object, const FProperty& Property, FString& OutPath)
    {
        AssetRefType Value;
        if (!Property.GetPropertyValue_InContainer(&Object, Value))
        {
            return false;
        }

        OutPath = Value.Path;
        return true;
    }

    bool GetReflectedAssetPathForLua(UObject& Object, const FProperty& Property, FString& OutPath)
    {
        return TryGetReflectedAssetPathForLua<FStaticMeshAssetRef>(Object, Property, OutPath)
            || TryGetReflectedAssetPathForLua<FSkeletalMeshAssetRef>(Object, Property, OutPath)
            || TryGetReflectedAssetPathForLua<FTextureAssetRef>(Object, Property, OutPath)
            || TryGetReflectedAssetPathForLua<FMaterialAssetRef>(Object, Property, OutPath)
            || TryGetReflectedAssetPathForLua<FAnimationSequenceAssetRef>(Object, Property, OutPath)
            || TryGetReflectedAssetPathForLua<FAnimStateMachineAssetRef>(Object, Property, OutPath)
            || TryGetReflectedAssetPathForLua<FCurveAssetRef>(Object, Property, OutPath)
            || TryGetReflectedAssetPathForLua<FSceneAssetRef>(Object, Property, OutPath)
            || TryGetReflectedAssetPathForLua<FSoundAssetRef>(Object, Property, OutPath);
    }

    bool SetReflectedAssetPathFromLua(UObject& Object, const FProperty& Property, const FString& Path)
    {
        if (dynamic_cast<const FStaticMeshAssetProperty*>(&Property))
        {
            return Property.SetPropertyValue_InContainer(&Object, FStaticMeshAssetRef(Path));
        }
        if (dynamic_cast<const FSkeletalMeshAssetProperty*>(&Property))
        {
            return Property.SetPropertyValue_InContainer(&Object, FSkeletalMeshAssetRef(Path));
        }
        if (dynamic_cast<const FTextureAssetProperty*>(&Property))
        {
            return Property.SetPropertyValue_InContainer(&Object, FTextureAssetRef(Path));
        }
        if (dynamic_cast<const FMaterialAssetProperty*>(&Property))
        {
            return Property.SetPropertyValue_InContainer(&Object, FMaterialAssetRef(Path));
        }
        if (dynamic_cast<const FAnimationSequenceAssetProperty*>(&Property))
        {
            return Property.SetPropertyValue_InContainer(&Object, FAnimationSequenceAssetRef(Path));
        }
        if (dynamic_cast<const FAnimStateMachineAssetProperty*>(&Property))
        {
            return Property.SetPropertyValue_InContainer(&Object, FAnimStateMachineAssetRef(Path));
        }
        if (dynamic_cast<const FCurveAssetProperty*>(&Property))
        {
            return Property.SetPropertyValue_InContainer(&Object, FCurveAssetRef(Path));
        }
        if (dynamic_cast<const FSceneAssetProperty*>(&Property))
        {
            return Property.SetPropertyValue_InContainer(&Object, FSceneAssetRef(Path));
        }
        if (dynamic_cast<const FSoundAssetProperty*>(&Property))
        {
            return Property.SetPropertyValue_InContainer(&Object, FSoundAssetRef(Path));
        }

        return false;
    }

    sol::object LuaGetReflectedProperty(sol::this_state State, UObject& Object, const FString& PropertyName)
    {
        const FProperty* Property = FindLuaProperty(Object, PropertyName, EPropertyFlags::LuaRead);
        if (!Property)
        {
            return sol::make_object(State, sol::nil);
        }

        if (dynamic_cast<const FBoolProperty*>(Property))
        {
            bool Value = false;
            return Property->GetPropertyValue_InContainer(&Object, Value)
                ? sol::make_object(State, Value)
                : sol::make_object(State, sol::nil);
        }
        if (dynamic_cast<const FInt32Property*>(Property))
        {
            int32 Value = 0;
            return Property->GetPropertyValue_InContainer(&Object, Value)
                ? sol::make_object(State, Value)
                : sol::make_object(State, sol::nil);
        }
        if (dynamic_cast<const FFloatProperty*>(Property))
        {
            float Value = 0.0f;
            return Property->GetPropertyValue_InContainer(&Object, Value)
                ? sol::make_object(State, Value)
                : sol::make_object(State, sol::nil);
        }
        if (dynamic_cast<const FNameProperty*>(Property))
        {
            FName Value;
            return Property->GetPropertyValue_InContainer(&Object, Value)
                ? sol::make_object(State, Value)
                : sol::make_object(State, sol::nil);
        }
        if (dynamic_cast<const FStringProperty*>(Property))
        {
            FString Value;
            return Property->GetPropertyValue_InContainer(&Object, Value)
                ? sol::make_object(State, Value)
                : sol::make_object(State, sol::nil);
        }
        if (dynamic_cast<const FStaticMeshAssetProperty*>(Property)
            || dynamic_cast<const FSkeletalMeshAssetProperty*>(Property)
            || dynamic_cast<const FTextureAssetProperty*>(Property)
            || dynamic_cast<const FMaterialAssetProperty*>(Property)
            || dynamic_cast<const FAnimationSequenceAssetProperty*>(Property)
            || dynamic_cast<const FAnimStateMachineAssetProperty*>(Property)
            || dynamic_cast<const FCurveAssetProperty*>(Property)
            || dynamic_cast<const FSceneAssetProperty*>(Property)
            || dynamic_cast<const FSoundAssetProperty*>(Property))
        {
            FString Value;
            return GetReflectedAssetPathForLua(Object, *Property, Value)
                ? sol::make_object(State, Value)
                : sol::make_object(State, sol::nil);
        }
        if (dynamic_cast<const FObjectProperty*>(Property))
        {
            UObject* Value = nullptr;
            return Property->GetPropertyValue_InContainer(&Object, Value)
                ? sol::make_object(State, Value)
                : sol::make_object(State, sol::nil);
        }

        return sol::make_object(State, sol::nil);
    }

    bool LuaSetReflectedProperty(UObject& Object, const FString& PropertyName, sol::object Value)
    {
        const FProperty* Property = FindLuaProperty(Object, PropertyName, EPropertyFlags::LuaWrite);
        if (!Property || !Value.valid() || Value == sol::nil)
        {
            return false;
        }

        if (dynamic_cast<const FBoolProperty*>(Property))
        {
            return Value.is<bool>()
                && Property->SetPropertyValue_InContainer(&Object, Value.as<bool>());
        }
        if (dynamic_cast<const FInt32Property*>(Property))
        {
            return Value.get_type() == sol::type::number
                && Property->SetPropertyValue_InContainer(&Object, static_cast<int32>(Value.as<double>()));
        }
        if (dynamic_cast<const FFloatProperty*>(Property))
        {
            return Value.get_type() == sol::type::number
                && Property->SetPropertyValue_InContainer(&Object, static_cast<float>(Value.as<double>()));
        }
        if (dynamic_cast<const FNameProperty*>(Property))
        {
            if (Value.is<FName>())
            {
                return Property->SetPropertyValue_InContainer(&Object, Value.as<FName>());
            }
            if (Value.is<FString>())
            {
                return Property->SetPropertyValue_InContainer(&Object, FName(Value.as<FString>()));
            }
            return false;
        }
        if (dynamic_cast<const FStringProperty*>(Property))
        {
            return Value.is<FString>()
                && Property->SetPropertyValue_InContainer(&Object, Value.as<FString>());
        }
        if (dynamic_cast<const FStaticMeshAssetProperty*>(Property)
            || dynamic_cast<const FSkeletalMeshAssetProperty*>(Property)
            || dynamic_cast<const FTextureAssetProperty*>(Property)
            || dynamic_cast<const FMaterialAssetProperty*>(Property)
            || dynamic_cast<const FAnimationSequenceAssetProperty*>(Property)
            || dynamic_cast<const FAnimStateMachineAssetProperty*>(Property)
            || dynamic_cast<const FCurveAssetProperty*>(Property)
            || dynamic_cast<const FSceneAssetProperty*>(Property)
            || dynamic_cast<const FSoundAssetProperty*>(Property))
        {
            return Value.is<FString>()
                && SetReflectedAssetPathFromLua(Object, *Property, Value.as<FString>());
        }
        if (dynamic_cast<const FObjectProperty*>(Property))
        {
            return Value.is<UObject*>()
                && Property->SetPropertyValue_InContainer(&Object, Value.as<UObject*>());
        }

        return false;
    }
}

void FScriptManager::BindMathTypes()
{
    GLuaState->set_function("Log", [](const std::string& Msg)
    {
        UE_LOG(("[Lua] " + Msg + "\n").c_str());
    });

    GLuaState->set_function("LogWarning", [](const std::string& Msg)
    {
        UE_LOG_WARNING(("[Lua Warning] " + Msg + "\n").c_str());
    });

    GLuaState->set_function("LogError", [](const std::string& Msg)
    {
        UE_LOG_ERROR(("[Lua Error] " + Msg + "\n").c_str());
    });

    GLuaState->set_function("WaitForSeconds", [](sol::this_state State, float Seconds)
    {
        sol::state_view Lua(State);
        sol::table Table = Lua.create_table();
        Table["type"] = "seconds";
        Table["value"] = Seconds;
        return Table;
    });

    GLuaState->set_function("WaitForUnscaledSeconds", [](sol::this_state State, float Seconds)
    {
        sol::state_view Lua(State);
        sol::table Table = Lua.create_table();
        Table["type"] = "unscaled_seconds";
        Table["value"] = Seconds;
        return Table;
    });

    GLuaState->set_function("WaitForFrames", [](sol::this_state State, int Frames)
    {
        sol::state_view Lua(State);
        sol::table Table = Lua.create_table();
        Table["type"] = "frames";
        Table["value"] = Frames;
        return Table;
    });

    LUA_BEGIN_TYPE_FACTORY(GLuaState, FName, "FName", []()
                           { return FName(); }, [](const char* Str)
                           { return FName(Str); }, [](const std::string& Str)
                           { return FName(Str.c_str()); })
    LUA_METHOD(ToString, ToString);
    LUA_META(to_string, [](const FName& Name)
             { return Name.ToString(); });
    LUA_END_TYPE();

    LUA_BEGIN_TYPE_FACTORY(GLuaState, FVector, "Vector", []()
                           { return FVector(); }, [](float X, float Y, float Z)
                           { return FVector(X, Y, Z); })
    LUA_FIELD(X, X);
    LUA_FIELD(Y, Y);
    LUA_FIELD(Z, Z);
    LUA_FIELD(x, X);
    LUA_FIELD(y, Y);
    LUA_FIELD(z, Z);
    LUA_METHOD(Size, Size);
    LUA_METHOD(SizeSquared, SizeSquared);
    LUA_METHOD(Size2D, Size2D);
    LUA_METHOD(SizeSquared2D, SizeSquared2D);
    LUA_SET(Length, [](const FVector& Self)
            { return Self.Size(); });
    LUA_SET(LengthSquared, [](const FVector& Self)
            { return Self.SizeSquared(); });
    LUA_OVERLOAD(Normalize, [](FVector& Self)
                 { return Self.Normalize(); }, [](FVector& Self, float Tolerance)
                 { return Self.Normalize(Tolerance); });
    LUA_OVERLOAD(GetSafeNormal, [](const FVector& Self)
                 { return Self.GetSafeNormal(); }, [](const FVector& Self, float Tolerance)
                 { return Self.GetSafeNormal(Tolerance); });
    LUA_OVERLOAD(Normalized, [](const FVector& Self)
                 { return Self.Normalized(); }, [](const FVector& Self, float Tolerance)
                 { return Self.Normalized(Tolerance); });
    LUA_OVERLOAD(GetSafeNormal2D, [](const FVector& Self)
                 { return Self.GetSafeNormal2D(); }, [](const FVector& Self, float Tolerance)
                 { return Self.GetSafeNormal2D(Tolerance); });
    LUA_OVERLOAD(Normalized2D, [](const FVector& Self)
                 { return Self.GetSafeNormal2D(); }, [](const FVector& Self, float Tolerance)
                 { return Self.GetSafeNormal2D(Tolerance); });
    LUA_SET(Dot, [](const FVector& Self, const FVector& Other)
            { return Self.DotProduct(Other); });
    LUA_SET(DotProduct, [](const FVector& Self, const FVector& Other)
            { return Self.DotProduct(Other); });
    LUA_SET(Cross, [](const FVector& Self, const FVector& Other)
            { return Self.CrossProduct(Other); });
    LUA_SET(CrossProduct, [](const FVector& Self, const FVector& Other)
            { return Self.CrossProduct(Other); });
    LUA_SET(DistanceTo, [](const FVector& Self, const FVector& Other)
            { return FVector::Dist(Self, Other); });
    LUA_SET(DistanceSquaredTo, [](const FVector& Self, const FVector& Other)
            { return FVector::DistSquared(Self, Other); });
    LUA_SET(Distance, [](const FVector& A, const FVector& B)
            { return FVector::Dist(A, B); });
    LUA_SET(Dist, [](const FVector& A, const FVector& B)
            { return FVector::Dist(A, B); });
    LUA_SET(DistSquared, [](const FVector& A, const FVector& B)
            { return FVector::DistSquared(A, B); });
    LUA_SET(Lerp, [](const FVector& A, const FVector& B, float T)
            { return FVector::Lerp(A, B, T); });
    LUA_SET(Zero, []()
            { return FVector::ZeroVector; });
    LUA_SET(One, []()
            { return FVector::OneVector; });
    LUA_SET(Forward, []()
            { return FVector::ForwardVector; });
    LUA_SET(Backward, []()
            { return FVector::BackwardVector; });
    LUA_SET(Right, []()
            { return FVector::RightVector; });
    LUA_SET(Left, []()
            { return FVector::LeftVector; });
    LUA_SET(Up, []()
            { return FVector::UpVector; });
    LUA_SET(Down, []()
            { return FVector::DownVector; });
    LUA_META(equal_to, [](const FVector& A, const FVector& B)
             { return A == B; });
    LUA_META(addition, [](const FVector& A, const FVector& B)
             { return A + B; });
    LUA_META(subtraction, [](const FVector& A, const FVector& B)
             { return A - B; });
    LUA_META(unary_minus, [](const FVector& V)
             { return -V; });
    LUA_META(multiplication, sol::overload(
                                 [](const FVector& V, float S)
                                 {
                                     return V * S;
                                 },
                                 [](float S, const FVector& V)
                                 {
                                     return V * S;
                                 }));
    LUA_META(division, [](const FVector& V, float S)
             { return V / S; });
    LUA_END_TYPE();

    LUA_BEGIN_TYPE_FACTORY(GLuaState, FQuat, "Quat", []()
                           { return FQuat(); }, [](float X, float Y, float Z, float W)
                           { return FQuat(X, Y, Z, W); })
    LUA_FIELD(X, X);
    LUA_FIELD(Y, Y);
    LUA_FIELD(Z, Z);
    LUA_FIELD(W, W);
    LUA_FIELD(x, X);
    LUA_FIELD(y, Y);
    LUA_FIELD(z, Z);
    LUA_FIELD(w, W);
    LUA_META(equal_to, [](const FQuat& A, const FQuat& B)
             { return A == B; });
    LUA_META(addition, [](const FQuat& A, const FQuat& B)
             { return A + B; });
    LUA_META(subtraction, [](const FQuat& A, const FQuat& B)
             { return A - B; });
    LUA_META(unary_minus, [](const FQuat& Q)
             { return -Q; });
    LUA_META(multiplication, sol::overload(
                                 [](const FQuat& Q, float S)
                                 {
                                     return Q * S;
                                 },
                                 [](float S, const FQuat& Q)
                                 {
                                     return Q * S;
                                 },
                                 [](const FQuat& A, const FQuat& B)
                                 {
                                     return A * B;
                                 },
                                 [](const FQuat& Q, const FVector& V)
                                 {
                                     return Q * V;
                                 }));
    LUA_META(division, [](const FQuat& Q, float S)
             { return Q / S; });
    LUA_END_TYPE();

    LUA_BEGIN_TYPE_FACTORY(GLuaState, FTransform, "Transform",
                           []()
                           {
                               return FTransform();
                           },
                           [](const FVector& Translation, const FQuat& Rotation, const FVector& Scale)
                           {
                               return FTransform(Rotation, Translation, Scale);
                           })
    LUA_PROPERTY(Location, &FTransform::GetLocation, &FTransform::SetLocation);
    LUA_PROPERTY(Translation, &FTransform::GetTranslation, &FTransform::SetTranslation);
    LUA_PROPERTY(Rotation,
                 &FTransform::GetRotation,
                 [](FTransform& Self, const FQuat& InRotation)
                 {
                     Self.SetRotation(InRotation);
                 });
    LUA_PROPERTY(Scale, &FTransform::GetScale3D, &FTransform::SetScale3D);
    LUA_END_TYPE();

    LUA_BEGIN_TYPE_NO_CTOR(GLuaState, FHitResult, "HitResult")
    LUA_FIELD(Distance, Distance);
    LUA_FIELD(Location, Location);
    LUA_FIELD(Normal, Normal);
    LUA_FIELD(FaceIndex, FaceIndex);
    LUA_FIELD(bHit, bHit);
    LUA_METHOD(Reset, Reset);
    LUA_METHOD(IsValid, IsValid);
    LUA_END_TYPE();
}

void FScriptManager::BindObjectTypes()
{
    LUA_BEGIN_TYPE_NO_CTOR(GLuaState, UObject, "Object")
    LUA_RO_PROPERTY(UUID, GetUUID);
    LUA_METHOD(GetUUID, GetUUID);
    LUA_SET(GetName, &LuaGetObjectName);
    LUA_SET(GetType, &LuaGetObjectType);
    LUA_SET(IsA, &LuaObjectIsA);
    LUA_SET(GetProperty, &LuaGetReflectedProperty);
    LUA_SET(SetProperty, &LuaSetReflectedProperty);
    LUA_SET(GetReflectedProperty, &LuaGetReflectedProperty);
    LUA_SET(SetReflectedProperty, &LuaSetReflectedProperty);
    LUA_END_TYPE();
}
