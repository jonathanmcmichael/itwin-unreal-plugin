/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPathAnimTool.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <PathAnimation/ITwinPathAnimTool.h>
#include <PathAnimation/ITwinAnimPathInfo.h>
#include <PathAnimation/BakedAnimKeyFrames.h>
#include <Helpers/ITwinConsoleCommandUtils.inl>
#include <Helpers/ITwinMathUtils.h>
#include <Helpers/ITwinTracingHelper.h>
#include <Helpers/WorldSingleton.h>
#include <ITwinGeolocation.h>
#include <ITwinGoogle3DTileset.h>
#include <ITwinIModel.h>
#include <ITwinRealityData.h>
#include <ITwinTilesetAccess.h>
#include <ITwinUtilityLibrary.h>
#include <Math/UEMathConversion.h>
#include <Population/ITwinPopulation.h>
#include <Population/ITwinPopulationTool.h>
#include <Spline/ITwinSplineHelper.h>
#include <Spline/ITwinSplineTool.h>
#include <Spline/ITwinSplineEnums.h>
#include <Components/SplineComponent.h>

// UE headers
#include <Blueprint/WidgetLayoutLibrary.h>
#include <DrawDebugHelpers.h>
#include <EngineUtils.h> // for TActorIterator<>
#include <Engine/StaticMeshActor.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#	include <Core/Tools/Log.h>
#	include <glm/matrix.hpp>
#	include <glm/gtc/matrix_access.hpp>
#	include <glm/gtx/compatibility.hpp>
#	include <BeUtils/SplineSampling/SplineSampling.h>
#	include <SDK/Core/Visualization/Instance.h>
#	include <SDK/Core/Visualization/InstancesGroup.h>
#	include <SDK/Core/Visualization/InstancesManager.h>
#   include <SDK/Core/Visualization/KeyframeAnimator.h>
#   include <SDK/Core/Visualization/PathAnimation.h>
#	include <SDK/Core/Visualization/Spline.h>
#	include <SDK/Core/Tools/Log.h>
#	include <SDK/Core/Tools/TypeId.h>
#	include <SDK/Core/Tools/Extension.h>
#include <Compil/AfterNonUnrealIncludes.h>

#pragma optimize("", off) // Disable optimization for easier debugging (TODO: remove)

namespace
{
	bool IsSplineUsedForPathAnim(AITwinSplineHelper* SplineHelper)
	{
		return SplineHelper &&
			(SplineHelper->GetUsage() == EITwinSplineUsage::AnimPath
			|| SplineHelper->GetUsage() == EITwinSplineUsage::AnimPathTraffic
			|| SplineHelper->GetUsage() == EITwinSplineUsage::AnimPathCrowd);
	}

	EITwinSplineUsage GetSplineUsageFromAnimPathType(EITwinAnimPathType PathType)
	{
		switch (PathType)
		{
		case EITwinAnimPathType::Object:
			return EITwinSplineUsage::AnimPath;
		case EITwinAnimPathType::Traffic:
			return EITwinSplineUsage::AnimPathTraffic;
		case EITwinAnimPathType::Crowd:
			return EITwinSplineUsage::AnimPathCrowd;
		default:
			ensureMsgf(false, TEXT("Unknown anim path type"));
			return EITwinSplineUsage::Undefined;
		}
	}

	EITwinAnimPathType GetAnimPathTypeFromSplineUsage(EITwinSplineUsage SplineUsage)
	{
		switch (SplineUsage)
		{
		case EITwinSplineUsage::AnimPath:
			return EITwinAnimPathType::Object;
		case EITwinSplineUsage::AnimPathTraffic:
			return EITwinAnimPathType::Traffic;
		case EITwinSplineUsage::AnimPathCrowd:
			return EITwinAnimPathType::Crowd;
		default:
			ensureMsgf(false, TEXT("Unknown spline usage"));
			return EITwinAnimPathType::Count;
		}
	}
}


typedef AdvViz::SDK::IInstancePtr SharedInstance;

class InstanceWithAnimPathExt : public AdvViz::SDK::Tools::Extension, public AdvViz::SDK::Tools::TypeId<InstanceWithAnimPathExt>, public std::enable_shared_from_this<InstanceWithAnimPathExt>
{
public:
	InstanceWithAnimPathExt(FTransform InitTransform) : initTransform_(InitTransform), lastTransform_(InitTransform) {};

	using AdvViz::SDK::Tools::TypeId<InstanceWithAnimPathExt>::GetTypeId;

	FTransform GetTransform(float DeltaTime, EITwinAnimPathRepeatMode repeatMode, bool bReverse)
	{
		if (!keyFrames_.IsValid() || keyFrames_->NeedsUpdate())
			return initTransform_;
		
		curTime_ += DeltaTime;
		
		if (isPaused || curTime_ < 0)
			return lastTransform_;

		float animTime(curTime_);
		float duration(keyFrames_->GetTotalTime());
		if (repeatMode == EITwinAnimPathRepeatMode::None)
		{
			if (curTime_ > duration)
			{
				animTime = duration;
				//isPaused = true;
			}
		}
		else if (curTime_ > duration)
		{
			animTime = FMath::Fmod(curTime_, duration);
			if (repeatMode == EITwinAnimPathRepeatMode::PingPong)
			{
				int32 cycle = FMath::FloorToInt(curTime_ / duration);
				if (cycle % 2 == 1)
					bReverse = !bReverse;
			}
		}
		lastTransform_ = keyFrames_->GetTransform(animTime, bReverse);
		return lastTransform_;
	}

	void SetKeyFrames(UBakedAnimKeyFrames* keyFramesPtr)
	{
		keyFrames_ = keyFramesPtr;
		curTime_ = 0.f;
	}

	void SetPaused(bool bPaused)
	{
		isPaused = bPaused;
	}

	void ResetAnimation(float InDelay)
	{
		curTime_ = -InDelay;
		lastTransform_ = initTransform_;
	}

private:
	FTransform initTransform_;
	FTransform lastTransform_;
	TWeakObjectPtr<UBakedAnimKeyFrames> keyFrames_;
	float curTime_ = 0.f; // TODO
	float delay_ = 0.f; // TODO
	bool isPaused = false;
};



class AITwinPathAnimTool::FImpl
{
public:
	AITwinPathAnimTool& Owner;

	TArray<TStrongObjectPtr<UITwinObjectAnimPathInfo> > ObjectAnimPathInfos;
	TArray<TStrongObjectPtr<UITwinTrafficAnimPathInfo> > TrafficAnimPathInfos;
	TArray<TStrongObjectPtr<UITwinCrowdAnimPathInfo> > CrowdAnimPathInfos;

