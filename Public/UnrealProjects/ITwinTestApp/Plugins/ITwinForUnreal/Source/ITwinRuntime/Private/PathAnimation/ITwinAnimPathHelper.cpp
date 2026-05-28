/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAnimPathHelper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <PathAnimation/ITwinAnimPathHelper.h>
#include <Spline/ITwinSplineHelper.h>
#include <PathAnimation/BakedAnimKeyFrames.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#	include <Core/Tools/Log.h>
#	include <SDK/Core/Visualization/RefID.h>
//#   include <SDK/Core/Visualization/PathAnimation.h>
#include <Compil/AfterNonUnrealIncludes.h>


struct UITwinAnimPathHelper::FImpl
{
	UITwinAnimPathHelper& Owner;

	//FString PathName; // do we need to rename paths?
	AdvViz::SDK::IAnimationPathInfoPtr PathProp;
	TArray<TStrongObjectPtr<UBakedAnimKeyFrames> > BakedFramesPerLane;
	TArray<FString> Objects; // same as 'objects' of PathProp but in FString format
	bool isPlaying = false;

	FImpl(UITwinAnimPathHelper& InOwner)
		: Owner(InOwner)
	{
	}

	bool HasBakedAnimation() const
	{
		if (BakedFramesPerLane.Num() == 0)
			return false;
		auto pathProp = PathProp->GetAutoLock();
		if (BakedFramesPerLane.Num() != pathProp->GetLaneCount())
			return false;
		for (int i = 0; i < BakedFramesPerLane.Num(); ++i)
			if (!BakedFramesPerLane[i] || !BakedFramesPerLane[i]->IsReady())
				return false;
		return true;
	}

	void InvalidateBakedAnimation()
	{
		lastBakingRequestTime = Owner.GetWorld()->GetRealTimeSeconds();
		for (int i = 0; i < BakedFramesPerLane.Num(); ++i)
			BakedFramesPerLane[i]->MarkForUpdate();
	}

	void BakeAnimationIfNeeded(AdvViz::SDK::ISplinePtr Spline)
	{
		auto splineInst = Spline->GetRAutoLock();

		if (BakedFramesPerLane.Num() > 0 // not first baking
			&& (lastBakingRequestTime < 0 || Owner.GetWorld()->GetRealTimeSeconds() - lastBakingRequestTime < 5.f))
			return; // avoid baking too often (e.g. when moving spline points)

		auto pathProp = PathProp->GetAutoLock();
		if (BakedFramesPerLane.Num() != pathProp->GetLaneCount())
			BakedFramesPerLane.SetNum(pathProp->GetLaneCount());

		for (int i = 0; i < BakedFramesPerLane.Num(); ++i) // TODO: add lane offset for crowd/traffic paths
		{
			if (!BakedFramesPerLane[i])
				BakedFramesPerLane[i] = TStrongObjectPtr<UBakedAnimKeyFrames>(NewObject<UBakedAnimKeyFrames>(&Owner));
			BakedFramesPerLane[i]->BakeSpline(Owner.GetWorld(), splineInst->GetId(), Owner.GetLaneSpeed(i));
		}

		lastBakingRequestTime = -1.f;
	}


private:
	float lastBakingRequestTime = -1.f;
};

UBakedAnimKeyFrames* UITwinAnimPathHelper::GetBakedFrames(int laneIdx/* = 0*/)
{
	if (Impl->BakedFramesPerLane.IsValidIndex(laneIdx))
		return Impl->BakedFramesPerLane[laneIdx].Get();
	return nullptr;
}

void UITwinAnimPathHelper::Init(AITwinSplineHelper* InSplineHelper, AdvViz::SDK::IAnimationPathInfoPtr InPathProp)
{
	SplineHelper = InSplineHelper;

	Impl = MakePimpl<FImpl>(*this);
	Impl->PathProp = InPathProp;
	
	// TODO: apply the following right after spline tool activation
	UpdateSpline();
}

