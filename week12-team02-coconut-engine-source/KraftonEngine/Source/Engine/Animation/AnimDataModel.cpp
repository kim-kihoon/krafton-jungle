#include "Animation/AnimDataModel.h"

#include "Animation/Notify.h"
#include "Animation/NotifyRegistry.h"
#include "Core/Log.h"
#include "Core/Property/FEnumProperty.h"
#include "Core/UObject/FSoftObjectPtr.h"
#include "Object/ObjectFactory.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

IMPLEMENT_CLASS(UAnimDataModel, UObject)

namespace
{
	constexpr uint32 AnimNotifyListMagic = 0x544E5645; // EVNT
	constexpr uint32 AnimNotifyListVersion = 1;
	constexpr uint32 AnimNotifyPayloadMagic = 0x544F4E41; // ANOT
	constexpr uint32 AnimNotifyPayloadVersion = 1;
	constexpr uint32 MaxNotifyPayloadStringBytes = 4096;
	constexpr uint32 MaxNotifyPayloadProperties = 256;

	float Clamp01(float Value)
	{
		return std::max(0.0f, std::min(1.0f, Value));
	}

	float NormalizeSequenceTime(float Time, float Length)
	{
		if (Length <= 0.0f)
		{
			return 0.0f;
		}

		return std::max(0.0f, std::min(Time, Length));
	}

	FVector SampleVectorKeys(const TArray<FVector>& Keys, float FramePosition, int32 NumberOfKeys, const FVector& DefaultValue)
	{
		if (Keys.empty())
		{
			return DefaultValue;
		}

		if (Keys.size() == 1 || NumberOfKeys <= 1)
		{
			return Keys.front();
		}

		const int32 LastKey = static_cast<int32>(Keys.size()) - 1;
		const float KeyPosition = FramePosition * (static_cast<float>(LastKey) / static_cast<float>(std::max(1, NumberOfKeys - 1)));
		const int32 Key0 = std::max(0, std::min(static_cast<int32>(std::floor(KeyPosition)), LastKey));
		const int32 Key1 = std::max(0, std::min(Key0 + 1, LastKey));
		const float Alpha = Clamp01(KeyPosition - static_cast<float>(Key0));
		return FVector::Lerp(Keys[Key0], Keys[Key1], Alpha);
	}

	FQuat SampleQuatKeys(const TArray<FQuat>& Keys, float FramePosition, int32 NumberOfKeys, const FQuat& DefaultValue)
	{
		if (Keys.empty())
		{
			return DefaultValue;
		}

		if (Keys.size() == 1 || NumberOfKeys <= 1)
		{
			return Keys.front();
		}

		const int32 LastKey = static_cast<int32>(Keys.size()) - 1;
		const float KeyPosition = FramePosition * (static_cast<float>(LastKey) / static_cast<float>(std::max(1, NumberOfKeys - 1)));
		const int32 Key0 = std::max(0, std::min(static_cast<int32>(std::floor(KeyPosition)), LastKey));
		const int32 Key1 = std::max(0, std::min(Key0 + 1, LastKey));
		const float Alpha = Clamp01(KeyPosition - static_cast<float>(Key0));
		return FQuat::Slerp(Keys[Key0], Keys[Key1], Alpha);
	}

	template <typename T>
	bool ReadValue(FArchive& Ar, T& OutValue)
	{
		if (!Ar.CanSerialize(sizeof(T)))
		{
			return false;
		}

		Ar << OutValue;
		return true;
	}

	template <typename T>
	void WriteValue(FArchive& Ar, T& Value)
	{
		Ar << Value;
	}

	void WritePayloadString(FArchive& Ar, const FString& Value)
	{
		uint32 Length = static_cast<uint32>(Value.size());
		Ar << Length;
		if (Length > 0)
		{
			Ar.Serialize(const_cast<char*>(Value.data()), Length);
		}
	}

	bool ReadPayloadString(FArchive& Ar, FString& OutValue)
	{
		uint32 Length = 0;
		if (!ReadValue(Ar, Length))
		{
			return false;
		}

		if (Length > MaxNotifyPayloadStringBytes || !Ar.CanSerialize(Length))
		{
			return false;
		}

		OutValue.resize(Length);
		if (Length > 0)
		{
			Ar.Serialize(OutValue.data(), Length);
		}
		return true;
	}