	TWeakObjectPtr<AITwinPopulationTool> PopulationTool;
	TWeakObjectPtr<AITwinSplineTool> SplineTool;

	FImpl(AITwinPathAnimTool& InOwner) : Owner(InOwner)
	{}

	inline int32 NumPaths(EITwinAnimPathType PathType) const;

	inline UITwinAnimPathInfo* GetMutableAnimPathInfo(FAnimPathIdentifier PathHandle);
	inline const UITwinAnimPathInfo* GetAnimPathInfo(FAnimPathIdentifier PathHandle) const;

	bool RegisterAnimPathSpline(AITwinSplineHelper* SplineHelper);
	bool UnregisterAnimPathSpline(AITwinSplineHelper* SplineBeingRemoved);

	// For communication with iTwin Studio
	AdvViz::SDK::RefID GetPathRefId(FAnimPathIdentifier PathHandle) const;
	FAnimPathIdentifier GetPathIdentifier(AdvViz::SDK::RefID const& RefID) const;

	std::optional<FAnimPathIdentifier> GetSelectedPath() const;
	bool SelectPath(FAnimPathIdentifier PathHandle, bool bEnterIsolationMode = true);

	bool RemovePath(FAnimPathIdentifier PathHandle, bool bTriggeredFromITS);
	void ZoomOnPath(FAnimPathIdentifier PathHandle);

	void ResetAnimation(FAnimPathIdentifier PathHandle);
	void PlayAnimation(FAnimPathIdentifier PathHandle, bool bPlay);

	bool Add3DObject(FAnimPathIdentifier PathHandle, bool bClearPrevious = false);
	void RemovePathObjects(FAnimPathIdentifier PathHandle);

	// Make the Spline Tool the active tool, with usage restricted to the creation of the given type of animation path
	TWeakObjectPtr<AITwinSplineTool> ActivateSplineTool(UWorld* World, EITwinAnimPathType PathType);

	// Select a spline, or reset selection if InSplineHelper is null
	void SelectSpline(AITwinSplineHelper* InSplineHelper, UWorld* World);

	void OnSplineEditedInTool();

	// Store whether the removal event was initiated by Unreal (delete key in 3D viewport) or
	// iTwin Studio (trash icon in path property widget)
	enum class ERemovalInitiator : uint8_t
	{
		Unreal,
		ITS
	};
	std::optional<ERemovalInitiator> RemovalInitiatorOpt;

	struct [[nodiscard]] FScopedRemovalContext
	{
		FImpl& Impl;

		FScopedRemovalContext(FImpl& InImpl, ERemovalInitiator RemovalInitiator) : Impl(InImpl)
		{
			Impl.RemovalInitiatorOpt.emplace(RemovalInitiator);
		}

		~FScopedRemovalContext()
		{
			Impl.RemovalInitiatorOpt.reset();
		}
	};

	// Change all path splines visibility in the viewport (without deactivating them)
	void SetAllAnimPathProxiesVisibility(bool bVisibleInGame);
	void HideAllAnimPathProxies() { SetAllAnimPathProxiesVisibility(false); }

	// Show/Hide path splines for the given path type
	void SetAnimPathProxyVisibility(EITwinAnimPathType PathType, bool bVisibleInGame, bool bIsolationMode = false);
	void ShowOnlyAnimPathProxiesOfType(EITwinAnimPathType SelectedType, bool bIsolationMode);

	// Returns whether the splines of the given path type are visible
	bool IsAnimPathProxyVisible(EITwinAnimPathType PathType) const;

	void BakeAnimation(FAnimPathIdentifier PathHandle);
	void BakeAnimation();

	void UpdateAnimatedObjects(FAnimPathIdentifier PathHandle, float DeltaTime);
	void Tick(float DeltaTime);
};

/*
TWeakObjectPtr<AITwinPopulationTool> AITwinPathAnimTool::FImpl::ActivatePopulationTool(UWorld* World,
	bool bUpdateTransformationMode)
{
	if (ensure(PopulationTool.IsValid()))
	{
		if (!PopulationTool->IsEnabled())
		{
			AITwinInteractiveTool::DisableAll(World);
			PopulationTool->SetEnabled(true);
		}
		PopulationTool->ResetToDefault();
		if (bUpdateTransformationMode && TransformationModeOpt)
		{
			PopulationTool->SetTransformationMode(*TransformationModeOpt);
		}
	}
	return PopulationTool;
}

AITwinPopulation const* AITwinPathAnimTool::FImpl::GetSelectedPopulation(int32& OutSelectedInstanceIndex) const
{
	AITwinPopulation const* SelectedPopulation = nullptr;
	OutSelectedInstanceIndex = INDEX_NONE;
	if (ensure(PopulationTool.IsValid()))
	{
		SelectedPopulation = PopulationTool->GetSelectedPopulation();
		if (SelectedPopulation)
		{
			OutSelectedInstanceIndex = PopulationTool->GetSelectedInstanceIndex();
		}
	}
	return SelectedPopulation;
}

void AITwinPathAnimTool::FImpl::SelectPopulationInstance(AITwinPopulation* Population,
	int32 InstanceIndex,
	UWorld* World)
{
	bool const bUpdateTransformationMode = (Population != nullptr);
	auto const PopTool = ActivatePopulationTool(World, bUpdateTransformationMode);
	if (PopTool.IsValid())
	{
		PopTool->SetSelectedPopulation(Population);
		PopTool->SetSelectedInstanceIndex(InstanceIndex);
		PopTool->SelectionChangedEvent.Broadcast();
	}
}

void AITwinPathAnimTool::FImpl::DeleteSelectedPopulationInstance()
{
	if (ensure(PopulationTool.IsValid()))
	{
		PopulationTool->DeleteSelectedInstance();
	}
}
*/

TWeakObjectPtr<AITwinSplineTool> AITwinPathAnimTool::FImpl::ActivateSplineTool(UWorld* World, EITwinAnimPathType PathType)
{
	if (ensure(SplineTool.IsValid()))
	{
		bool bNeedEnableSplineTool = false;
		if (SplineTool->IsEnabled())
		{
			bNeedEnableSplineTool = SplineTool->GetUsage() != GetSplineUsageFromAnimPathType(PathType);
		}
		else
		{
			AITwinInteractiveTool::DisableAll(World);
			bNeedEnableSplineTool = true;
		}
		if (bNeedEnableSplineTool)
		{
			ITwin::EnableSplineTool(World, true, GetSplineUsageFromAnimPathType(PathType));
		}
	}
	return SplineTool;
}

