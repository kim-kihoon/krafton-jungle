#include "Component/Light/LightComponent.h"
#include "Serialization/Archive.h"
#include "Object/ObjectFactory.h"

//void ULightComponent::GetEditableProperties(TArray<FProperty>& OutProps)
//{
//	ULightComponentBase::GetEditableProperties(OutProps);
//	OutProps.push_back({ "Shadow Resolution Scale", EPropertyType::Float, "Shadow", &ShadowResolutionScale, 0.1f, 4.0f, 0.1f });
//	OutProps.push_back({ "Shadow Bias",             EPropertyType::Float, "Shadow", &ShadowBias,            -0.2f, 0.2f, 0.0001f });
//	OutProps.push_back({ "Shadow Slope Bias",       EPropertyType::Float, "Shadow", &ShadowSlopeBias,       -0.2f, 0.2f, 0.0001f });
//	OutProps.push_back({ "Shadow Normal Bias",      EPropertyType::Float, "Shadow", &ShadowNormalBias,      -0.2f, 0.2f, 0.0001f });
//	OutProps.push_back({ "Shadow Sharpen",          EPropertyType::Float, "Shadow", &ShadowSharpen,         0.0f, 1.0f, 0.05f });
//}
