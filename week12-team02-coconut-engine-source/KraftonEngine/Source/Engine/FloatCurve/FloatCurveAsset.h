#pragma once
#include "Object/Object.h"
#include "Math/FloatCurve.h"
#include "FloatCurveAsset.generated.h"

class FArchive;

UCLASS()
class UFloatCurveAsset : public UObject
{
public:
	GENERATED_BODY(UFloatCurveAsset)

	UFloatCurveAsset() = default;
	~UFloatCurveAsset() override;

	FFloatCurve& GetCurve() { return Curve; }
	const FFloatCurve& GetCurve() const { return Curve; }

	void SetSourcePath(const FString& InPath) { SourcePath = InPath; }
	const FString& GetSourcePath() const { return SourcePath; }

	void Serialize(FArchive& Ar) override;

private:
	FFloatCurve Curve;
	FString SourcePath;
};