void AITwinPathAnimTool::FImpl::SelectSpline(AITwinSplineHelper* SplineHelper, UWorld* World)
{
	if (!ensure(SplineTool.IsValid()))
		return;
	if (SplineHelper)
	{
		AITwinInteractiveTool::DisableAll(World);

		ITwin::EnableSplineTool(World, true, SplineHelper->GetUsage());
		SplineTool->SetSelectedSpline(SplineHelper);
	}
	else
	{
		// Deselect
		SplineTool->SetSelectedSpline(nullptr);
	}
}


AITwinPathAnimTool::AITwinPathAnimTool()
	: Impl(MakePimpl<FImpl>(*this))
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;//false; TODO
	PrimaryActorTick.bTickEvenWhenPaused = true;
	//PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}

void AITwinPathAnimTool::ConnectPopulationTool(AITwinPopulationTool* PopulationTool)
{
	Impl->PopulationTool = PopulationTool;
	//if (PopulationTool)
	//{
	//	PopulationTool->InteractiveCreationAbortedEvent.AddUniqueDynamic(this, &AITwinPathAnimTool::OnItemCreationAbortedInTool);
	//}
}

void AITwinPathAnimTool::ConnectSplineTool(AITwinSplineTool* SplineTool)
{
	Impl->SplineTool = SplineTool;
	if (SplineTool)
	{
		SplineTool->SplineAddedEvent.AddUniqueDynamic(this, &AITwinPathAnimTool::OnSplineHelperAdded);
		//SplineTool->SplinePointSelectedEvent.AddUniqueDynamic(this, &UAnimationPathToolWidgetImpl::OnSplinePointSelected);
		SplineTool->SplineBeforeRemovedEvent.AddUniqueDynamic(this, &AITwinPathAnimTool::OnSplineHelperRemoved);
		SplineTool->InteractiveCreationAbortedEvent.AddUniqueDynamic(this, &AITwinPathAnimTool::OnItemCreationAbortedInTool);
		SplineTool->SplineEditionEvent.AddUniqueDynamic(this, &AITwinPathAnimTool::OnSplineEditedInTool);
		//SplineTool->SplinePointMovedEvent.AddUniqueDynamic(this, &AITwinPathAnimTool::OnSplineEditedInTool);
	}
}

void AITwinPathAnimTool::FImpl::BakeAnimation(FAnimPathIdentifier PathHandle)
{
	if (auto PathInfo = GetMutableAnimPathInfo(PathHandle))
		PathInfo->BakeAnimationIfNeeded();
}

void AITwinPathAnimTool::FImpl::BakeAnimation()
{
	for (EITwinAnimPathType Type : {
		EITwinAnimPathType::Object,
		EITwinAnimPathType::Traffic,
		EITwinAnimPathType::Crowd })
	{
		for (int32 Index(0); Index < NumPaths(Type); ++Index)
		{
			BakeAnimation(FAnimPathIdentifier(Type, Index));
		}
	}
}

void AITwinPathAnimTool::FImpl::RemovePathObjects(FAnimPathIdentifier PathHandle)
{
	if (!PopulationTool.IsValid())
		return;

	if (auto PathInfo = GetMutableAnimPathInfo(PathHandle))
	{
		for (auto Population : PathInfo->Populations)
		{
			if (Population.IsValid())
				Population->RemoveAllInstances();
		}
	}
}

bool AITwinPathAnimTool::FImpl::Add3DObject(FAnimPathIdentifier PathHandle, bool bClearPrevious/* = false*/)
{
	if (!PathHandle.IsValid(NumPaths(PathHandle.PathType)))
		return false;

	auto PathInfo = GetMutableAnimPathInfo(PathHandle);
	if (!PathInfo->SplineHelper.IsValid())
		return false;

	FVector Pos = PathInfo->SplineHelper->GetSplineComponent()->GetLocationAtDistanceAlongSpline(0.0, ESplineCoordinateSpace::World);

	if (PathInfo->Objects.Num() > 0 && PopulationTool.IsValid())
	{
		if (bClearPrevious && PathInfo->Populations.Num() > 0)// && PathInfo->PopulationHelper->GetNumberOfInstances() > 0)
		{
			RemovePathObjects(PathHandle);
		}
		AITwinPopulation* Population = PopulationTool->PreLoadPopulation(PathInfo->Objects[0]);
		if (Population)
		{
			PathInfo->Populations.Add(Population);
			int32 instIdx = Population->AddInstance(FTransform(Pos));
			if (instIdx >= 0)
			{
				PathInfo->BakeAnimationIfNeeded();
				std::shared_ptr<InstanceWithAnimPathExt> animPathExt = std::make_shared<InstanceWithAnimPathExt>(FTransform(Pos));
				if (PathInfo->BakedFramesPerLane.Num() > 0)
					animPathExt->SetKeyFrames(PathInfo->BakedFramesPerLane[0].Get());

				if (auto InstancePtr = Population->GetAVizInstance(instIdx))
				{
					auto inst = InstancePtr->GetAutoLock();
					inst->AddExtension(animPathExt); //if (inst->GetAnimPathId()) TODO: add anim path RefId to rebuild connection upon loading?
				}
			}
			return true;
		}
	}

	return false;
}

//FString AITwinPathAnimTool::GetName(FAnimPathIdentifier PathHandle) const
//{
//	if (auto PathInfo = Impl->GetAnimPathInfo(PathHandle))
//		return PathInfo->GetName();
//	return FString();
//}
//
//void AITwinPathAnimTool::SetName(FAnimPathIdentifier PathHandle, const FString& Name)
//{
//	Impl->GetMutableAnimPathInfo(PathHandle)->SetName(Name);
//}

void AITwinPathAnimTool::Get3DObjects(FAnimPathIdentifier PathHandle, TArray<FString>& AssetPaths) const
{
	if (auto PathInfo = Impl->GetAnimPathInfo(PathHandle))
		AssetPaths = PathInfo->Get3DObjects();
	else
		AssetPaths.Empty();
}

