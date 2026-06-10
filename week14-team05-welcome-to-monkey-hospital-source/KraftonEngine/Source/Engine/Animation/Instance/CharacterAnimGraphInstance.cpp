#include "CharacterAnimGraphInstance.h"

#include "Component/Movement/CharacterMovementComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "GameFramework/AActor.h"
#include "Math/MathUtils.h"
#include "Serialization/Archive.h"

#include <cmath>

void UCharacterAnimGraphInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	// 부모 (UAnimGraphInstance) 가 자산 Version polling + 재컴파일 처리.
	Super::NativeUpdateAnimation(DeltaSeconds);

	Speed       = 0.0f;
	GroundSpeed = 0.0f;
	Direction   = 0.0f;
	ShouldMove  = false;
	IsFalling   = false;
	bIsFalling  = false;
	if (USkeletalMeshComponent* Comp = GetOwningComponent())
	{
		if (AActor* Owner = Comp->GetOwner())
		{
			if (UCharacterMovementComponent* Move = Owner->GetComponentByClass<UCharacterMovementComponent>())
			{
				Speed       = Move->GetSpeed();
				GroundSpeed = Speed;
				ShouldMove  = Speed > 3.0f;
				IsFalling   = Move->IsFalling();
				bIsFalling  = IsFalling;
				// Direction 계산은 아직 이 엔진의 CharacterMovement API 에 actor-relative velocity
				// helper 가 없으므로 0 으로 둔다. 필요 시 owner forward/right 기반 계산을 추가한다.
			}
		}
	}

	// 여기서 SetGraphVariable* 를 호출하지 않는다.
	// AnimGraph-owned 변수는 Lua BP / 게임 코드가 명시적으로 set 하는 수동 입력이어야 한다.
	// 자동 locomotion 값은 위 UPROPERTY 들로만 노출하고, 그래프에 같은 이름의 변수가
	// 선언되어 있지 않을 때 compiler 의 reflected-property fallback 으로 읽힌다.
	// 이렇게 해야 Lua BP 가 Set Anim Graph Bool("IsFalling", ...) 로 넣은 값이 다음 프레임에
	// CharacterMovement 자동값으로 덮어써지는 문제가 생기지 않는다.
}

void UCharacterAnimGraphInstance::Serialize(FArchive& Ar)
{
	// 부모 직렬화 — GraphAssetPath / DefaultSequencePath 라운드트립.
	Super::Serialize(Ar);
}