	bool IsSupportedNotifyPropertyType(EPropertyType Type)
	{
		switch (Type)
		{
		case EPropertyType::Bool:
		case EPropertyType::ByteBool:
		case EPropertyType::Int:
		case EPropertyType::Float:
		case EPropertyType::Vec3:
		case EPropertyType::Vec4:
		case EPropertyType::Rotator:
		case EPropertyType::String:
		case EPropertyType::Name:
		case EPropertyType::SceneComponentRef:
		case EPropertyType::Color4:
		case EPropertyType::MaterialSlot:
		case EPropertyType::Enum:
		case EPropertyType::Script:
		case EPropertyType::SoftObject:
			return true;
		default:
			return false;
		}
	}

	const FProperty* FindNotifyPropertyByName(const TArray<const FProperty*>& Properties, const FString& Name)
	{
		for (const FProperty* Property : Properties)
		{
			if (Property && Property->Name == Name)
			{
				return Property;
			}
		}
		return nullptr;
	}

	uint32 CountSupportedNotifyProperties(const TArray<const FProperty*>& Properties)
	{
		uint32 Count = 0;
		for (const FProperty* Property : Properties)
		{
			if (Property && IsSupportedNotifyPropertyType(Property->GetType()))
			{
				++Count;
			}
		}
		return Count;
	}

	bool SerializeFloatValues(FArchive& Ar, float* Values, uint32 Count)
	{
		const size_t ByteCount = sizeof(float) * Count;
		if (!Ar.CanSerialize(ByteCount))
		{
			return false;
		}

		Ar.Serialize(Values, ByteCount);
		return true;
	}

	bool ReadLegacyNotifyList(FArchive& Ar, uint32 NotifyCount, TArray<FAnimNotifyEvent>& Notifies)
	{
		if (NotifyCount == 0)
		{
			Notifies.clear();
			return true;
		}

		const size_t ByteCount = sizeof(FAnimNotifyEvent) * static_cast<size_t>(NotifyCount);
		if (!Ar.CanSerialize(ByteCount))
		{
			Notifies.clear();
			return false;
		}

		Notifies.resize(NotifyCount);
		Ar.Serialize(Notifies.data(), ByteCount);
		return true;
	}

	bool SerializeNotifyList(FArchive& Ar, TArray<FAnimNotifyEvent>& Notifies)
	{
		if (Ar.IsSaving())
		{
			uint32 Magic = AnimNotifyListMagic;
			uint32 Version = AnimNotifyListVersion;
			uint32 NotifyCount = static_cast<uint32>(Notifies.size());
			WriteValue(Ar, Magic);
			WriteValue(Ar, Version);
			WriteValue(Ar, NotifyCount);

			for (FAnimNotifyEvent& Notify : Notifies)
			{
				Ar << Notify;
			}
			return true;
		}

		if (!Ar.IsLoading())
		{
			return true;
		}

		uint32 MagicOrLegacyCount = 0;
		if (!ReadValue(Ar, MagicOrLegacyCount))
		{
			return false;
		}

		if (MagicOrLegacyCount != AnimNotifyListMagic)
		{
			return ReadLegacyNotifyList(Ar, MagicOrLegacyCount, Notifies);
		}

		uint32 Version = 0;
		if (!ReadValue(Ar, Version) || Version != AnimNotifyListVersion)
		{
			Notifies.clear();
			UE_LOG("AnimNotify load warning: unsupported notify list version.");
			return false;
		}

		uint32 NotifyCount = 0;
		if (!ReadValue(Ar, NotifyCount))
		{
			Notifies.clear();
			return false;
		}

		Notifies.resize(NotifyCount);
		for (FAnimNotifyEvent& Notify : Notifies)
		{
			Ar << Notify;
		}
		return true;
	}

