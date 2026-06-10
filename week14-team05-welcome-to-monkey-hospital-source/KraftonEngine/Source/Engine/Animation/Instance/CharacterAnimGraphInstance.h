#pragma once

#include "Animation/Graph/AnimGraphInstance.h"

// UAnimGraphInstance + UCharacterAnimInstance 데모 변수의 합성.
//
// 그래프가 가정하는 OwnerClass 와 실제 박힌 AnimInstance 클래스가 같아야 변수 reflection 이
// 동작한다. UCharacterAnimInstance 는 UAnimInstance 직접 상속이라 그래프 흐름과 호환 X —
// 그래서 이 클래스를 별도로 둔다.
//
// 사용:
//   1) SkeletalMeshComponent.AnimInstanceClass = UCharacterAnimGraphInstance
//   2) GraphAssetPath 에 편집한 그래프 자산 path 박음
//   3) 그래프의 OwnerClass = "UCharacterAnimGraphInstance" 선택
//   4) VariableGet → BlendListByEnum.Selector 에 Speed 연결
//   5) 자동 변동되는 Speed (sin) 가 매 frame BlendListByEnum 의 ActiveChildIndex 갱신
//      → Idle ↔ Walk 자동 전환 (sequence 가 박혀 있다는 가정)

#include "Source/Engine/Animation/Instance/CharacterAnimGraphInstance.generated.h"

UCLASS()
class UCharacterAnimGraphInstance : public UAnimGraphInstance
{
public:
	GENERATED_BODY()
	UCharacterAnimGraphInstance() = default;
	~UCharacterAnimGraphInstance() override = default;

	void NativeUpdateAnimation(float DeltaSeconds) override;
	void Serialize(FArchive& Ar)                   override;

	// CharacterMovement 에서 계산되는 자동 관측값.
	// 중요: 이 값들은 reflected UPROPERTY fallback 용이다. 같은 이름의 AnimGraph 변수를
	// My Blueprint - Variables 에 선언하면 그 변수는 Lua BP / 게임 코드가 수동 제어하고,
	// 아래 reflected 값보다 우선된다. 즉 C++ 자동 갱신이 BP 입력을 덮어쓰지 않는다.
	UPROPERTY(Edit, Category="Animation|Character", DisplayName="Speed", Min=0.0f, Max=100.0f, Speed=0.5f)
	float Speed = 0.0f;

	UPROPERTY(Edit, Category="Animation|Character", DisplayName="Ground Speed", Min=0.0f, Max=100.0f, Speed=0.5f)
	float GroundSpeed = 0.0f;

	UPROPERTY(Edit, Category="Animation|Character", DisplayName="Direction", Min=-180.0f, Max=180.0f, Speed=1.0f)
	float Direction = 0.0f;

	UPROPERTY(Edit, Category="Animation|Character", DisplayName="Should Move")
	bool ShouldMove = false;

	UPROPERTY(Edit, Category="Animation|Character", DisplayName="Is Falling")
	bool IsFalling = false;

	// 기존 그래프 호환용 별칭. 기존 rule 이 bIsFalling 을 읽는 경우도 계속 동작한다.
	UPROPERTY(Edit, Category="Animation|Character", DisplayName="bIsFalling")
	bool bIsFalling = false;
};
