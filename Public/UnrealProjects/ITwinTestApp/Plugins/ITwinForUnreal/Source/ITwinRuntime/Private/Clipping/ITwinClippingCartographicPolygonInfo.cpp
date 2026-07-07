/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingCartographicPolygonInfo.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Clipping/ITwinClippingCartographicPolygonInfo.h>

#include <Spline/ITwinSplineHelper.h>

//---------------------------------------------------------------------------------------
// struct FITwinClippingCartographicPolygonInfo
//---------------------------------------------------------------------------------------

void FITwinClippingCartographicPolygonInfo::InitWith(AITwinSplineHelper* Spline)
{
	BE_ASSERT(!SplineHelper.IsValid(), "SplineHelper is already initialized");
	SplineHelper = Spline;
	if (SplineHelper.IsValid())
	{
		SetInvertEffect(SplineHelper->IsInvertedCutoutEffect());

		std::set<ITwin::ModelLink> const Links = SplineHelper->GetLinkedModels();
		for (ITwin::ModelLink const& Link : Links)
		{
			SetInfluenceSpecificModel(Link, true);
		}
	}
}

bool FITwinClippingCartographicPolygonInfo::GetInvertEffect() const
{
	return Properties.bInvertEffect;
}

void FITwinClippingCartographicPolygonInfo::DoSetInvertEffect(bool bInvert)
{
	Properties.bInvertEffect = bInvert;
}

void FITwinClippingCartographicPolygonInfo::DoSetEnabled(bool bInEnabled)
{
	if (SplineHelper.IsValid())
	{
		SplineHelper->EnableEffect(bInEnabled);
	}
}

AdvViz::SDK::RefID FITwinClippingCartographicPolygonInfo::GetAVizSplineId() const
{
	if (SplineHelper.IsValid())
	{
		return SplineHelper->GetAVizSplineId();
	}
	else
	{
		return AdvViz::SDK::RefID::Invalid();
	}
}