	void WriteNotifyPropertyValue(FArchive& Ar, const FProperty& Property, const void* Container)
	{
		const void* ValuePtr = Property.ContainerPtrToValuePtr(Container);
		switch (Property.GetType())
		{
		case EPropertyType::Bool:
		{
			bool Value = *static_cast<const bool*>(ValuePtr);
			WriteValue(Ar, Value);
			break;
		}
		case EPropertyType::ByteBool:
		{
			uint8 Value = *static_cast<const uint8*>(ValuePtr);
			WriteValue(Ar, Value);
			break;
		}
		case EPropertyType::Int:
		{
			int32 Value = *static_cast<const int32*>(ValuePtr);
			WriteValue(Ar, Value);
			break;
		}
		case EPropertyType::Float:
		{
			float Value = *static_cast<const float*>(ValuePtr);
			WriteValue(Ar, Value);
			break;
		}
		case EPropertyType::Vec3:
		case EPropertyType::Rotator:
		{
			float Values[3] = {};
			std::memcpy(Values, ValuePtr, sizeof(Values));
			Ar.Serialize(Values, sizeof(Values));
			break;
		}
		case EPropertyType::Vec4:
		case EPropertyType::Color4:
		{
			float Values[4] = {};
			std::memcpy(Values, ValuePtr, sizeof(Values));
			Ar.Serialize(Values, sizeof(Values));
			break;
		}
		case EPropertyType::String:
		case EPropertyType::Script:
		case EPropertyType::SceneComponentRef:
		{
			WritePayloadString(Ar, *static_cast<const FString*>(ValuePtr));
			break;
		}
		case EPropertyType::Name:
		{
			WritePayloadString(Ar, static_cast<const FName*>(ValuePtr)->ToString());
			break;
		}
		case EPropertyType::Enum:
		{
			int64 Value = 0;
			std::memcpy(&Value, ValuePtr, std::min<uint32>(Property.ElementSize, sizeof(Value)));
			WriteValue(Ar, Value);
			break;
		}
		case EPropertyType::MaterialSlot:
		{
			WritePayloadString(Ar, static_cast<const FMaterialSlot*>(ValuePtr)->Path);
			break;
		}
		case EPropertyType::SoftObject:
		{
			WritePayloadString(Ar, static_cast<const FSoftObjectPtr*>(ValuePtr)->GetPath().ToString());
			break;
		}
		default:
			break;
		}
	}

	bool ReadNotifyPropertyValue(FArchive& Ar, EPropertyType StoredType, const FProperty* TargetProperty, void* Container)
	{
		const bool bCanApply = TargetProperty
			&& TargetProperty->GetType() == StoredType
			&& Container;

		void* ValuePtr = bCanApply ? TargetProperty->ContainerPtrToValuePtr(Container) : nullptr;
		switch (StoredType)
		{
		case EPropertyType::Bool:
		{
			bool Value = false;
			if (!ReadValue(Ar, Value)) return false;
			if (bCanApply) *static_cast<bool*>(ValuePtr) = Value;
			return true;
		}
		case EPropertyType::ByteBool:
		{
			uint8 Value = 0;
			if (!ReadValue(Ar, Value)) return false;
			if (bCanApply) *static_cast<uint8*>(ValuePtr) = Value;
			return true;
		}
		case EPropertyType::Int:
		{
			int32 Value = 0;
			if (!ReadValue(Ar, Value)) return false;
			if (bCanApply) *static_cast<int32*>(ValuePtr) = Value;
			return true;
		}
		case EPropertyType::Float:
		{
			float Value = 0.0f;
			if (!ReadValue(Ar, Value)) return false;
			if (bCanApply) *static_cast<float*>(ValuePtr) = Value;
			return true;
		}
		case EPropertyType::Vec3:
		case EPropertyType::Rotator:
		{
			float Values[3] = {};
			if (!SerializeFloatValues(Ar, Values, 3)) return false;
			if (bCanApply) std::memcpy(ValuePtr, Values, sizeof(Values));
			return true;
		}
		case EPropertyType::Vec4:
		case EPropertyType::Color4:
		{
			float Values[4] = {};
			if (!SerializeFloatValues(Ar, Values, 4)) return false;
			if (bCanApply) std::memcpy(ValuePtr, Values, sizeof(Values));
			return true;
		}
		case EPropertyType::String:
		case EPropertyType::Script:
		case EPropertyType::SceneComponentRef:
		{
			FString Value;
			if (!ReadPayloadString(Ar, Value)) return false;
			if (bCanApply) *static_cast<FString*>(ValuePtr) = Value;
			return true;
		}
		case EPropertyType::Name:
		{
			FString Value;
			if (!ReadPayloadString(Ar, Value)) return false;
			if (bCanApply) *static_cast<FName*>(ValuePtr) = FName(Value);
			return true;
		}
		case EPropertyType::Enum:
		{
			int64 Value = 0;
			if (!ReadValue(Ar, Value)) return false;
			if (bCanApply)
			{
				std::memcpy(ValuePtr, &Value, std::min<uint32>(TargetProperty->ElementSize, sizeof(Value)));
			}
			return true;
		}
		case EPropertyType::MaterialSlot:
		{
			FString Value;
			if (!ReadPayloadString(Ar, Value)) return false;
			if (bCanApply) static_cast<FMaterialSlot*>(ValuePtr)->Path = Value;
			return true;
		}
		case EPropertyType::SoftObject:
		{
			FString Value;
			if (!ReadPayloadString(Ar, Value)) return false;
			if (bCanApply) static_cast<FSoftObjectPtr*>(ValuePtr)->SetPath(Value);
			return true;
		}
		default:
			return false;
		}
	}

