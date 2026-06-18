/*--------------------------------------------------------------------------------------+
|
|     $Source: SplineSampling.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "SplineSampling.h"

#include "OcclusionMap.h"
#include "SplineHelper.h"
#include "SplinePattern.h"


namespace BeUtils
{
	Spline2DProjector::~Spline2DProjector()
	{

	}

	std::optional<Spline2DProjector::vec2_type> Spline2DProjector::Project2DOpt(vec3_type const& pos) const
	{
		BE_ISSUE("when overriding MayFail, please override Project2DOpt!");
		return Project2D(pos);
	}

	std::optional<Spline2DProjector::vec3_type> Spline2DProjector::Project3DOpt(vec3_type const& pos) const
	{
		BE_ISSUE("when overriding MayFail, please override Project3DOpt!");
		return Project3D(pos);
	}

	void SampleSplineInterior(SplineCurve const& spline,
		TransformHolder const& transform,
		BoundingBox const& samplingBox_World,
		glm::dvec3 const& averageInstanceDims_World,
		SplineSamplingParameters const& params,
		std::vector<SplineCurve::vector_type>& outPositions)
	{
		BE_ASSERT(params.samplingMode == ESplineSamplingMode::Interior);

		if (!IsInitialized(samplingBox_World))
		{
			BE_ISSUE("invalid sampling box");
			return;
		}
		if (averageInstanceDims_World.x <= 0. || averageInstanceDims_World.y <= 0.)
		{
			BE_ISSUE("invalid mean instance dimension");
			return;
		}
		auto const boxDims = GetBoxDimensions(samplingBox_World);
		const double areaToPopulate = boxDims.x * boxDims.y;
		const double objAvgSurface = averageInstanceDims_World.x * averageInstanceDims_World.y;
		const double objAvgLengthWidthRatio = averageInstanceDims_World.y / averageInstanceDims_World.x;

		int nInstances = static_cast<int>(ceil(areaToPopulate / objAvgSurface));

		// compute number of instances for current density
		float popDensity = params.density;
		popDensity *= 100.0f;
		nInstances *= (int)(popDensity * popDensity);
		nInstances /= (100 * 100);

		// compute cellsAlongX and cellsAlongY
		// we assume here that the objects are right next to each other
		double cellSizeX = std::sqrt(areaToPopulate / nInstances);
		double cellSizeY = cellSizeX * objAvgLengthWidthRatio;
		if (params.forceAligned)
		{
			cellSizeX = params.fixedSpacing ? params.fixedSpacing.value().x : averageInstanceDims_World.x;
			cellSizeY = params.fixedSpacing ? params.fixedSpacing.value().y : averageInstanceDims_World.y;
		}
		const int cellsAlongX = static_cast<int>(std::ceil(boxDims.x / cellSizeX));
		const int cellsAlongY = static_cast<int>(std::ceil(boxDims.y / cellSizeY));

		OcclusionMap surfaceGrid(samplingBox_World, cellsAlongX, cellsAlongY);

		SplineHelper splineHelper(&spline);
		SplinePattern spline2DEffect(transform, splineHelper);
		spline2DEffect.SetOcclusion(false);
		surfaceGrid.BuildFrom2DPattern(spline2DEffect);

		surfaceGrid.GetSampledPositions(outPositions, params.forceAligned, params.randSeed);
	}

	void SampleSplinePath(SplineCurve const& spline,
		TransformHolder const& transform,
		SplineSamplingParameters const& params,
		std::vector<SplineCurve::vector_type>& outPositions)
	{
		BE_ASSERT(params.samplingMode == ESplineSamplingMode::AlongPath);

		SplineHelper const splineHelper(&spline);
		SplineHelper::EPathRegularSamplingMode const mode = params.fixedSpacing
			? SplineHelper::EPathRegularSamplingMode::FixedSpacing
			: SplineHelper::EPathRegularSamplingMode::FixedNbSamples;
		std::variant<size_t, double> fixedCountOrDistance;
		if (params.fixedSpacing)
		{
			fixedCountOrDistance = params.fixedSpacing->x;
		}
		else if (params.fixedNbInstances)
		{
			fixedCountOrDistance = *params.fixedNbInstances;
		}
		else
		{
			BE_ISSUE("invalid sampling parameters");
			return;
		}
		Basic2DProjector const defaultProjector(E2DProjection::Z_Axis);
		Spline2DProjector const* pProjector = params.customProjector.get();
		if (!pProjector)
		{
			pProjector = &defaultProjector;
		}
		splineHelper.GetRegularSamples(outPositions, mode, fixedCountOrDistance,
			transform,
			*pProjector);
	}

	void SampleSpline(SplineCurve const& spline,
		TransformHolder const& transform,
		BoundingBox const& samplingBox_World,
		glm::dvec3 const& averageInstanceDims_World,
		SplineSamplingParameters const& params,
		std::vector<SplineCurve::vector_type>& outPositions)
	{
		if (params.samplingMode == ESplineSamplingMode::Interior)
		{
			SampleSplineInterior(spline, transform, samplingBox_World, averageInstanceDims_World, params, outPositions);
		}
		else
		{
			SampleSplinePath(spline, transform, params, outPositions);
		}
	}
}