void AITwinPathAnimTool::Set3DObjects(FAnimPathIdentifier PathHandle, const TArray<FString>& AssetPaths)
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object || AssetPaths.Num() == 1);
	auto PathInfo = Impl->GetMutableAnimPathInfo(PathHandle);
	if (!PathInfo)
		return;
	Impl->GetMutableAnimPathInfo(PathHandle)->Set3DObjects(AssetPaths);
	if (PathHandle.PathType == EITwinAnimPathType::Object) // TODO: refactor
	{
		Impl->Add3DObject(PathHandle, true); // add new object and clear previous one if any
	}
}

bool AITwinPathAnimTool::HasInvDirection(FAnimPathIdentifier PathHandle) const
{
	if (auto PathInfo = Impl->GetAnimPathInfo(PathHandle))
		return PathInfo->HasInvDirection();
	return false;
}

void AITwinPathAnimTool::SetInvDirection(FAnimPathIdentifier PathHandle, bool bInvDirection)
{
	if (auto PathInfo = Impl->GetMutableAnimPathInfo(PathHandle))
		PathInfo->SetInvDirection(bInvDirection);
}

bool AITwinPathAnimTool::IsLoop(FAnimPathIdentifier PathHandle) const
{
	if (auto PathInfo = Impl->GetAnimPathInfo(PathHandle))
		return PathInfo->IsLoop();
	return false;
}

void AITwinPathAnimTool::SetIsLoop(FAnimPathIdentifier PathHandle, bool isLoop)
{
	if (auto PathInfo = Impl->GetMutableAnimPathInfo(PathHandle))
	{
		PathInfo->SetIsLoop(isLoop);
		if (PathInfo->SplineHelper.IsValid())
			PathInfo->SplineHelper->SetClosedLoop(isLoop);
	}
}

float AITwinPathAnimTool::GetSpeed(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Object);
	if (auto PathInfo = Impl->GetAnimPathInfo(PathHandle))
		return PathInfo->GetSpeed();
	return 0.f;
}

void AITwinPathAnimTool::SetSpeed(FAnimPathIdentifier PathHandle, float Speed)
{
	//ensure(PathHandle.PathType == EITwinAnimPathType::Object);
	if (auto PathInfo = Impl->GetMutableAnimPathInfo(PathHandle))
		PathInfo->SetSpeed(Speed);
}

float AITwinPathAnimTool::GetDelay(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Object);
	if (auto PathInfo = Impl->GetAnimPathInfo(PathHandle))
		return PathInfo->GetDelay();
	return 0.f;
}

void AITwinPathAnimTool::SetDelay(FAnimPathIdentifier PathHandle, float Delay)
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Object);
	if (auto PathInfo = Impl->GetMutableAnimPathInfo(PathHandle))
		PathInfo->SetDelay(Delay);
	Impl->ResetAnimation(PathHandle);
}

EITwinAnimPathRepeatMode AITwinPathAnimTool::GetRepeatMode(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Object);
	if (auto PathInfo = Impl->GetAnimPathInfo(PathHandle))
		return PathInfo->GetRepeatMode();
	return EITwinAnimPathRepeatMode::Count;
}

void AITwinPathAnimTool::SetRepeatMode(FAnimPathIdentifier PathHandle, EITwinAnimPathRepeatMode RepeatMode)
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Object);
	if (auto PathInfo = Impl->GetMutableAnimPathInfo(PathHandle))
		PathInfo->SetRepeatMode(RepeatMode);
	Impl->ResetAnimation(PathHandle);
}

bool AITwinPathAnimTool::IsOneWay(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathInfo = Impl->GetAnimPathInfo(PathHandle))
		return PathInfo->IsOneWay();
	return true;
}

void AITwinPathAnimTool::SetOneWay(FAnimPathIdentifier PathHandle, bool bOneWay)
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathInfo = Impl->GetMutableAnimPathInfo(PathHandle))
	{
		PathInfo->SetOneWay(bOneWay);
		PathInfo->UpdateSpline();
	}
}

int AITwinPathAnimTool::GetLaneCount(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathInfo = Impl->GetAnimPathInfo(PathHandle))
		return PathInfo->GetLaneCount();
	return 0;
}

void AITwinPathAnimTool::SetLaneCount(FAnimPathIdentifier PathHandle, int LaneCount)
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathInfo = Impl->GetMutableAnimPathInfo(PathHandle))
	{
		PathInfo->SetLaneCount(LaneCount);
		PathInfo->UpdateSpline();
	}
}

float AITwinPathAnimTool::GetLaneWidth(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathInfo = Impl->GetAnimPathInfo(PathHandle))
		return PathInfo->GetLaneWidth();
	return 0;
}

void AITwinPathAnimTool::SetLaneWidth(FAnimPathIdentifier PathHandle, float LaneWidth)
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathInfo = Impl->GetMutableAnimPathInfo(PathHandle))
	{
		PathInfo->SetLaneWidth(LaneWidth);
		PathInfo->UpdateSpline();
	}
}

float AITwinPathAnimTool::GetDensity(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathInfo = Impl->GetAnimPathInfo(PathHandle))
		return PathInfo->GetDensity();
	return 0.f;
}

void AITwinPathAnimTool::SetDensity(FAnimPathIdentifier PathHandle, float Density)
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathInfo = Impl->GetMutableAnimPathInfo(PathHandle))
	{
		PathInfo->SetDensity(Density);
		// TODO: repopulate
	}
}

float AITwinPathAnimTool::GetSeparatorWidth(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Traffic);
	if (auto PathInfo = Impl->GetAnimPathInfo(PathHandle))
		return PathInfo->GetSeparatorWidth();
	return 0.f;
}

void AITwinPathAnimTool::SetSeparatorWidth(FAnimPathIdentifier PathHandle, float SeparatorWidth)
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Traffic);
	if (auto PathInfo = Impl->GetMutableAnimPathInfo(PathHandle))
	{
		PathInfo->SetSeparatorWidth(SeparatorWidth);
		PathInfo->UpdateSpline();
	}
}

float AITwinPathAnimTool::GetMinSpeed(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Traffic);
	if (auto PathInfo = Impl->GetAnimPathInfo(PathHandle))
		return PathInfo->GetMinSpeed();
	return 0.f;
}

void AITwinPathAnimTool::SetMinSpeed(FAnimPathIdentifier PathHandle, float MinSpeed)
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Traffic);
	if (auto PathInfo = Impl->GetMutableAnimPathInfo(PathHandle))
		PathInfo->SetMinSpeed(MinSpeed);
}

float AITwinPathAnimTool::GetMaxSpeed(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Traffic);
	if (auto PathInfo = Impl->GetAnimPathInfo(PathHandle))
		return PathInfo->GetMaxSpeed();
	return 0.f;
}