	void WriteNotifyTriggerPayload(FArchive& Ar, const FAnimNotifyEvent& Notify)
	{
		uint8 bHasTrigger = Notify.NotifyTrigger ? 1 : 0;
		WriteValue(Ar, bHasTrigger);
		if (!Notify.NotifyTrigger)
		{
			return;
		}

		FString ClassName = Notify.NotifyTrigger->GetClass()->GetName();
		WritePayloadString(Ar, ClassName);

		TArray<const FProperty*> Properties;
		Notify.NotifyTrigger->GetEditableProperties(Properties);
		uint32 PropertyCount = CountSupportedNotifyProperties(Properties);
		WriteValue(Ar, PropertyCount);

		for (const FProperty* Property : Properties)
		{
			if (!Property || !IsSupportedNotifyPropertyType(Property->GetType()))
			{
				continue;
			}

			WritePayloadString(Ar, Property->Name);
			uint8 TypeValue = static_cast<uint8>(Property->GetType());
			WriteValue(Ar, TypeValue);
			WriteNotifyPropertyValue(Ar, *Property, Notify.NotifyTrigger);
		}
	}

	bool ReadNotifyTriggerPayload(FArchive& Ar, FAnimNotifyEvent& Notify)
	{
		uint8 bHasTrigger = 0;
		if (!ReadValue(Ar, bHasTrigger))
		{
			return false;
		}

		Notify.NotifyTrigger = nullptr;
		if (!bHasTrigger)
		{
			return true;
		}

		FString ClassName;
		if (!ReadPayloadString(Ar, ClassName))
		{
			return false;
		}

		uint32 PropertyCount = 0;
		if (!ReadValue(Ar, PropertyCount) || PropertyCount > MaxNotifyPayloadProperties)
		{
			return false;
		}

		UNotify* Trigger = nullptr;
		const TMap<FString, UClass*>& NotifyClasses = FNotifyRegistry::Get().GetNotifyClasses();
		if (NotifyClasses.find(ClassName) != NotifyClasses.end())
		{
			UObject* Object = FObjectFactory::Get().Create(ClassName);
			Trigger = Cast<UNotify>(Object);
		}
		else
		{
			UE_LOG("AnimNotify load warning: unknown notify class. Class=%s", ClassName.c_str());
		}

		TArray<const FProperty*> Properties;
		if (Trigger)
		{
			Trigger->GetEditableProperties(Properties);
		}

		for (uint32 PropertyIndex = 0; PropertyIndex < PropertyCount; ++PropertyIndex)
		{
			FString PropertyName;
			if (!ReadPayloadString(Ar, PropertyName))
			{
				return false;
			}

			uint8 TypeValue = 0;
			if (!ReadValue(Ar, TypeValue))
			{
				return false;
			}

			const EPropertyType StoredType = static_cast<EPropertyType>(TypeValue);
			const FProperty* TargetProperty = Trigger ? FindNotifyPropertyByName(Properties, PropertyName) : nullptr;
			if (!ReadNotifyPropertyValue(Ar, StoredType, TargetProperty, Trigger))
			{
				return false;
			}
		}

		Notify.NotifyTrigger = Trigger;
		return true;
	}

