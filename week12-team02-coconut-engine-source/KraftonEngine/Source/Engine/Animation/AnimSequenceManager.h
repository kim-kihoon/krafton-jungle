#pragma once

#include "Core/CoreTypes.h"
#include "Core/Singleton.h"

class UAnimSequence;
/**
 * AnimSequence용 runtime loader/cache manager 입니다.
 * .uasset을 읽고 UAnimSequence를 캐싱합니다.
 * Editor import 직후 등록하거나, Content Browser에서
 * 이 파일이 AnimSequence package인지 확인하는 데도 씁니다.
 */
class FAnimSequenceManager : public TSingleton<FAnimSequenceManager>
{
	friend class TSingleton<FAnimSequenceManager>;

public:
	UAnimSequence* Load(const FString& Path);
	UAnimSequence* Find(const FString& Path) const;
	void RegisterAnimSequence(const FString& Path, UAnimSequence* AnimSequence);
	bool Save(UAnimSequence* AnimSequence);
	bool IsAnimSequencePackage(const FString& Path) const;

private:
	TMap<FString, UAnimSequence*> AnimSequenceCache;
};
