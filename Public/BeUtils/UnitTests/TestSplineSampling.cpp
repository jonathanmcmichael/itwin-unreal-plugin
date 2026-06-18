/*--------------------------------------------------------------------------------------+
|
|     $Source: TestSplineSampling.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <catch2/catch_all.hpp>
#include <BeUtils/SplineSampling/SplineSampling.h>


class ParabolaSplineCurve final : public BeUtils::SplineCurve
{
public:
	ParabolaSplineCurve() {}

	glm::dvec3 GetPositionAtCoord(value_type const& u) const override
	{
		return glm::dvec3(
			u,
			2.* u * u - 1.,
			5.);
	}
	glm::dvec3 GetTangentAtCoord(value_type const& u) const override
	{
		// Not used for test, can return anything.
		return glm::dvec3(1., u, 0.);
	}
	size_t PointCount(const bool /*accountForCyclicity*/) const override { return 2; }
	glm::dvec3 GetPositionAtIndex(size_t idx) const override
	{
		if (idx == 0)
			return glm::dvec3(0., -1., 5.);
		else if (idx == 1)
			return glm::dvec3(1., 1., 5.);
		else
		{
			BE_ISSUE("out of range", idx);
			return glm::dvec3(0.);
		}
	}
	bool IsCyclic() const { return false; }

	static glm::dvec2 GetParabolaPosition(double u)
	{
		return glm::dvec2(
			u,
			2. * u * u - 1);
	}
};

static inline glm::dvec3 GetProjectedPosition(double u)
{
	return glm::dvec3(ParabolaSplineCurve::GetParabolaPosition(u), 0.);
}

static void SampleTestSplineStroke(
	std::vector<glm::dvec3>& outSampledPositions,
	std::optional<uint32_t> const& fixedNbInstances,
	std::optional<double> const& fixedSpacing = std::nullopt,
	std::optional<double> const& optYBound = std::nullopt)
{
	ParabolaSplineCurve const testCurve;

	BeUtils::SplineSamplingParameters samplingParams;
	samplingParams.samplingMode = BeUtils::ESplineSamplingMode::AlongPath;
	samplingParams.fixedNbInstances = fixedNbInstances;
	if (fixedSpacing)
	{
		samplingParams.fixedSpacing = glm::dvec2(*fixedSpacing, 0.);
	}

	BeUtils::TransformHolder const identityTsf;

	static constexpr double InvalidProjectionValue = -100000.;

	/// Adapts a 3D world-to-screen projector (like FScreenSpaceProjector) to the Spline2DProjector interface
	/// expected by BeUtils::SampleSplinePath.
	class Test2DProjector final : public BeUtils::Spline2DProjector
	{
	public:
		Test2DProjector(std::optional<double> const& inOptYBound)
			: optYBound_(inOptYBound)
		{ }

		bool MayFail() const override { return optYBound_.has_value(); }

		std::optional<vec2_type> Project2DOpt(vec3_type const& pos) const override
		{
			if (optYBound_ && pos.y < *optYBound_)
			{
				return std::nullopt;
			}
			else
			{
				return vec2_type(pos.x, pos.y);
			}
		}

		std::optional<vec3_type> Project3DOpt(vec3_type const& pos) const override
		{
			std::optional<vec2_type> Proj2D = Project2DOpt(pos);
			if (Proj2D)
			{
				return vec3_type(*Proj2D, 0.);
			}
			else
			{
				return std::nullopt;
			}
		}

		vec2_type Project2D(vec3_type const& pos) const override
		{
			return Project2DOpt(pos).value_or(vec2_type(InvalidProjectionValue, InvalidProjectionValue));
		}

		vec3_type Project3D(vec3_type const& pos) const override
		{
			return vec3_type(Project2D(pos), 0.);
		}

		BeUtils::E2DProjection GetProjection() const override
		{
			return BeUtils::E2DProjection::Z_Axis;
		}

	private:
		// If provided, this will cause the projector to fail for any point with a y coordinate below the
		// given bound. This allows testing how SampleSplinePath handles projection failures.
		const std::optional<double> optYBound_;
		const bool withProjectionFailure_ = false;
	};

	samplingParams.customProjector = std::make_unique<Test2DProjector>(optYBound);

	outSampledPositions.clear();
	BeUtils::SampleSplinePath(testCurve, identityTsf, samplingParams, outSampledPositions);
}

