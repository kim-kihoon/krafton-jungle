#pragma once
#include "PrimitiveComponent.h"
#include "Renderer/SubUV.h"
#include <string>

class UParticleSubUVComp : public UPrimitiveComponent
{
	DECLARE_OBJECT(UParticleSubUVComp, UPrimitiveComponent)
public:
	UParticleSubUVComp(const wchar_t* InTexture = nullptr);
	virtual ~UParticleSubUVComp() override;

	int32 Columns = 6;
	int32 Rows = 6;
	float PlayRate = 24.0f;
	bool bLoop = true;
	bool LastFrame = false;

	void SetTexture(const wchar_t* path);
	const wchar_t* GetTexturePath() const { return TexturePath.c_str(); }

	virtual void Update(float DeltaTime) override;
	void TickComponent(float DeltaTime);
	virtual void CreateRenderObjects() override;

	virtual json::JSON Serialize() override;
	virtual void Deserialize(json::JSON) override;

private:
	std::wstring TexturePath;
	FSubUV SubUVData;
	float ElapsedTime = 0.0f;
};