void AITwinPathAnimTool::SetMaxSpeed(FAnimPathIdentifier PathHandle, float MaxSpeed)
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Traffic);
	if (auto PathInfo = Impl->GetMutableAnimPathInfo(PathHandle))
		PathInfo->SetMaxSpeed(MaxSpeed);
}

bool AITwinPathAnimTool::StartInteractiveCreation(EITwinAnimPathType PathType)
{
	// Abort current anim path creation, if any
	AbortInteractiveCreation(true);

	// Make sure we hide all anim path proxies (only the new item will be visible).
	Impl->HideAllAnimPathProxies();

	// Start interactive drawing
	TWeakObjectPtr<AITwinSplineTool> SplineTool = Impl->ActivateSplineTool(GetWorld(), PathType);
	if (SplineTool.IsValid())
	{
		//Impl->PathTypeBeingCreated = PathType;
		// Activate overview camera (top view)
		SplineTool->OnOverviewCamera();
		SplineTool->StartInteractiveCreation();
		return true;
	}
	return false;
}

bool AITwinPathAnimTool::FImpl::RegisterAnimPathSpline(AITwinSplineHelper* SplineHelper)
{
	FAnimPathIdentifier PathHandle;
	PathHandle.PathType = GetAnimPathTypeFromSplineUsage(SplineHelper->GetUsage());
	PathHandle.PathIndex = NumPaths(PathHandle.PathType);
	//auto spline = SplineHelper->GetAVizSpline()->GetRAutoLock();
	//auto splineId = spline->GetId();

	switch (PathHandle.PathType)
	{	
	case EITwinAnimPathType::Object:
	{
		//UITwinObjectAnimPathInfo& PathInfo = ObjectAnimPathInfos.AddDefaulted_GetRef();
		TStrongObjectPtr<UITwinObjectAnimPathInfo> PathInfo(NewObject<UITwinObjectAnimPathInfo>(&Owner));
		ObjectAnimPathInfos.Add(PathInfo);
		PathInfo->Init(SplineHelper);
		break;
	}
	case EITwinAnimPathType::Traffic:
	{
		//UITwinTrafficAnimPathInfo& PathInfo = TrafficAnimPathInfos.AddDefaulted_GetRef();
		TStrongObjectPtr<UITwinTrafficAnimPathInfo> PathInfo(NewObject<UITwinTrafficAnimPathInfo>(&Owner));
		TrafficAnimPathInfos.Add(PathInfo);
		PathInfo->Init(SplineHelper);
		break;
	}
	case EITwinAnimPathType::Crowd:
	{
		//UITwinCrowdAnimPathInfo& PathInfo = CrowdAnimPathInfos.AddDefaulted_GetRef();
		TStrongObjectPtr<UITwinCrowdAnimPathInfo> PathInfo(NewObject<UITwinCrowdAnimPathInfo>(&Owner));
		CrowdAnimPathInfos.Add(PathInfo);
		PathInfo->Init(SplineHelper);
		break;
	}
	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(case EITwinAnimPathType::Count:, false);
	}

	Owner.AnimPathListModifiedEvent.Broadcast();
	Owner.AnimPathAddedEvent.Broadcast(PathHandle);
	return true;
}

void AITwinPathAnimTool::OnSplineHelperAdded(AITwinSplineHelper* NewSpline)
{
	if (IsSplineUsedForPathAnim(NewSpline))
	{
		Impl->RegisterAnimPathSpline(NewSpline);
	}
}

bool AITwinPathAnimTool::FImpl::UnregisterAnimPathSpline(AITwinSplineHelper* SplineBeingRemoved)
{
	auto const SelectedBefore = GetSelectedPath();

	auto PathHandle = GetPathIdentifier(SplineBeingRemoved->GetAVizSplineId());
	if (ensure(PathHandle.IsValid(NumPaths(PathHandle.PathType))))
	{
		switch (PathHandle.PathType)
		{
		case EITwinAnimPathType::Object:
			ObjectAnimPathInfos.RemoveAt(PathHandle.PathIndex);
			break;
		case EITwinAnimPathType::Traffic:
			TrafficAnimPathInfos.RemoveAt(PathHandle.PathIndex);
			break;
		case EITwinAnimPathType::Crowd:
			CrowdAnimPathInfos.RemoveAt(PathHandle.PathIndex);
			break;
		default:
			break;
		}

		//const bool bTriggeredFromITS = RemovalInitiatorOpt
		//	&& *RemovalInitiatorOpt == FImpl::ERemovalInitiator::ITS;
		Owner.AnimPathRemovedEvent.Broadcast(PathHandle, false);//bTriggeredFromITS); TODO
		Owner.AnimPathListModifiedEvent.Broadcast();

		// After removing a path, we should exit isolation mode or else we'll be in an inconsistent state
		if (SelectedBefore)
		{
			SetAllAnimPathProxiesVisibility(true);
		}
		return true;
	}

	return false;
}

void AITwinPathAnimTool::OnSplineHelperRemoved(AITwinSplineHelper* SplineBeingRemoved)
{
	if (IsSplineUsedForPathAnim(SplineBeingRemoved))
	{
		Impl->UnregisterAnimPathSpline(SplineBeingRemoved);
	}
}

void AITwinPathAnimTool::OnItemCreationAbortedInTool(bool bTriggeredFromITS)
{
	InteractiveCreationAbortedEvent.Broadcast(bTriggeredFromITS);
}

void AITwinPathAnimTool::FImpl::OnSplineEditedInTool()
{
	if (!SplineTool.IsValid() || !SplineTool->IsUsedForPathAnim() || !SplineTool->GetSelectedSpline())
		return;
	auto PathHandle = GetPathIdentifier(SplineTool->GetSelectedSpline()->GetAVizSplineId());
	if (auto PathInfo = GetMutableAnimPathInfo(PathHandle))
	{
		PathInfo->InvalidateBakedAnimation();
	}
}

void AITwinPathAnimTool::OnSplineEditedInTool()
{
	Impl->OnSplineEditedInTool();
}

inline int32 AITwinPathAnimTool::FImpl::NumPaths(EITwinAnimPathType PathType) const
{
	switch (PathType)
	{
	case EITwinAnimPathType::Object:	return ObjectAnimPathInfos.Num();
	case EITwinAnimPathType::Traffic:	return TrafficAnimPathInfos.Num();
	case EITwinAnimPathType::Crowd:		return CrowdAnimPathInfos.Num();
	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(case EITwinAnimPathType::Count: , 0);
	}
}