static void CheckSampling(std::vector<glm::dvec3> const& sampledPositions,
	double x_start, double x_end,
	double toleranceForXbounds = 1e-8,
	std::optional<double> const& y_bound = std::nullopt)
{
	REQUIRE(sampledPositions.size() > 0);
	CHECK(std::fabs(sampledPositions.front().x - x_start) < toleranceForXbounds);
	double prevU = x_start - toleranceForXbounds;
	for (const auto& pos : sampledPositions)
	{
		if (y_bound)
		{
			CHECK(pos.y >= *y_bound); // check the effect of the projection failure bound
		}
		CHECK(glm::distance(pos, GetProjectedPosition(pos.x)) < 1e-6);
		CHECK(pos.x > prevU);
		CHECK(pos.x <= x_end);
		prevU = pos.x;
	}
	CHECK(std::fabs(sampledPositions.back().x - x_end) < toleranceForXbounds);
}

TEST_CASE("TestStroke_FixedNbSamples")
{
	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineStroke(sampledPositions, 16);
		REQUIRE(sampledPositions.size() == 16);
		CheckSampling(sampledPositions, 0., 1.);
		CHECK(glm::distance(sampledPositions.front(), glm::dvec3(0., -1., 0.)) < 1e-6);
		CHECK(glm::distance(sampledPositions[4], GetProjectedPosition(0.4465408805)) < 1e-6);
		CHECK(glm::distance(sampledPositions[10], GetProjectedPosition(0.7924528301886)) < 1e-6);
		CHECK(glm::distance(sampledPositions.back(), glm::dvec3(1., 1., 0.)) < 1e-6);
	}
	{
		// Test with just 2 samples to ensure that the code correctly handles the case where the fixed number
		// of instances equals the number of points in the curve.
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineStroke(sampledPositions, 2);
		REQUIRE(sampledPositions.size() == 2);
		CHECK(glm::distance(sampledPositions.front(), glm::dvec3(0., -1., 0.)) < 1e-6);
		CHECK(glm::distance(sampledPositions.back(), glm::dvec3(1., 1., 0.)) < 1e-6);
	}
}

TEST_CASE("TestStroke_FixedSpacing")
{
	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineStroke(sampledPositions, std::nullopt, 0.05);
		REQUIRE(sampledPositions.size() == 47);
		CheckSampling(sampledPositions, 0., 1., 1e-2);
		CHECK(glm::distance(sampledPositions.front(), glm::dvec3(0., -1., 0.)) < 1e-6);
		CHECK(glm::distance(sampledPositions[4], GetProjectedPosition(0.185185185)) < 1e-6);
		CHECK(glm::distance(sampledPositions[10], GetProjectedPosition(0.38344226579)) < 1e-6);
		CHECK(glm::distance(sampledPositions[37], GetProjectedPosition(0.880174291939)) < 1e-6);
	}
	{
		// Test with a too large spacing => this should result in only one point (start).
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineStroke(sampledPositions, std::nullopt, 5.0);
		REQUIRE(sampledPositions.size() == 1);
		CHECK(glm::distance(sampledPositions.front(), glm::dvec3(0., -1., 0.)) < 1e-6);
	}
}

TEST_CASE("TestStroke_FixedNbSamples_WithProjectionFailure")
{
	{
		std::vector<glm::dvec3> sampledPositions;
		const double yBound = 0.;
		SampleTestSplineStroke(sampledPositions, 16, std::nullopt, yBound);
		REQUIRE(sampledPositions.size() == 16);

		// the first point should be close to that at u=0.71, (~ sqrt(2) / 2) which is the first point on the
		// curve that has a y coordinate above 0.
		CheckSampling(sampledPositions, 0.71, 1., 1e-2, yBound);
	}
	{
		// Test what happens when all points fail to project: in this case, the code should return an empty
		// vector.
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineStroke(sampledPositions, 64, std::nullopt, 50.);
		REQUIRE(sampledPositions.empty());
	}
	{
		// If the bound does not filter out anything, we should get the same result as with no failure.
		std::vector<glm::dvec3> sampledPositions;
		const double yBound = -50.;
		SampleTestSplineStroke(sampledPositions, 16, std::nullopt, yBound);
		REQUIRE(sampledPositions.size() == 16);
		CheckSampling(sampledPositions, 0., 1., 1e-8, yBound);
	}

}

