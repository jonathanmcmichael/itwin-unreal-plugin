/*--------------------------------------------------------------------------------------+
|
|     $Source: Spline2DProjector.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <BeUtils/SplineSampling/SplineDefines.h>


// MUST be included before EnumSwitchCoverage...
#include <SDK/Core/Tools/Assert.h>
#include <BeHeaders/Compil/EnumSwitchCoverage.h>
#include <optional>


namespace BeUtils
{

	/// Performs a projection from a 3D space to a 2D space.
	class Spline2DProjector
	{
	public:
		using vec2_type = glm::dvec2;
		using vec3_type = glm::dvec3;

		Spline2DProjector() = default;
		virtual ~Spline2DProjector();

		virtual vec2_type Project2D(vec3_type const& pos) const = 0;
		virtual vec3_type Project3D(vec3_type const& pos) const = 0;
		virtual E2DProjection GetProjection() const = 0;

		/// Some projection modes may fail. In such case, the methods Project2DOpt / Project3DOpt should be
		/// overridden.
		virtual bool MayFail() const { return false; }
		virtual std::optional<vec2_type> Project2DOpt(vec3_type const& pos) const;
		virtual std::optional<vec3_type> Project3DOpt(vec3_type const& pos) const;
	};


	/// Performs a basic projection along a given axis.
	class Basic2DProjector final : public Spline2DProjector
	{
	public:
		E2DProjection const proj_;

		Basic2DProjector(E2DProjection proj)
			: proj_(proj)
		{
		}

		E2DProjection GetProjection() const final { return proj_; }

		vec2_type Project2D(vec3_type const& pos) const final
		{
			switch (proj_)
			{
			BE_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH(case E2DProjection::None:)
			case E2DProjection::X_Axis: return vec2_type(pos.y, pos.z);
			case E2DProjection::Y_Axis: return vec2_type(pos.z, pos.x);
			case E2DProjection::Z_Axis: return vec2_type(pos.x, pos.y);
			}
		}

		[[nodiscard]] vec3_type Project3D(vec3_type const& pos) const final
		{
			switch (proj_)
			{
			case E2DProjection::X_Axis: return vec3_type(0.0, pos.y, pos.z);
			case E2DProjection::Y_Axis: return vec3_type(pos.x, 0.0, pos.z);
			case E2DProjection::Z_Axis: return vec3_type(pos.x, pos.y, 0.0);
			case E2DProjection::None: return pos;
				BE_NO_UNCOVERED_ENUM_ASSERT_AND_RETURN(pos);
			}
		}
	};
}
