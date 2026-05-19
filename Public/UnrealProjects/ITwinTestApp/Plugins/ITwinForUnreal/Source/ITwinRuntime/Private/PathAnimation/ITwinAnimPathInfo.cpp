/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAnimPathInfo.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <PathAnimation/ITwinAnimPathInfo.h>
#include <Spline/ITwinSplineHelper.h>
#include <PathAnimation/BakedAnimKeyFrames.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#	include <Core/Tools/Log.h>
#	include <SDK/Core/Visualization/RefID.h>
#include <Compil/AfterNonUnrealIncludes.h>


#pragma optimize("", off) // Disable optimization for easier debugging (TODO: remove)

void UITwinAnimPathInfo::Init(AITwinSplineHelper* InSplineHelper)
{
	SplineHelper = InSplineHelper;
	// TODO: apply the following right after spline tool activation
	UpdateSpline();
}

void UITwinAnimPathInfo::UpdateSpline()
{
	if (!SplineHelper.IsValid())
		return;
	SplineHelper->SetClosedLoop(IsLoop());
}

void UITwinCrowdAnimPathInfo::UpdateSpline()
{
	UITwinAnimPathInfo::UpdateSpline();

	if (SplineHelper.IsValid())
		SplineHelper->SetFixedSplineWidth(GetRoadWidth());
}

void UITwinTrafficAnimPathInfo::UpdateSpline()
{
	UITwinAnimPathInfo::UpdateSpline();

	if (SplineHelper.IsValid())
		SplineHelper->SetFixedSplineWidth(GetRoadWidth());
}

float UITwinAnimPathInfo::GetRoadWidth() const
{
	return (IsOneWay() ? 1 : 2) * GetLaneCount() * GetLaneWidth() + GetSeparatorWidth();
}

bool UITwinAnimPathInfo::HasBakedAnimation() const
{
	if (BakedFramesPerLane.Num() != GetLaneCount())
		return false;
	for (int i = 0; i < BakedFramesPerLane.Num(); ++i)
		if (!BakedFramesPerLane[i] || !BakedFramesPerLane[i]->IsReady())
			return false;
	return true;
}

void UITwinAnimPathInfo::InvalidateBakedAnimation()
{
	lastBakingRequestTime = GetWorld()->GetRealTimeSeconds();
	for (int i = 0; i < BakedFramesPerLane.Num(); ++i)
		BakedFramesPerLane[i]->MarkForUpdate();
}

void UITwinAnimPathInfo::BakeAnimationIfNeeded()
{
	if (!ensure(SplineHelper.IsValid()) || SplineHelper->GetNumberOfSplinePoints() < 2)
		return;
	auto splineInst = SplineHelper->GetAVizSpline()->GetRAutoLock();

	if (BakedFramesPerLane.Num() > 0 // not first baking
		&& (lastBakingRequestTime < 0 || GetWorld()->GetRealTimeSeconds() - lastBakingRequestTime < 5.f))
		return; // avoid baking too often (e.g. when moving spline points)

	if (BakedFramesPerLane.Num() != GetLaneCount())
		BakedFramesPerLane.SetNum(GetLaneCount());

	for (int i = 0; i < BakedFramesPerLane.Num(); ++i) // TODO: add lane offset for crowd/traffic paths
	{
		if (!BakedFramesPerLane[i])
			BakedFramesPerLane[i] = TStrongObjectPtr<UBakedAnimKeyFrames>(NewObject<UBakedAnimKeyFrames>(this));
		BakedFramesPerLane[i]->BakeSpline(SplineHelper->GetWorld(), splineInst->GetId(), GetSpeed());
	}

	lastBakingRequestTime = -1.f;
}

void UITwinObjectAnimPathInfo::SetSpeed(float InSpeed)
{
	Speed = InSpeed;
	for (int i = 0; i < BakedFramesPerLane.Num(); ++i)
		BakedFramesPerLane[i]->SetSpeed(InSpeed);
}

float UITwinTrafficAnimPathInfo::GetLaneSpeed(int laneIdx) const
{
	// TODO: compute per lane speed from density, max/min speeds and lane index
	return GetSpeed();
}

#pragma optimize("", on)