AdvViz::SDK::RefID UITwinAnimPathHelper::GetPathRefID() const
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	return pathProp->GetId();
}

AdvViz::SDK::RefID UITwinAnimPathHelper::GetSplineRefID() const
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	return pathProp->GetSplineId();
}

//AdvViz::SDK::RefID UITwinAnimPathHelper::GetInstanceGroupRefID() const
//{
//	auto pathProp = Impl->PathProp->GetAutoLock();
//	return pathProp->GetInstGroupId();
//}

void UITwinAnimPathHelper::UpdateSpline()
{
	if (!SplineHelper.IsValid())
		return;
	SplineHelper->SetClosedLoop(IsLoop());
}

void UITwinCrowdAnimPathHelper::UpdateSpline()
{
	UITwinAnimPathHelper::UpdateSpline();

	if (SplineHelper.IsValid())
		SplineHelper->SetFixedSplineWidth(GetRoadWidth());
}

float UITwinAnimPathHelper::GetRoadWidth() const
{
	return (IsOneWay() ? 1 : 2) * GetLaneCount() * GetLaneWidth() + GetSeparatorWidth();
}

bool UITwinAnimPathHelper::HasBakedAnimation() const
{
	return Impl->HasBakedAnimation();
}

void UITwinAnimPathHelper::InvalidateBakedAnimation()
{
	Impl->InvalidateBakedAnimation();
}

void UITwinAnimPathHelper::BakeAnimationIfNeeded()
{
	if (!ensure(SplineHelper.IsValid()) || SplineHelper->GetNumberOfSplinePoints() < 2)
		return;
	Impl->BakeAnimationIfNeeded(SplineHelper->GetAVizSpline());
}

const TArray<FString>& UITwinAnimPathHelper::Get3DObjectPaths() const
{
	return Impl->Objects;
}

void UITwinAnimPathHelper::Get3DObjects(TArray<FString>& Assets) const
{
	Assets.Empty();
	std::vector<std::string> paths;
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->GetObjects(paths);
	for (auto path : paths)
	{
		Assets.Add(UTF8_TO_TCHAR(path.c_str()));
	}
}

void UITwinAnimPathHelper::Set3DObjects(const TArray<FString>& Assets)
{
	Impl->Objects.Empty();
	std::vector<std::string> paths;
	for (auto Asset : Assets)
	{
		paths.push_back(TCHAR_TO_UTF8(*Asset));

		TArray<FString> Parts;
		Asset.ParseIntoArray(Parts, TEXT("###"), /*InCullEmpty=*/false);
		if (Parts.Num() > 2)
			Impl->Objects.Add(Parts[2]);
	}
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetObjects(paths);
}

void UITwinAnimPathHelper::Set3DObjectsFromProps()
{
	std::vector<std::string> paths;
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->GetObjects(paths);
	Impl->Objects.Empty();
	for (auto path : paths)
	{
		FString Asset(UTF8_TO_TCHAR(path.c_str()));

		TArray<FString> Parts;
		Asset.ParseIntoArray(Parts, TEXT("###"), /*InCullEmpty=*/false);
		if (Parts.Num() > 2)
			Impl->Objects.Add(Parts[2]);
	}
}

bool UITwinAnimPathHelper::HasInvDirection() const
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	return pathProp->HasInvDir();
}

void UITwinAnimPathHelper::SetInvDirection(bool bInInvDirection)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetInvDir(bInInvDirection);
}

bool UITwinAnimPathHelper::IsLoop() const
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	return pathProp->IsLooping();
}

void UITwinAnimPathHelper::SetIsLoop(bool bInIsLoop)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetIsLooping(bInIsLoop);
}

////////////////////////////////////////////////
// UITwinObjectAnimPathHelper

float UITwinObjectAnimPathHelper::GetSpeed() const
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	return pathProp->GetSpeed();
}

