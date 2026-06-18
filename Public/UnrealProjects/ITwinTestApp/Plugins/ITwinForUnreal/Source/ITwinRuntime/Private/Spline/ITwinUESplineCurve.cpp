/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinUESplineCurve.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Spline/ITwinUESplineCurve.h>

#include <Components/SplineComponent.h>

// -------------------------------- FITwinUESplineCurve --------------------------------

FITwinUESplineCurve::FITwinUESplineCurve(USplineComponent const& InSpline)
	: UESpline(InSpline)
{
}

glm::dvec3 FITwinUESplineCurve::GetPositionAtCoord(value_type const& u) const
{
	// Directly work in world coordinates
	const float SplineTime = u * UESpline.Duration;
	auto const Pos_World = UESpline.GetLocationAtTime(SplineTime, ESplineCoordinateSpace::World);
	return {
		Pos_World.X,
		Pos_World.Y,
		Pos_World.Z
	};
}

glm::dvec3 FITwinUESplineCurve::GetTangentAtCoord(value_type const& u) const
{
	const float SplineTime = u * UESpline.Duration;
	auto const Tgte_World = UESpline.GetTangentAtTime(SplineTime, ESplineCoordinateSpace::World);
	return {
		Tgte_World.X,
		Tgte_World.Y,
		Tgte_World.Z
	};
}

size_t FITwinUESplineCurve::PointCount(const bool /*accountForCyclicity*/) const
{
	return static_cast<size_t>(UESpline.GetNumberOfSplinePoints());
}

glm::dvec3 FITwinUESplineCurve::GetPositionAtIndex(size_t idx) const
{
	// Directly work in world coordinates
	auto const Pos_World = UESpline.GetLocationAtSplinePoint(static_cast<int32>(idx),
		ESplineCoordinateSpace::World);
	return {
		Pos_World.X,
		Pos_World.Y,
		Pos_World.Z
	};
}

bool FITwinUESplineCurve::IsCyclic() const
{
	return UESpline.IsClosedLoop();
}


// -------------------------------- FITwinUESplineCurve --------------------------------

FITwinUESplineChunkCurve::FITwinUESplineChunkCurve(USplineComponent const& InSpline, int32 InChunkIndex)
	: UESpline(InSpline)
	, StartInputKey(static_cast<float>(InChunkIndex))
{

}

glm::dvec3 FITwinUESplineChunkCurve::GetPositionAtCoord(value_type const& u) const
{
	auto const Pos_World = UESpline.GetLocationAtSplineInputKey(StartInputKey + u, ESplineCoordinateSpace::World);
	return {
		Pos_World.X,
		Pos_World.Y,
		Pos_World.Z
	};
}

glm::dvec3 FITwinUESplineChunkCurve::GetTangentAtCoord(value_type const& u) const
{
	auto const Tgte_World = UESpline.GetTangentAtSplineInputKey(StartInputKey + u, ESplineCoordinateSpace::World);
	return {
		Tgte_World.X,
		Tgte_World.Y,
		Tgte_World.Z
	};
}

size_t FITwinUESplineChunkCurve::PointCount(const bool /*accountForCyclicity*/) const
{
	return 2; // A chunk is defined by 2 control points
}

glm::dvec3 FITwinUESplineChunkCurve::GetPositionAtIndex(size_t idx) const
{
	return GetPositionAtCoord(static_cast<value_type>(idx));
}

bool FITwinUESplineChunkCurve::IsCyclic() const
{
	return UESpline.IsClosedLoop();
}
