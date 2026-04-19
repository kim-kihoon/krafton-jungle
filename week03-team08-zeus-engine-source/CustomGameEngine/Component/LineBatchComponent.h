#pragma once

#include "Math/Color.h"
//#include "UObject/ObjectMacros.h"
#include "Component/PrimitiveComponent.h"
#include "Logger.h"

//#include "LineBatchComponent.generated.h"

// 렌더링을 위해 수집되는 개별 라인 데이터
struct FBatchedLine
{
    FVector Start;          // 시작점
    FVector End;            // 끝점
    FColor Color;         // 색상 
 //   float Thickness;        // 선 두께 (DX11 기본 선은 1px 고정이지만, 나중에 지오메트리 셰이더 확장 고려)
 //   float RemainingLifeTime; // 0이면 이번 프레임에만 그리고 삭제, > 0 이면 일정 시간 유지
	//uint8_t DepthPriority;
	EShowFlag Type;
 //   uint32 BatchID;

	//FBatchedLine()
	//	: Start(ForceInit)
	//	, End(ForceInit)
	//	, Color(ForceInit)
	//	, Thickness(0)
	//	, RemainingLifeTime(0)
	//	, DepthPriority(0)
	//	, BatchID(0)
	//{
	//}
	FBatchedLine(const FVector& InStart, const FVector& InEnd, const FColor& InColor, EShowFlag InType)  //, uint32 InBatchID = 0)
		: Start(InStart)
		, End(InEnd)
		, Color(InColor)
		//, Thickness(InThickness)
		//, RemainingLifeTime(InLifeTime)
		//, DepthPriority(InDepthPriority)
		, Type(InType)
	//	, BatchID(InBatchID)
	{
	}
};


class ULineBatchComponent : public UPrimitiveComponent
{
	DECLARE_OBJECT(ULineBatchComponent, UPrimitiveComponent)
public:
	ULineBatchComponent();
	~ULineBatchComponent() override;
	
private:
	// 수집된 선 데이터들은 BatchedLines에 차곡차곡 쌓이게 됩니다.
	TArray<FBatchedLine> BatchedLines;

public:
	void Update(float DeltaTime) override;
	void CreateRenderObjects() override;
	void UpdateRenderObjects() override;

	void DrawLine(const FVector& Start, const FVector& End, const FColor& Color, EShowFlag Type = EShowFlag::Debug);

};