int32 AITwinPathAnimTool::NumPaths(EITwinAnimPathType PathType) const
{
	return Impl->NumPaths(PathType);
}

inline UITwinAnimPathInfo* AITwinPathAnimTool::FImpl::GetMutableAnimPathInfo(FAnimPathIdentifier PathHandle)
{
	if (!PathHandle.IsValid(NumPaths(PathHandle.PathType)))
		return nullptr;
	switch (PathHandle.PathType)
	{
	BE_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH(case EITwinAnimPathType::Count: )
	case EITwinAnimPathType::Object:	return ObjectAnimPathInfos[PathHandle.PathIndex].Get();
	case EITwinAnimPathType::Traffic:	return TrafficAnimPathInfos[PathHandle.PathIndex].Get();
	case EITwinAnimPathType::Crowd:		return CrowdAnimPathInfos[PathHandle.PathIndex].Get();
	}
}

inline const UITwinAnimPathInfo* AITwinPathAnimTool::FImpl::GetAnimPathInfo(FAnimPathIdentifier PathHandle) const
{
	if (!PathHandle.IsValid(NumPaths(PathHandle.PathType)))
		return nullptr;
	switch (PathHandle.PathType)
	{
	BE_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH(case EITwinAnimPathType::Count:)
	case EITwinAnimPathType::Object:	return ObjectAnimPathInfos[PathHandle.PathIndex].Get();
	case EITwinAnimPathType::Traffic:	return TrafficAnimPathInfos[PathHandle.PathIndex].Get();
	case EITwinAnimPathType::Crowd:		return CrowdAnimPathInfos[PathHandle.PathIndex].Get();
	}
}

bool AITwinPathAnimTool::FImpl::RemovePath(FAnimPathIdentifier PathHandle, bool bTriggeredFromITS)
{
	if (!ensure(PathHandle.IsValid(NumPaths(PathHandle.PathType))))
		return false;

	//FScopedRemovalContext RemovalCtx(*this,
	//	bTriggeredFromITS ? ERemovalInitiator::ITS : ERemovalInitiator::Unreal); TODO

	// Select the animation path if needed (for undo/redo) - the path is already selected if this event is
	// triggered from iTS animation path properties page, but not if the event is triggered from the list of paths.
	auto const CurrentSelection = GetSelectedPath();
	bool bPathIsSelected = CurrentSelection
		&& CurrentSelection->PathType == PathHandle.PathType
		&& CurrentSelection->PathIndex == PathHandle.PathIndex;
	if (!bPathIsSelected)
	{
		bPathIsSelected = SelectPath(PathHandle, false);
	}
	//Owner.RemoveAnimPathStartedEvent.Broadcast(); TODO

	const int32 NumPathsOld = NumPaths(PathHandle.PathType);
	PlayAnimation(PathHandle, false);
	if (auto PathInfo = GetAnimPathInfo(PathHandle))
	{
		// Remove associated objects from the population tool, if any
		RemovePathObjects(PathHandle);
		// Remove spline and path animation info
		if (PathInfo->SplineHelper.IsValid() && ensure(SplineTool.IsValid()))
			SplineTool->DeleteSpline(PathInfo->SplineHelper.Get());
	}

	const bool bRemoved = (NumPaths(PathHandle.PathType) == NumPathsOld - 1);
	return bRemoved;
}

bool AITwinPathAnimTool::RemovePath(FAnimPathIdentifier PathHandle, bool bTriggeredFromITS)
{
	const bool bRemoved = Impl->RemovePath(PathHandle, bTriggeredFromITS);
	if (bRemoved)
	{
		//RemoveAnimPathCompletedEvent.Broadcast(); TODO
	}
	return bRemoved;
}

bool AITwinPathAnimTool::FImpl::SelectPath(FAnimPathIdentifier PathHandle, bool bEnterIsolationMode)
{
	if (!ensure(PathHandle.IsValid(NumPaths(PathHandle.PathType))))
		return false;

	bool bHasSetSelection = false;
	auto PathInfo = GetAnimPathInfo(PathHandle);
	if (PathInfo && PathInfo->SplineHelper.IsValid())
	{
		SelectSpline(PathInfo->SplineHelper.Get(), Owner.GetWorld());

		bHasSetSelection = PathInfo->SplineHelper->IsSelected();
	}
	if (bEnterIsolationMode && bHasSetSelection)
	{
		ShowOnlyAnimPathProxiesOfType(PathHandle.PathType, true);
	}
	return bHasSetSelection;
}

bool AITwinPathAnimTool::SelectPath(FAnimPathIdentifier PathHandle, bool bEnterIsolationMode)
{
	return Impl->SelectPath(PathHandle, bEnterIsolationMode);
}

std::optional<FAnimPathIdentifier> AITwinPathAnimTool::FImpl::GetSelectedPath() const
{
	AITwinSplineHelper const* SelectedSpline = nullptr;
	if (ensure(SplineTool.IsValid()) && SplineTool->IsUsedForPathAnim())
	{
		SelectedSpline = SplineTool->GetSelectedSpline();
	}
	if (SelectedSpline)
	{
		return GetPathIdentifier(SelectedSpline->GetAVizSplineId());
	}
	return std::nullopt;
}

std::optional<FAnimPathIdentifier> AITwinPathAnimTool::GetSelectedPath() const
{
	return Impl->GetSelectedPath();
}

void AITwinPathAnimTool::DeSelectAll(bool bExitIsolationMode)
{
	auto CurrentSelection = GetSelectedPath();
	if (CurrentSelection)
	{
		Impl->SelectSpline(nullptr, GetWorld());
		
		if (bExitIsolationMode)
		{
			// Restore visibility of proxies.
			Impl->SetAllAnimPathProxiesVisibility(true);
		}
	}
	BroadcastSelection();
}

void AITwinPathAnimTool::FImpl::UpdateAnimatedObjects(FAnimPathIdentifier PathHandle, float DeltaTime)
{
	if (auto PathInfo = GetMutableAnimPathInfo(PathHandle))
	{
		if (!PathInfo->HasBakedAnimation())
		{
			PathInfo->BakeAnimationIfNeeded();
			return;
		}

		for (auto Population : PathInfo->Populations)
		{
			if (!Population.IsValid())
				continue;
			for (int32 instIdx(0); instIdx < Population->GetNumberOfInstances(); instIdx++)
			{
				if (auto InstancePtr = Population->GetAVizInstance(instIdx))
				{
					auto inst = InstancePtr->GetRAutoLock();
					if (auto animPathExt = inst->GetExtension<InstanceWithAnimPathExt>())
					{
						auto transform = animPathExt->GetTransform(DeltaTime, PathInfo->GetRepeatMode(), PathInfo->HasInvDirection());
						Population->SetInstanceTransformUEOnly(instIdx, transform);
					}
				}
			}
		}
	}
}

