/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingEventHub.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Clipping/ITwinClippingEnums.h>
#include <GameFramework/Actor.h>

#include "ITwinClippingEventHub.generated.h"

struct FFeatureEventProperties;

/// Base class for the ClippingTool, providing with a delegates allowing to connect some events in the
/// lifetime of cutouts.
UCLASS()
class ITWINRUNTIME_API AITwinClippingEventHub : public AActor
{
	GENERATED_BODY()
public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEffectListModifiedEvent);
	UPROPERTY()
	FEffectListModifiedEvent EffectListModifiedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEffectAddedEvent, EITwinClippingPrimitiveType, EffectType, int32, EffectIndex);
	UPROPERTY()
	FEffectAddedEvent EffectAddedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRemoveEffectStartedEvent);
	UPROPERTY()
	FRemoveEffectStartedEvent RemoveEffectStartedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRemoveEffectCompletedEvent);
	UPROPERTY()
	FRemoveEffectCompletedEvent RemoveEffectCompletedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEffectRemovedEvent, EITwinClippingPrimitiveType, EffectType, int32, EffectIndex, bool, bTriggeredFromITS);
	UPROPERTY()
	FEffectRemovedEvent EffectRemovedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEffectSelectedEvent, EITwinClippingPrimitiveType, EffectType, int32, EffectIndex);
	UPROPERTY()
	FEffectSelectedEvent EffectSelectedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSplinePointSelectedEvent);
	UPROPERTY()
	FSplinePointSelectedEvent SplinePointSelectedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSplinePointMovedEvent, bool, bMovedInITS);
	UPROPERTY()
	FSplinePointMovedEvent SplinePointMovedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FActivationEvent, bool, bActivated);
	UPROPERTY()
	FActivationEvent ActivationEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FInteractiveCreationAbortedEvent, bool, bTriggeredFromITS);
	UPROPERTY()
	FInteractiveCreationAbortedEvent InteractiveCreationAbortedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEffectModifiedEvent, EITwinClippingPrimitiveType, EffectType, int32, EffectIndex, bool, bTriggeredFromITS);
	UPROPERTY()
	FEffectModifiedEvent EffectModifiedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FClippingModifiedEvent, const FFeatureEventProperties&, Properties);
	UPROPERTY()
	FClippingModifiedEvent ClippingModifiedEvent;

	virtual void BroadcastSelection() {}
};
