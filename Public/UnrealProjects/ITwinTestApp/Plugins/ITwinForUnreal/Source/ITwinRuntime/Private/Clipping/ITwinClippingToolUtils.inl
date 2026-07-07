/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingToolUtils.inl $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Population/ITwinPopulation.h>

namespace ITwinClippingToolUtils
{
	// Get the plane equation (normal and W) from the transform of a given instance in the ClippingPlanePopulation.
	template <typename T>
	void GetPlaneEquationFromTransform(UE::Math::TVector<T>& OutPlaneOrientation, T& OutPlaneW,
		const FTransform& Transform)
	{
		auto const PositionUE = Transform.GetLocation();
		auto const PlaneOrientationUE = Transform.GetUnitAxis(EAxis::Z); // GetUpVector
		OutPlaneOrientation = UE::Math::TVector<T>(PlaneOrientationUE);
		OutPlaneW = static_cast<T>(PositionUE.Dot(PlaneOrientationUE));
	}

	// Get the plane equation (normal and W) from the transform of a given instance in the ClippingPlanePopulation.
	template <typename T>
	bool GetPlaneEquationFromUEInstance(UE::Math::TVector<T>& OutPlaneOrientation, T& OutPlaneW,
		TWeakObjectPtr<AITwinPopulation> const& PlanePopulation,
		int32 InInstanceIndex)
	{
		if (!ensure(PlanePopulation.IsValid()))
		{
			return false;
		}
		if (!ensure(InInstanceIndex >= 0
				 && InInstanceIndex < PlanePopulation->GetNumberOfInstances()))
		{
			return false;
		}
		const FTransform InstanceTransform = PlanePopulation->GetInstanceTransform(InInstanceIndex);
		GetPlaneEquationFromTransform<T>(OutPlaneOrientation, OutPlaneW, InstanceTransform);
		return true;
	}
}