void AITwinPathAnimTool::FImpl::Tick(float DeltaTime)
{
	for (EITwinAnimPathType Type : {
		EITwinAnimPathType::Object,
		EITwinAnimPathType::Traffic,
		EITwinAnimPathType::Crowd })
	{
		for (int32 Index(0); Index < NumPaths(Type); ++Index)
		{
			UpdateAnimatedObjects(FAnimPathIdentifier(Type, Index), DeltaTime);
		}
	}
}

void AITwinPathAnimTool::FImpl::ZoomOnPath(FAnimPathIdentifier PathHandle)
{
	auto PathInfo = GetAnimPathInfo(PathHandle);
	if (PathInfo && PathInfo->SplineHelper.IsValid())
	{
		// use overview camera for zoom
		Owner.OnOverviewCamera(PathInfo->SplineHelper.Get());
	}
}

void AITwinPathAnimTool::ZoomOnPath(FAnimPathIdentifier PathHandle)
{
	Impl->ZoomOnPath(PathHandle);
}

void AITwinPathAnimTool::FImpl::ResetAnimation(FAnimPathIdentifier PathHandle)
{
	if (!ensure(PathHandle.IsValid(NumPaths(PathHandle.PathType))))
		return;

	auto PathInfo = GetAnimPathInfo(PathHandle);
	if (!PathInfo)
		return;
	for (auto Population : PathInfo->Populations)
	{
		if (!Population.IsValid())
			continue;

		for (int32 instIdx(0); instIdx < Population->GetNumberOfInstances(); instIdx++)
		{
			if (auto InstancePtr = Population->GetAVizInstance(instIdx))
			{
				auto inst = InstancePtr->GetRAutoLock();
				if (auto animPathExt = inst->GetExtension<InstanceWithAnimPathExt>())
				{
					animPathExt->ResetAnimation(PathInfo->GetDelay());
				}
			}
		}
	}
}

void AITwinPathAnimTool::ResetAnimation(FAnimPathIdentifier PathHandle)
{
	Impl->ResetAnimation(PathHandle);
}

void AITwinPathAnimTool::FImpl::PlayAnimation(FAnimPathIdentifier PathHandle, bool bPlay)
{
	if (!ensure(PathHandle.IsValid(NumPaths(PathHandle.PathType))))
		return;

	auto PathInfo = GetAnimPathInfo(PathHandle);
	if (!PathInfo)
		return;
	for (auto Population : PathInfo->Populations)
	{
		if (!Population.IsValid())
			continue;

		for (int32 instIdx(0); instIdx < Population->GetNumberOfInstances(); instIdx++)
		{
			if (auto InstancePtr = Population->GetAVizInstance(instIdx))
			{
				auto inst = InstancePtr->GetRAutoLock();
				if (auto animPathExt = inst->GetExtension<InstanceWithAnimPathExt>())
				{
					animPathExt->SetPaused(!bPlay);
				}
			}
		}
	}
}

void AITwinPathAnimTool::PlayAnimation(FAnimPathIdentifier PathHandle, bool bPlay)
{
	Impl->PlayAnimation(PathHandle, bPlay);
}

void AITwinPathAnimTool::FImpl::SetAnimPathProxyVisibility(EITwinAnimPathType PathType, bool bVisibleInGame, bool bIsolationMode/* = false*/)
{
	for (int32 i(0); i < NumPaths(PathType); i++)
	{
		auto PathInfo = GetAnimPathInfo(FAnimPathIdentifier(PathType, i));
		if (PathInfo && PathInfo->SplineHelper.IsValid())
		{
			const bool bShowSpline = bVisibleInGame
				&& (!bIsolationMode || PathInfo->SplineHelper->IsSelected());
			PathInfo->SplineHelper->SetActorHiddenInGame(!bShowSpline);
		}
	}
}

bool AITwinPathAnimTool::FImpl::IsAnimPathProxyVisible(EITwinAnimPathType PathType) const
{
	for (int32 i(0); i < NumPaths(PathType); i++)
	{
		auto PathInfo = GetAnimPathInfo(FAnimPathIdentifier(PathType, i));
		if (PathInfo && PathInfo->SplineHelper.IsValid())
		{
			return !PathInfo->SplineHelper->IsHidden();
		}
	}

	return false;
}

void AITwinPathAnimTool::FImpl::SetAllAnimPathProxiesVisibility(bool bVisibleInGame)
{
	for (EITwinAnimPathType PathType : TEnumRange<EITwinAnimPathType>())
	{
		SetAnimPathProxyVisibility(PathType, bVisibleInGame);
	}
}

void AITwinPathAnimTool::FImpl::ShowOnlyAnimPathProxiesOfType(EITwinAnimPathType SelectedType, bool bIsolationMode)
{
	for (EITwinAnimPathType PathType : TEnumRange<EITwinAnimPathType>())
	{
		SetAnimPathProxyVisibility(PathType, PathType == SelectedType, bIsolationMode);
	}
}

void AITwinPathAnimTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	Impl->Tick(DeltaTime);
}

void AITwinPathAnimTool::OnActivatePicking(bool bActivate)
{
	if (bActivate)
	{
		// Beware the tool can be activated *after* the user selects animation path from the list in the UI: in
		// such case, we should not make all proxies visible, but instead preserve the current isolation
		// mode.
		auto const CurrentSelection = GetSelectedPath();
		if (CurrentSelection)
			Impl->ShowOnlyAnimPathProxiesOfType(CurrentSelection->PathType, true);
		else
			Impl->SetAllAnimPathProxiesVisibility(true);
	}
	else
	{
		Impl->HideAllAnimPathProxies();
	}
}

