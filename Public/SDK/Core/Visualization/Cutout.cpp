/*--------------------------------------------------------------------------------------+
|
|     $Source: Cutout.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Cutout.h"

#include "ConstantIDs.h"
#include "ScenePersistence.h"
#include "SplinesManager.h"


namespace AdvViz::SDK
{
	 std::string FindModelTypeFromLayerId(IScenePersistence const& Scene,
		std::string const& LayerId)
	{
		if (LayerId == ADVVIZ_GOOGLE_LAYER_ID)
		{
			return "GlobalMapLayer";
		}
		for (auto const& Link : Scene.GetLinks())
		{
			if (Link->GetRef() == LayerId)
			{
				return Link->GetType();
			}
		}
		BE_LOGW("ITwinDecoration", "No link found for layer " << LayerId);
		return "";
	}

	ISplinePtr ConvertCutoutToSpline(Cutout const& Cutout,
		RefID const& RefId,
		IScenePersistence const& Scene,
		ISplinesManager& SplinesManager)
	{
		if (Cutout.cutoutType != ECutoutType::Polygons)
			return {};

		auto const* PolygonSet = std::get_if<CutoutPolygonSet>(&Cutout.cutoutData);
		if (!PolygonSet || PolygonSet->polygons.empty())
			return {};

		ISplinePtr SplinePtr = SplinesManager.AddSpline();
		auto Spline = SplinePtr->GetAutoLock();
		Spline->SetId(RefId);
		Spline->SetUsage(ESplineUsage::MapCutout);

		std::vector<SplineLinkedModel> AdvVizLinkedModels;
		for (auto const& LayerId : Cutout.appliesTo)
		{
			auto const LayerType = FindModelTypeFromLayerId(Scene, LayerId);
			if (LayerType != "")
			{
				AdvVizLinkedModels.push_back({
					LayerType,
					LayerId
				});
			}
		}
		Spline->SetLinkedModels(AdvVizLinkedModels);

		Spline->SetInvertEffect(Cutout.inverse);
		Spline->EnableEffect(Cutout.enabled);
		Spline->SetClosedLoop(true);

		dmat3x4 transform3x4;
		if (PolygonSet->transformFromClip)
		{
			auto const& transform4x4 = PolygonSet->transformFromClip.value();
			std::memcpy(transform3x4.data(), transform4x4.data(), 12 * sizeof(double));
		}
		else
		{
			// If no transform is provided, use the identity.
			transform3x4.fill(0.0);
			transform3x4[0] = transform3x4[5] = transform3x4[10] = 1.0;
		}
		Spline->SetTransform(transform3x4);

		// Current spline persistence supports a single polygon per spline.
		// If several polygons are present, convert the first one only.
		for (auto const& Position : PolygonSet->polygons.front().positions)
		{
			auto Point = Spline->AddPoint()->GetAutoLock();
			Point->SetPosition(Position);
			// For now, we don't set the tangent and up-vector for each point: they should be computed by the
			// client application.
		}

		return SplinePtr;
	}

	bool ConvertSplineToCutout(ISplinePtr const& splinePtr, Cutout& cutout)
	{
		cutout.cutoutType = ECutoutType::Polygons;
		cutout.appliesTo.clear();

		CutoutPolygonSet PolygonSet;

		if (!splinePtr)
		{
			cutout.inverse = cutout.enabled = false;
			cutout.cutoutData = std::move(PolygonSet);
			return false;
		}

		auto const Spline = splinePtr->GetRAutoLock();

		cutout.enabled = Spline->IsEnabledEffect();
		cutout.inverse = Spline->GetInvertEffect();

		for (auto const& LinkedModel : Spline->GetLinkedModels())
		{
			cutout.appliesTo.push_back(LinkedModel.modelId);
		}

		auto const& transform3x4 = Spline->GetTransform();
		auto& transform4x4 = PolygonSet.transformFromClip.emplace();
		std::memcpy(transform4x4.data(), transform3x4.data(), 12 * sizeof(double));
		transform4x4[12] = 0.0;
		transform4x4[13] = 0.0;
		transform4x4[14] = 0.0;
		transform4x4[15] = 1.0;

		CutoutPolygon Polygon;
		Polygon.positions.reserve(Spline->GetPoints().size());
		for (auto const& PointPtr : Spline->GetPoints())
		{
			if (!PointPtr)
				continue;

			auto Point = PointPtr->GetAutoLock();
			Polygon.positions.push_back(Point->GetPosition());
		}

		if (!Polygon.positions.empty())
		{
			PolygonSet.polygons.push_back(std::move(Polygon));
		}

		cutout.cutoutData = std::move(PolygonSet);

		return true;
	}

}
