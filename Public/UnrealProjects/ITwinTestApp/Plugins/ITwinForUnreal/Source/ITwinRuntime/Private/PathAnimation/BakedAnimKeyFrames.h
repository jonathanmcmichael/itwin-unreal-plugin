/*--------------------------------------------------------------------------------------+
|
|     $Source: BakedAnimKeyFrames.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <Containers/Array.h>
//#include <Containers/Map.h>
//#include <GameFramework/Actor.h>
#include <Math/MathFwd.h>
#include <Misc/EnumRange.h>
#include <UObject/Object.h>
//#include <Templates/PimplPtr.h>
//#include <Spline/ITwinSplineHelper.h>

#include <optional>

#include "BakedAnimKeyFrames.generated.h"

class UWorld;

namespace AdvViz::SDK
{
	class RefID;
}

enum class EBakedKeyFramesStatus : uint8
{
	Invalid,
	InProgress,
	Ready
};


UCLASS()
class UBakedAnimKeyFrames : public UObject
{
	GENERATED_BODY()
public:
	UBakedAnimKeyFrames() {}

	void MarkForUpdate();
	bool NeedsUpdate() const;
	bool IsReady() const;

	float GetTotalTime() const;
	float GetTotalLength() const;
	int32 GetLaneIndex() const;
	void SetSpeed(float InSpeed);
	void BakeSpline(UWorld* World, const AdvViz::SDK::RefID& SplineId, float InSpeed, int32 InLaneIdx, std::optional<float> InOffset = {}/*corresponds to the offset of the lane*/);
	int32 GetKeyframeIndex(float Time);
	FTransform GetTransform(float Time, bool bReverse = false);

private:
	TArray<FTransform> transforms;
	float TotalLength = 0.f;
	float TotalTime = 0.f;
	float DistanceStep = 30 / 25.f; // sample every 1/25s for an object moving at 1 km/h
	int32 LaneIdx = 0;
	EBakedKeyFramesStatus Status = EBakedKeyFramesStatus::Invalid;
};