bool AITwinPathAnimTool::DoMouseClickPicking(bool& bOutSelectionGizmoNeeded)
{
	bool bRelevantAction = false;
	bOutSelectionGizmoNeeded = false;
	UWorld* World = GetWorld();
	if (!World)
		return false;

	auto const OldSelection = GetSelectedPath();

	if (Impl->ObjectAnimPathInfos.Num() > 0) // TODO: add support for other path types
	{
		auto SplineTool = Impl->ActivateSplineTool(World, EITwinAnimPathType::Object);
		if (SplineTool.IsValid())
		{
			// Quick fix for point selection/insertion: we need to restore the initial selection, if a
			// polygon was selected, as point operations are only allowed on the selected spline (and
			// the selection is lost when the spline tool is disabled through ActivatePopulationTool...)
			if (OldSelection
				&& OldSelection->PathType == EITwinAnimPathType::Object
				&& OldSelection->PathIndex >= 0
				&& OldSelection->PathIndex < Impl->ObjectAnimPathInfos.Num())
			{
				SplineTool->SetSelectedSpline(Impl->ObjectAnimPathInfos[OldSelection->PathIndex]->SplineHelper.Get());
			}
			bRelevantAction = SplineTool->DoMouseClickAction();
			if (bRelevantAction)
				bOutSelectionGizmoNeeded = SplineTool->HasSelection();
		}
	}

	auto const NewSelection = GetSelectedPath();
	if (NewSelection)
	{
		// Isolation of the selected item, if any.
		if (!OldSelection || OldSelection->PathType != NewSelection->PathType)
		{
			Impl->ShowOnlyAnimPathProxiesOfType(NewSelection->PathType, true);
		}
	}
	else if (OldSelection)
	{
		// End of isolation mode.
		Impl->SetAllAnimPathProxiesVisibility(true);
	}

	// Notify new selection. If nothing is selected, notify it as well (using -1 as index).
	BroadcastSelection();

	return bRelevantAction;
}

void AITwinPathAnimTool::BroadcastSelection()
{
	// Notify new selection (using -1 as index if nothing i selected)
	auto const NewSelection = GetSelectedPath();
	if (NewSelection)
	{
		AnimPathSelectedEvent.Broadcast(NewSelection.value());
	}
	else
	{
		AnimPathSelectedEvent.Broadcast(FAnimPathIdentifier());
	}
}

void AITwinPathAnimTool::OnOverviewCamera(AITwinSplineHelper const* SpecificSpline)
{
	UWorld* World = GetWorld();
	if (!World)
		return;
	TWeakObjectPtr<AITwinSplineTool> SplineTool = Impl->ActivateSplineTool(World, GetAnimPathTypeFromSplineUsage(SpecificSpline->GetUsage()));
	if (SplineTool.IsValid())
	{
		SplineTool->OnOverviewCamera(SpecificSpline);
	}
}

//template <typename Func>
//void AITwinPathAnimTool::FImpl::VisitAnimPathOfType(EITwinAnimPathType PathType, Func const& Fn)
//{
//	switch (PathType)
//	{
//	case EITwinAnimPathType::Object:
//		for (auto PathInfo : ObjectAnimPathInfos)
//		{
//			Fn(BoxInfo);
//		}
//		break;
//
//	case EITwinAnimPathType::Crowd:
//		for (auto PathInfo : CrowdAnimPathInfos)
//		{
//			Fn(PlaneInfo);
//		}
//		break;
//
//	case EITwinAnimPathType::Traffic:
//		for (auto PathInfo : TrafficAnimPathInfos)
//		{
//			Fn(PathInfo);
//		}
//		break;
//
//	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinAnimPathType::Count: );
//	}
//}

AdvViz::SDK::RefID AITwinPathAnimTool::FImpl::GetPathRefId(FAnimPathIdentifier PathHandle) const
{
	auto PathInfo = GetAnimPathInfo(PathHandle);
	return PathInfo && PathInfo->SplineHelper.IsValid() ? PathInfo->SplineHelper->GetAVizSplineId() : AdvViz::SDK::RefID::Invalid();
}

AdvViz::SDK::RefID AITwinPathAnimTool::GetPathRefId(FAnimPathIdentifier PathHandle) const
{
	return Impl->GetPathRefId(PathHandle);
}

FAnimPathIdentifier AITwinPathAnimTool::FImpl::GetPathIdentifier(AdvViz::SDK::RefID const& RefID) const
{
	int32 Index = ObjectAnimPathInfos.IndexOfByPredicate(
		[&RefID](TStrongObjectPtr<UITwinObjectAnimPathInfo> const InItem)
		{
			return InItem->SplineHelper.IsValid()
				&& InItem->SplineHelper->GetAVizSplineId() == RefID;
		});
	if (Index != INDEX_NONE)
		return FAnimPathIdentifier(EITwinAnimPathType::Object, Index);

	Index = TrafficAnimPathInfos.IndexOfByPredicate(
		[&RefID](TStrongObjectPtr<UITwinTrafficAnimPathInfo> const InItem)
		{
			return InItem->SplineHelper.IsValid()
				&& InItem->SplineHelper->GetAVizSplineId() == RefID;
		});
	if (Index != INDEX_NONE)
		return FAnimPathIdentifier(EITwinAnimPathType::Traffic, Index);

	Index = CrowdAnimPathInfos.IndexOfByPredicate(
		[&RefID](TStrongObjectPtr<UITwinCrowdAnimPathInfo> const InItem)
		{
			return InItem->SplineHelper.IsValid()
				&& InItem->SplineHelper->GetAVizSplineId() == RefID;
		});
	if (Index != INDEX_NONE)
		return FAnimPathIdentifier(EITwinAnimPathType::Crowd, Index);

	return FAnimPathIdentifier(EITwinAnimPathType::Count, INDEX_NONE);
}

FAnimPathIdentifier AITwinPathAnimTool::GetPathIdentifier(AdvViz::SDK::RefID const& RefID) const
{
	return Impl->GetPathIdentifier(RefID);
}

void AITwinPathAnimTool::AbortInteractiveCreation(bool bTriggeredFromITS)
{
	// Abort current animation path creation, if any
	AITwinInteractiveTool* ActiveTool = AITwinInteractiveTool::GetActiveTool(GetWorld());
	if (ActiveTool && ActiveTool->IsUsedForPathAnim())
	{
		if (ActiveTool->IsInteractiveCreationMode())
			ActiveTool->AbortInteractiveCreation(bTriggeredFromITS);
		ActiveTool->SetEnabled(false);
		ActiveTool->SetUsedForPathAnim(false);
	}
}

void AITwinPathAnimTool::Deactivate()
{
	// Abort current path creation, if any.
	AbortInteractiveCreation(true);

	// Deselect all, without changing the visibility (since we will hide all below...)
	DeSelectAll(false);

	// Trigger event to refresh the selection gizmo
	ActivationEvent.Broadcast(false);

	Impl->HideAllAnimPathProxies();
}

#pragma optimize("", on)