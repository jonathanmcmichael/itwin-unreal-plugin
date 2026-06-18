/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinViewProjectionState.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"

/// Stores the view and projection state of an Unreal view, for comparison purposes (to avoid unnecessary
/// updates when the view/projection hasn't changed significantly).
struct FITwinViewProjectionState
{
	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	FVector2D ViewportSize = FVector2D::ZeroVector;
	float ViewportScale = 1.0f;
	float FOV = 90.0f;
	bool bIsOrthographic = false;
	float OrthoWidth = 0.0f;

	bool NearlyEquals(const FITwinViewProjectionState& Other) const
	{
		return ViewLocation.Equals(Other.ViewLocation, 0.1)
			&& ViewRotation.Equals(Other.ViewRotation, 0.01f)
			&& ViewportSize.Equals(Other.ViewportSize, 0.5f)
			&& FMath::IsNearlyEqual(ViewportScale, Other.ViewportScale, KINDA_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(FOV, Other.FOV, 0.01f)
			&& bIsOrthographic == Other.bIsOrthographic
			&& FMath::IsNearlyEqual(OrthoWidth, Other.OrthoWidth, 0.01f);
	}
};

class UWorld;

namespace ITwin
{
	bool GetViewProjectionState(const UWorld* World, FITwinViewProjectionState& OutState);
}