void UITwinObjectAnimPathHelper::SetSpeed(float InSpeed)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetSpeed(InSpeed);
	for (int i = 0; i < Impl->BakedFramesPerLane.Num(); ++i)
		Impl->BakedFramesPerLane[i]->SetSpeed(InSpeed);
}

float UITwinObjectAnimPathHelper::GetDelay() const
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	return pathProp->GetStartTime();
}

void UITwinObjectAnimPathHelper::SetDelay(float InDelay)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetStartTime(InDelay);
}

EITwinAnimPathRepeatMode UITwinObjectAnimPathHelper::GetRepeatMode() const
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	return static_cast<EITwinAnimPathRepeatMode>(pathProp->GetRepeatMode());
}

void UITwinObjectAnimPathHelper::SetRepeatMode(EITwinAnimPathRepeatMode InRepeatMode)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetRepeatMode(static_cast<int>(InRepeatMode));
}

////////////////////////////////////////////////
// UITwinCrowdAnimPathHelper

float UITwinCrowdAnimPathHelper::GetSpeed() const
{
	// TODO: manage speed per lane, like in traffic?
	return 300.f;
}

bool UITwinCrowdAnimPathHelper::IsOneWay() const
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	return pathProp->IsOneWay();
}

void UITwinCrowdAnimPathHelper::SetOneWay(bool bInOneWay)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetOneWay(bInOneWay);
	
	InvalidateBakedAnimation();
}

int UITwinCrowdAnimPathHelper::GetLaneCount() const
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	return pathProp->GetLaneCount();
}

void UITwinCrowdAnimPathHelper::SetLaneCount(int InLaneCount)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetLaneCount(InLaneCount);

	InvalidateBakedAnimation();
}

float UITwinCrowdAnimPathHelper::GetLaneWidth() const
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	return pathProp->GetLaneWidth();
}

void UITwinCrowdAnimPathHelper::SetLaneWidth(float InLaneWidth)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetLaneWidth(InLaneWidth);

	InvalidateBakedAnimation();
}

float UITwinCrowdAnimPathHelper::GetDensity() const
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	return pathProp->GetDensity();
}

void UITwinCrowdAnimPathHelper::SetDensity(float InDensity)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetDensity(InDensity);

	// TODO: update population when density changes
}

////////////////////////////////////////////////
// UITwinTrafficAnimPathHelper

float UITwinTrafficAnimPathHelper::GetSeparatorWidth() const
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	return pathProp->GetSepWidth();
}

void UITwinTrafficAnimPathHelper::SetSeparatorWidth(float InSeparatorWidth)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetSepWidth(InSeparatorWidth);

	InvalidateBakedAnimation();
}

float UITwinTrafficAnimPathHelper::GetSpeed() const
{
	return GetLaneSpeed(0);
}

void UITwinTrafficAnimPathHelper::SetSpeed(float InSpeed)
{
	SetMinSpeed(InSpeed);
	SetMaxSpeed(InSpeed);
}

float UITwinTrafficAnimPathHelper::GetMinSpeed() const
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	return pathProp->GetMinSpeed();
}

void UITwinTrafficAnimPathHelper::SetMinSpeed(float InMinSpeed)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetMinSpeed(InMinSpeed);

	// TODO: rebuild traffic animation when min speed changes, as it can impact traffic density
}

float UITwinTrafficAnimPathHelper::GetMaxSpeed() const
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	return pathProp->GetMaxSpeed();
}

void UITwinTrafficAnimPathHelper::SetMaxSpeed(float InMaxSpeed)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetMaxSpeed(InMaxSpeed);

	// TODO: rebuild traffic animation when max speed changes, as it can impact traffic density
}

float UITwinTrafficAnimPathHelper::GetLaneSpeed(int laneIdx) const
{
	// TODO: compute per lane speed from density, max/min speeds and lane index
	return GetSpeed();
}
