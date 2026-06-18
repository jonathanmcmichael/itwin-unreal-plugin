/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinUESplineCurve.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <BeUtils/SplineSampling/SplineSampling.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

class USplineComponent;

/// Adapts an Unreal Engine Spline Component to the SplineCurve interface expected by BeUtils::SampleSpline.
class FITwinUESplineCurve final : public BeUtils::SplineCurve
{
public:
	FITwinUESplineCurve(USplineComponent const& InSpline);
	virtual glm::dvec3 GetPositionAtCoord(value_type const& u) const override;
	virtual glm::dvec3 GetTangentAtCoord(value_type const& u) const override;
	virtual size_t PointCount(const bool /*accountForCyclicity*/) const override;
	virtual glm::dvec3 GetPositionAtIndex(size_t idx) const override;
	virtual bool IsCyclic() const override;

private:
	USplineComponent const& UESpline;
};

/// Adapter for a spline chunk (segment of a spline between two consecutive control points) to the
/// SplineCurve interface expected by BeUtils::SampleSpline.
class FITwinUESplineChunkCurve final : public BeUtils::SplineCurve
{
public:
	FITwinUESplineChunkCurve(USplineComponent const& InSpline, int32 InChunkIndex);
	virtual glm::dvec3 GetPositionAtCoord(value_type const& u) const override;
	virtual glm::dvec3 GetTangentAtCoord(value_type const& u) const override;
	virtual size_t PointCount(const bool /*accountForCyclicity*/) const override;
	virtual glm::dvec3 GetPositionAtIndex(size_t idx) const override;
	virtual bool IsCyclic() const override;

private:
	USplineComponent const& UESpline;
	float const StartInputKey;
};

