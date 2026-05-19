/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAnimPathInfo.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <Containers/Array.h>
#include <Containers/Map.h>
#include <GameFramework/Actor.h>
#include <Misc/EnumRange.h>
#include <Templates/PimplPtr.h>
#include <Spline/ITwinSplineHelper.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
	#include <glm/ext/matrix_double3x3.hpp>
	#include <glm/ext/vector_double3.hpp>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <memory>
#include <optional>

#include "ITwinAnimPathInfo.generated.h"

class AITwinPopulation;
class AITwinSplineHelper;
class UBakedAnimKeyFrames;

namespace AdvViz::SDK
{
	class RefID;
}

// Animation path type, should be in the same order same as in
// ITwinStudioApp\carrot\frontend\components\Toolbar\tools\PathAnimTool.tsx
UENUM(BlueprintType)
enum class EITwinAnimPathType : uint8
{
	Object,
	Crowd,
	Traffic,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EITwinAnimPathType, EITwinAnimPathType::Count);

UENUM(BlueprintType)
enum class EITwinAnimPathRepeatMode : uint8
{
	None,
	Loop,
	PingPong,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EITwinAnimPathRepeatMode, EITwinAnimPathRepeatMode::Count);


UCLASS()
class UITwinAnimPathInfo : public UObject
{
	GENERATED_BODY()

public:
	virtual ~UITwinAnimPathInfo() = default;
	//FString PathId;
	//FString PathName; // do we need to rename paths?
	//FString SplineId;
	TWeakObjectPtr<AITwinSplineHelper> SplineHelper;
	TArray<TWeakObjectPtr<AITwinPopulation> > Populations;
	TArray<TStrongObjectPtr<UBakedAnimKeyFrames> > BakedFramesPerLane;
	TArray<FString> Objects;
	bool bInvDirection = false;
	bool bIsLoop = false;
	bool isPlaying = false;
private:
	float lastBakingRequestTime = -1.f;

public:
	void Init(AITwinSplineHelper *InSplineHelper);

	// Update associated spline with current parameter values
	virtual void UpdateSpline();

	bool HasBakedAnimation() const;
	void InvalidateBakedAnimation();
	void BakeAnimationIfNeeded();

	float GetRoadWidth() const;

	//FString GetName() const { return Name; }
	//void SetName(const FString& InName) { Name = InName; }

	const TArray<FString>& Get3DObjects() const { return Objects; }
	void Set3DObjects(const TArray<FString>& AssetPaths) { Objects = AssetPaths; }

	bool HasInvDirection() const { return bInvDirection; }
	void SetInvDirection(bool bInInvDirection) { bInvDirection = bInInvDirection; }

	bool IsLoop() const { return bIsLoop; }
	void SetIsLoop(bool bInIsLoop) { bIsLoop = bInIsLoop; }

	virtual float GetSpeed() const { return 0.f; }
	virtual float GetLaneSpeed(int laneIdx) const { return GetSpeed(); }
	virtual void SetSpeed(float /*InSpeed*/) {}

	virtual float GetDelay() const { return 0.f; }
	virtual void SetDelay(float /*InDelay*/) {}

	virtual EITwinAnimPathRepeatMode GetRepeatMode() const { return EITwinAnimPathRepeatMode::Loop; }
	virtual void SetRepeatMode(EITwinAnimPathRepeatMode InRepeatMode) {}

	virtual bool IsOneWay() const { return true; }
	virtual void SetOneWay(bool /*bInOneWay*/) {}

	virtual int GetLaneCount() const { return 1; }
	virtual void SetLaneCount(int /*InLaneCount*/) {}
	
	virtual float GetLaneWidth() const { return 300.f; }
	virtual void SetLaneWidth(float /*InLaneWidth*/) {}
	
	virtual float GetDensity() const { return 0.f; }
	virtual void SetDensity(float /*InDensity*/) {}

	virtual float GetSeparatorWidth() const { return 0.f; }
	virtual void SetSeparatorWidth(float /*InSeparatorWidth*/) {}

	virtual float GetMinSpeed() const { return GetSpeed(); }
	virtual void SetMinSpeed(float /*InMinSpeed*/) {}
	
	virtual float GetMaxSpeed() const { return GetSpeed(); }
	virtual void SetMaxSpeed(float /*InMaxSpeed*/) {}
};

UCLASS()
class UITwinObjectAnimPathInfo : public UITwinAnimPathInfo
{
	GENERATED_BODY()
public:
	float Speed = 700.0; //in cm/s
	float Delay = 0.f;
	EITwinAnimPathRepeatMode RepeatMode = EITwinAnimPathRepeatMode::Loop;

	float GetSpeed() const override { return Speed; }
	void SetSpeed(float InSpeed) override;

	float GetDelay() const override { return Delay; }
	void SetDelay(float InDelay) override { Delay = InDelay; }

	EITwinAnimPathRepeatMode GetRepeatMode() const override { return RepeatMode; }
	void SetRepeatMode(EITwinAnimPathRepeatMode InRepeatMode) override { RepeatMode = InRepeatMode; }
};

UCLASS()
class UITwinCrowdAnimPathInfo : public UITwinAnimPathInfo
{
	GENERATED_BODY()
public:
	bool bOneWay = true;
	int LaneCount = 2;
	float LaneWidth = 300.f; // in cm
	float Density = 0.3f; // 30%

	// TODO: manage speed per lane?
	virtual float GetSpeed() const { return 300.f; }

	virtual bool IsOneWay() const { return bOneWay; }
	virtual void SetOneWay(bool bInOneWay) { bOneWay = bInOneWay; InvalidateBakedAnimation(); }

	virtual int GetLaneCount() const { return LaneCount; }
	virtual void SetLaneCount(int InLaneCount) { LaneCount = InLaneCount; InvalidateBakedAnimation(); }

	virtual float GetLaneWidth() const { return LaneWidth; }
	virtual void SetLaneWidth(float InLaneWidth) { LaneWidth = InLaneWidth; InvalidateBakedAnimation(); }

	// TODO: update population when density changes
	virtual float GetDensity() const { return Density; }
	virtual void SetDensity(float InDensity) { Density = InDensity; }

	virtual void UpdateSpline() override;
};

UCLASS()
class UITwinTrafficAnimPathInfo : public UITwinCrowdAnimPathInfo
{
	GENERATED_BODY()
public:
	float SeparatorWidth = 200.0f; // in cm
	float MinSpeed = 1389.0; //in cm/s (=50km/h)
	float MaxSpeed = 1389.0; //in cm/s (=50km/h)

	virtual float GetSeparatorWidth() const { return SeparatorWidth; }
	virtual void SetSeparatorWidth(float InSeparatorWidth) { SeparatorWidth  = InSeparatorWidth; InvalidateBakedAnimation(); }

	virtual float GetSpeed() const { return GetMaxSpeed(); }
	virtual float GetLaneSpeed(int laneIdx) const;
	virtual void SetSpeed(float InSpeed) { MinSpeed = InSpeed; MaxSpeed = InSpeed; }

	virtual float GetMinSpeed() const { return MinSpeed; }
	virtual void SetMinSpeed(float InMinSpeed) { MinSpeed = InMinSpeed; }

	virtual float GetMaxSpeed() const { return MaxSpeed; }
	virtual void SetMaxSpeed(float InMaxSpeed) { MaxSpeed = InMaxSpeed; }

	virtual void UpdateSpline() override;
};
