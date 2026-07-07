/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPopulationEnums.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <UObject/ObjectMacros.h>
#include <Engine/Blueprint.h>
#include <Misc/EnumRange.h>

UENUM(BlueprintType)
enum class EITwinInstantiatedObjectType : uint8
{
	Vehicle = 0,
	Vegetation,
	Character,
	ClippingPlane,
	ClippingBox,
	Crane,

	Other,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EITwinInstantiatedObjectType, EITwinInstantiatedObjectType::Count);
