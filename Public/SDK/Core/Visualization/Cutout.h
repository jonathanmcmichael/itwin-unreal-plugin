/*--------------------------------------------------------------------------------------+
|
|     $Source: Cutout.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#ifndef SDK_CPPMODULES
	#include <variant>
	#include <vector>
	#ifndef MODULE_EXPORT
		#define MODULE_EXPORT
	#endif // !MODULE_EXPORT
#endif

#include <Core/Tools/Types.h>


MODULE_EXPORT namespace AdvViz::SDK
{
	/// Types of cutouts, defining the way the cutout should be applied on the scene.
	/// See https://github.com/iTwin/scenes/blob/main/packages/scenesApi/src/domain/schemas/core/Cutout/Cutout.2.0.0.json
	/// The conversion to JSON is handled in ScenePersistenceAPI.cpp.

	enum class ECutoutType : uint8_t
	{
		Box,
		Plane,
		Polygons,
		ENUM_END
	};

	struct CutoutBox
	{
		double3 center = { 0., 0., 0. };
		double3 halfExtents = { 1., 1., 1. };
		double4 rotation = { 0., 0., 0., 1. };

		std::optional<std::array<double, 16>> transformFromClip;
	};

	struct CutoutPlane
	{
		// Inward unit normal vector. Points toward the region that should be kept when clipping.
		double3 normal = { 0., 0., 1. };
		// Signed distance from origin to plane.
		double distance = 0.;
	};

	struct CutoutPolygon
	{
		std::vector<double3> positions;
	};
	struct CutoutPolygonSet
	{
		std::vector<CutoutPolygon> polygons;

		std::optional<std::array<double, 16>> transformFromClip;
	};

	/// Holds common properties for all cutouts, independently of the type.
	struct CutoutBase
	{
		ECutoutType cutoutType = ECutoutType::ENUM_END;

		std::vector<std::string> appliesTo;
		bool enabled = true;
		bool inverse = false;
	};

	/// This structure is very close to what we export as Json for cutouts in ScenePersistenceAPI, but it
	/// is important to maintain it as a separate structure, so that we don't have to change it when the
	/// scene API is modified (or if we handle persistence through another service).
	struct Cutout : public CutoutBase
	{
		std::variant<CutoutBox, CutoutPlane, CutoutPolygonSet> cutoutData;
	};

}
