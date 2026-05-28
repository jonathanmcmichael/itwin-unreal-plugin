/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAnimPathHelper.h $
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
#   include <SDK/Core/Visualization/PathAnimation.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <memory>
#include <optional>

#include "ITwinAnimPathHelper.generated.h"

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
class UITwinAnimPathHelper : public UObject
{
	GENERATED_BODY()

public:
	virtual ~UITwinAnimPathHelper() = default;

	TWeakObjectPtr<AITwinSplineHelper> SplineHelper;
	TSet<TWeakObjectPtr<AITwinPopulation> > Populations;

protected:
	struct FImpl;
	TPimplPtr<FImpl> Impl;

public:
	void Init(AITwinSplineHelper* InSplineHelper, AdvViz::SDK::IAnimationPathInfoPtr InPathProp);

	AdvViz::SDK::RefID GetPathRefID() const;
	AdvViz::SDK::RefID GetSplineRefID() const;
	//AdvViz::SDK::RefID GetInstanceGroupRefID() const;

	// Update associated spline with current parameter values
	virtual void UpdateSpline();

	UBakedAnimKeyFrames* GetBakedFrames(int laneIdx = 0);
	bool HasBakedAnimation() const;
	void InvalidateBakedAnimation();
	void BakeAnimationIfNeeded();

	float GetRoadWidth() const;

	// Parameters common to all path animation types (object, crowd, traffic)

	// Return paths of the 3D objects associated to this animation path, if any
	const TArray<FString>& Get3DObjectPaths() const;
	// Return full description (server id + name + path) of the 3D objects associated to this animation path, if any
	void Get3DObjects(TArray<FString>& Assets) const;
	// Set full description of the 3D objects associated to this animation path
	void Set3DObjects(const TArray<FString>& Assets);
	// Set only paths of the 3D objects associated to this animation path
	void Set3DObjectsFromProps();

	bool HasInvDirection() const;
	void SetInvDirection(bool bInInvDirection);

	bool IsLoop() const;
	void SetIsLoop(bool bInIsLoop);

	// Other parameters that may not be relevant for all path animation types
	virtual float GetLaneSpeed(int /*laneIdx*/) const { return GetSpeed(); }
	virtual float GetSpeed() const { return 0.f; }
	virtual void SetSpeed(float /*InSpeed*/) {}

	virtual float GetDelay() const { return 0.f; }
	virtual void SetDelay(float /*InDelay*/) {}

	virtual EITwinAnimPathRepeatMode GetRepeatMode() const { return EITwinAnimPathRepeatMode::Loop; }
	virtual void SetRepeatMode(EITwinAnimPathRepeatMode /*InRepeatMode*/) {}

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
class UITwinObjectAnimPathHelper : public UITwinAnimPathHelper
{
	GENERATED_BODY()
public:
	float GetSpeed() const override;
	void SetSpeed(float InSpeed) override;

	float GetDelay() const override;
	void SetDelay(float InDelay) override;

	EITwinAnimPathRepeatMode GetRepeatMode() const override;
	void SetRepeatMode(EITwinAnimPathRepeatMode InRepeatMode) override;
};

UCLASS()
class UITwinCrowdAnimPathHelper : public UITwinAnimPathHelper
{
	GENERATED_BODY()
public:
	virtual float GetSpeed() const override;

	bool IsOneWay() const override;
	void SetOneWay(bool bInOneWay) override;

	int GetLaneCount() const override;
	void SetLaneCount(int InLaneCount) override;

	float GetLaneWidth() const override;
	void SetLaneWidth(float InLaneWidth) override;

	float GetDensity() const override;
	void SetDensity(float InDensity) override;

	virtual void UpdateSpline() override;
};

UCLASS()
class UITwinTrafficAnimPathHelper : public UITwinCrowdAnimPathHelper
{
	GENERATED_BODY()
public:
	float GetSeparatorWidth() const override;
	void SetSeparatorWidth(float InSeparatorWidth) override;

	float GetLaneSpeed(int laneIdx) const override;
	float GetSpeed() const override;
	void SetSpeed(float InSpeed) override;

	float GetMinSpeed() const override;
	void SetMinSpeed(float InMinSpeed) override;

	float GetMaxSpeed() const override;
	void SetMaxSpeed(float InMaxSpeed) override;
};