	void SerializeNotifyPayload(FArchive& Ar, TArray<FAnimNotifyEvent>& Notifies)
	{
		if (Ar.IsSaving())
		{
			uint32 Magic = AnimNotifyPayloadMagic;
			uint32 Version = AnimNotifyPayloadVersion;
			uint32 NotifyCount = static_cast<uint32>(Notifies.size());
			WriteValue(Ar, Magic);
			WriteValue(Ar, Version);
			WriteValue(Ar, NotifyCount);

			for (const FAnimNotifyEvent& Notify : Notifies)
			{
				WriteNotifyTriggerPayload(Ar, Notify);
			}
			return;
		}

		if (!Ar.IsLoading() || !Ar.CanSerialize(sizeof(uint32)))
		{
			return;
		}

		uint32 Magic = 0;
		if (!ReadValue(Ar, Magic) || Magic != AnimNotifyPayloadMagic)
		{
			return;
		}

		uint32 Version = 0;
		if (!ReadValue(Ar, Version) || Version != AnimNotifyPayloadVersion)
		{
			UE_LOG("AnimNotify load warning: unsupported notify payload version.");
			return;
		}

		uint32 NotifyCount = 0;
		if (!ReadValue(Ar, NotifyCount) || NotifyCount != static_cast<uint32>(Notifies.size()))
		{
			UE_LOG("AnimNotify load warning: notify payload count mismatch.");
			return;
		}

		for (FAnimNotifyEvent& Notify : Notifies)
		{
			if (!ReadNotifyTriggerPayload(Ar, Notify))
			{
				for (FAnimNotifyEvent& ExistingNotify : Notifies)
				{
					ExistingNotify.NotifyTrigger = nullptr;
				}
				UE_LOG("AnimNotify load warning: notify payload is incomplete.");
				return;
			}
		}
	}
}

void UAnimDataModel::Serialize(FArchive& Ar)
{
	Ar << BoneAnimationTracks;
	SerializeNotifyList(Ar, Notifies);
	if (Ar.IsLoading())
	{
		for (FAnimNotifyEvent& Notify : Notifies)
		{
			Notify.NotifyTrigger = nullptr;
		}
	}

	Ar << PlayLength;
	Ar << FrameRate;
	Ar << NumberOfFrames;
	Ar << NumberOfKeys;
	SerializeNotifyPayload(Ar, Notifies);
}

const FBoneAnimationTrack* UAnimDataModel::GetBoneTrackByIndex(int32 TrackIndex) const
{
	if (TrackIndex < 0 || TrackIndex >= static_cast<int32>(BoneAnimationTracks.size()))
	{
		return nullptr;
	}

	return &BoneAnimationTracks[TrackIndex];
}

const FBoneAnimationTrack* UAnimDataModel::FindBoneTrackByName(const FName& BoneName) const
{
	for (const FBoneAnimationTrack& Track : BoneAnimationTracks)
	{
		if (Track.Name == BoneName)
		{
			return &Track;
		}
	}

	return nullptr;
}

const FBoneAnimationTrack* UAnimDataModel::FindBoneTrackByIndex(int32 BoneTreeIndex) const
{
	for (const FBoneAnimationTrack& Track : BoneAnimationTracks)
	{
		if (Track.BoneTreeIndex == BoneTreeIndex)
		{
			return &Track;
		}
	}

	return nullptr;
}

bool UAnimDataModel::EvaluateBoneTrackTransform(const FBoneAnimationTrack& Track, float Time, FTransform& OutTransform, const FTransform& DefaultTransform) const
{
	const float EvalTime = NormalizeSequenceTime(Time, PlayLength);
	const int32 KeyCount = std::max(1, NumberOfKeys);
	const float FramePosition = (PlayLength > 0.0f && KeyCount > 1)
		? (EvalTime / PlayLength) * static_cast<float>(KeyCount - 1)
		: 0.0f;

	OutTransform.Location = SampleVectorKeys(Track.InternalTrackData.PosKeys, FramePosition, KeyCount, DefaultTransform.Location);
	OutTransform.Rotation = SampleQuatKeys(Track.InternalTrackData.RotKeys, FramePosition, KeyCount, DefaultTransform.Rotation);
	OutTransform.Scale = SampleVectorKeys(Track.InternalTrackData.ScaleKeys, FramePosition, KeyCount, DefaultTransform.Scale);
	return true;
}
