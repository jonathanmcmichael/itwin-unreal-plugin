/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPathAnimTool.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <PathAnimation/ITwinPathAnimTool.h>
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
#include <Decoration/ITwinDecorationHelper.h>
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

	TArray<TStrongObjectPtr<UITwinObjectAnimPathHelper> > ObjectAnimPaths;
	TArray<TStrongObjectPtr<UITwinTrafficAnimPathHelper> > TrafficAnimPaths;
	TArray<TStrongObjectPtr<UITwinCrowdAnimPathHelper> > CrowdAnimPaths;

	std::shared_ptr<AdvViz::SDK::IPathAnimManager> PathAnimManager;

	TWeakObjectPtr<AITwinPopulationTool> PopulationTool;
	TWeakObjectPtr<AITwinSplineTool> SplineTool;

	TWeakObjectPtr<AITwinDecorationHelper> DecorationHelper;

	FImpl(AITwinPathAnimTool& InOwner) : Owner(InOwner)
	{}

	inline int32 NumPaths(EITwinAnimPathType PathType) const;

	inline UITwinAnimPathHelper* GetMutableAnimPathHelper(FAnimPathIdentifier PathHandle);
	inline const UITwinAnimPathHelper* GetAnimPathHelper(FAnimPathIdentifier PathHandle) const;

	void LoadAnimationPaths();

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

	bool PopulatePathObjects(UITwinAnimPathHelper* PathHelper, bool bClearPrevious = false);
	bool PopulatePathObjects(FAnimPathIdentifier PathHandle, bool bClearPrevious = false);
	void RemovePathObjects(UITwinAnimPathHelper* PathHelper);
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

private:
	FAnimPathIdentifier GetPathIdentifierFromSpline(AdvViz::SDK::RefID const& RefID) const;
	UITwinAnimPathHelper* CreatePath(EITwinAnimPathType PathType);
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

void AITwinPathAnimTool::SetDecorationHelper(AITwinDecorationHelper* InDecoHelper)
{
	Impl->DecorationHelper = InDecoHelper;
}

void AITwinPathAnimTool::SetPathAnimManager(const std::shared_ptr<AdvViz::SDK::IPathAnimManager>& InPathAnimManager)
{
	Impl->PathAnimManager = InPathAnimManager;
}

void AITwinPathAnimTool::FImpl::LoadAnimationPaths()
{
	if (!ensure(PathAnimManager))
		return;

	std::set<AdvViz::SDK::RefID> AnimPathIds;
	PathAnimManager->GetAnimationPathIds(AnimPathIds);
	std::unordered_map<AdvViz::SDK::RefID, AITwinSplineHelper*> SplineRefIdToSplineMap;
	for (TActorIterator<AITwinSplineHelper> SplineIter(Owner.GetWorld()); SplineIter; ++SplineIter)
		SplineRefIdToSplineMap[SplineIter->GetAVizSplineId()] = *SplineIter;
	for (auto id : AnimPathIds)
	{
		if (auto PathPropPtr = PathAnimManager->GetAnimationPathInfo(id))
		{
			auto PathProp = PathPropPtr->GetRAutoLock();
			auto SplineRefID = PathProp->GetSplineId();
			if (!ensure(SplineRefID.IsValid() && SplineRefIdToSplineMap.contains(SplineRefID)))
				continue;
			auto SplineHelper = SplineRefIdToSplineMap[SplineRefID];
			EITwinAnimPathType PathType = GetAnimPathTypeFromSplineUsage(SplineHelper->GetUsage());
			if (auto PathHelper = CreatePath(PathType))
			{
				PathHelper->Init(SplineHelper, PathPropPtr);
				std::vector<std::string> assets;
				PathProp->GetObjects(assets);
				if (assets.size() > 0)
				{
					PathHelper->Set3DObjectsFromProps();
					PopulatePathObjects(PathHelper, false);
				}
			}
		}
	}
}

void AITwinPathAnimTool::LoadAnimationPaths()
{
	Impl->LoadAnimationPaths();
}

void AITwinPathAnimTool::FImpl::BakeAnimation(FAnimPathIdentifier PathHandle)
{
	if (auto PathHelper = GetMutableAnimPathHelper(PathHandle))
		PathHelper->BakeAnimationIfNeeded();
}

void AITwinPathAnimTool::FImpl::BakeAnimation()
{
	for (EITwinAnimPathType PathType : {
		EITwinAnimPathType::Object,
		EITwinAnimPathType::Traffic,
		EITwinAnimPathType::Crowd })
	{
		for (int32 Index(0); Index < NumPaths(PathType); ++Index)
		{
			BakeAnimation(FAnimPathIdentifier(PathType, Index));
		}
	}
}

void AITwinPathAnimTool::FImpl::RemovePathObjects(UITwinAnimPathHelper* PathHelper)
{
	if (!PopulationTool.IsValid() || !PathHelper)
		return;

	for (auto Population : PathHelper->Populations)
	{
		if (Population.IsValid())
			Population->RemoveAllInstances();
	}
	PathHelper->Populations.Empty();
}

void AITwinPathAnimTool::FImpl::RemovePathObjects(FAnimPathIdentifier PathHandle)
{
	RemovePathObjects(GetMutableAnimPathHelper(PathHandle));
}

bool AITwinPathAnimTool::FImpl::PopulatePathObjects(UITwinAnimPathHelper* PathHelper, bool bClearPrevious/* = false*/)
{
	if (!PathHelper || !PathHelper->SplineHelper.IsValid())
		return false;
	if (!ensure(PopulationTool.IsValid() && DecorationHelper.IsValid()))
		return false;

	if (bClearPrevious && PathHelper->Populations.Num() > 0)
	{
		RemovePathObjects(PathHelper);
	}

	// TODO: create multiple instances for crowd and traffic depending on density and asset dimensions (for traffic only)
	FVector Pos = PathHelper->SplineHelper->GetSplineComponent()->GetLocationAtDistanceAlongSpline(0.0, ESplineCoordinateSpace::World);
	auto InstGroupId = DecorationHelper->GetInstancesGroupIdForSpline(*(PathHelper->SplineHelper));
	auto Assets = PathHelper->Get3DObjectPaths();
	if (Assets.Num() > 0) // TODO: adapt to multiple objects (traffic and crowd)
	{
		PathHelper->BakeAnimationIfNeeded();

		AITwinPopulation* Population = DecorationHelper->GetOrCreatePopulation(Assets[0], InstGroupId);
		//AITwinPopulation* Population = PopulationTool->PreLoadPopulation(Assets[0]);
		if (Population)
		{
			PathHelper->Populations.Add(Population);
			int32 instIdx(0);
			if (Population->GetNumberOfInstances() == 0)
				instIdx = Population->AddInstance(FTransform(Pos));
			if (instIdx >= 0)
			{
				std::shared_ptr<InstanceWithAnimPathExt> animPathExt = std::make_shared<InstanceWithAnimPathExt>(FTransform(Pos));
				animPathExt->SetKeyFrames(PathHelper->GetBakedFrames());

				if (auto InstancePtr = Population->GetAVizInstance(instIdx))
				{
					auto Instance = InstancePtr->GetAutoLock();
					Instance->SetAnimPathId(PathHelper->GetPathRefID());
					Instance->AddExtension(animPathExt);
				}
			}
			return true;
		}
	}

	return false;
}

bool AITwinPathAnimTool::FImpl::PopulatePathObjects(FAnimPathIdentifier PathHandle, bool bClearPrevious/* = false*/)
{
	return PopulatePathObjects(GetMutableAnimPathHelper(PathHandle), bClearPrevious);
}

//FString AITwinPathAnimTool::GetName(FAnimPathIdentifier PathHandle) const
//{
//	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
//		return PathHelper->GetName();
//	return FString();
//}
//
//void AITwinPathAnimTool::SetName(FAnimPathIdentifier PathHandle, const FString& Name)
//{
//	Impl->GetMutableAnimPathHelper(PathHandle)->SetName(Name);
//}

void AITwinPathAnimTool::Get3DObjects(FAnimPathIdentifier PathHandle, TArray<FString>& Assets) const
{
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		PathHelper->Get3DObjects(Assets);
	else
		Assets.Empty();
}

void AITwinPathAnimTool::Set3DObjects(FAnimPathIdentifier PathHandle, const TArray<FString>& Assets)
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object || Assets.Num() == 1);
	auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle);
	if (!PathHelper)
		return;
	PathHelper->Set3DObjects(Assets);
	Impl->PopulatePathObjects(PathHelper, true); // add new objects and clear previous one if any (TODO: refactor)
}

bool AITwinPathAnimTool::HasInvDirection(FAnimPathIdentifier PathHandle) const
{
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->HasInvDirection();
	return false;
}

void AITwinPathAnimTool::SetInvDirection(FAnimPathIdentifier PathHandle, bool bInvDirection)
{
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
		PathHelper->SetInvDirection(bInvDirection);
}

bool AITwinPathAnimTool::IsLoop(FAnimPathIdentifier PathHandle) const
{
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->IsLoop();
	return false;
}

void AITwinPathAnimTool::SetIsLoop(FAnimPathIdentifier PathHandle, bool isLoop)
{
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
		PathHelper->SetIsLoop(isLoop);
		if (PathHelper->SplineHelper.IsValid())
			PathHelper->SplineHelper->SetClosedLoop(isLoop);
	}
}

float AITwinPathAnimTool::GetSpeed(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->GetSpeed();
	return 0.f;
}

void AITwinPathAnimTool::SetSpeed(FAnimPathIdentifier PathHandle, float Speed)
{
	//ensure(PathHandle.PathType == EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
		PathHelper->SetSpeed(Speed);
}

float AITwinPathAnimTool::GetDelay(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->GetDelay();
	return 0.f;
}

void AITwinPathAnimTool::SetDelay(FAnimPathIdentifier PathHandle, float Delay)
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
		PathHelper->SetDelay(Delay);
	Impl->ResetAnimation(PathHandle);
}

EITwinAnimPathRepeatMode AITwinPathAnimTool::GetRepeatMode(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->GetRepeatMode();
	return EITwinAnimPathRepeatMode::Count;
}

void AITwinPathAnimTool::SetRepeatMode(FAnimPathIdentifier PathHandle, EITwinAnimPathRepeatMode RepeatMode)
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
		PathHelper->SetRepeatMode(RepeatMode);
	Impl->ResetAnimation(PathHandle);
}

bool AITwinPathAnimTool::IsOneWay(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->IsOneWay();
	return true;
}

void AITwinPathAnimTool::SetOneWay(FAnimPathIdentifier PathHandle, bool bOneWay)
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
		PathHelper->SetOneWay(bOneWay);
		PathHelper->UpdateSpline();
	}
}

int AITwinPathAnimTool::GetLaneCount(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->GetLaneCount();
	return 0;
}

void AITwinPathAnimTool::SetLaneCount(FAnimPathIdentifier PathHandle, int LaneCount)
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
		PathHelper->SetLaneCount(LaneCount);
		PathHelper->UpdateSpline();
	}
}

float AITwinPathAnimTool::GetLaneWidth(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->GetLaneWidth();
	return 0;
}

void AITwinPathAnimTool::SetLaneWidth(FAnimPathIdentifier PathHandle, float LaneWidth)
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
		PathHelper->SetLaneWidth(LaneWidth);
		PathHelper->UpdateSpline();
	}
}

float AITwinPathAnimTool::GetDensity(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->GetDensity();
	return 0.f;
}

void AITwinPathAnimTool::SetDensity(FAnimPathIdentifier PathHandle, float Density)
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
		PathHelper->SetDensity(Density);
		// TODO: repopulate
	}
}

float AITwinPathAnimTool::GetSeparatorWidth(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Traffic);
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->GetSeparatorWidth();
	return 0.f;
}

void AITwinPathAnimTool::SetSeparatorWidth(FAnimPathIdentifier PathHandle, float SeparatorWidth)
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Traffic);
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
		PathHelper->SetSeparatorWidth(SeparatorWidth);
		PathHelper->UpdateSpline();
	}
}

float AITwinPathAnimTool::GetMinSpeed(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Traffic);
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->GetMinSpeed();
	return 0.f;
}

void AITwinPathAnimTool::SetMinSpeed(FAnimPathIdentifier PathHandle, float MinSpeed)
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Traffic);
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
		PathHelper->SetMinSpeed(MinSpeed);
}

float AITwinPathAnimTool::GetMaxSpeed(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Traffic);
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->GetMaxSpeed();
	return 0.f;
}

void AITwinPathAnimTool::SetMaxSpeed(FAnimPathIdentifier PathHandle, float MaxSpeed)
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Traffic);
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
		PathHelper->SetMaxSpeed(MaxSpeed);
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
		// Activate overview camera (top view)
		//SplineTool->OnOverviewCamera(); // no need for path animation for now
		SplineTool->StartInteractiveCreation();
		return true;
	}
	return false;
}

UITwinAnimPathHelper* AITwinPathAnimTool::FImpl::CreatePath(EITwinAnimPathType PathType)
{
	switch (PathType)
	{
	case EITwinAnimPathType::Object:
	{
		TStrongObjectPtr<UITwinObjectAnimPathHelper> ObjectPathHelper(NewObject<UITwinObjectAnimPathHelper>(&Owner));
		ObjectAnimPaths.Add(ObjectPathHelper);
		return ObjectPathHelper.Get();
	}
	case EITwinAnimPathType::Traffic:
	{
		TStrongObjectPtr<UITwinTrafficAnimPathHelper> TrafficPathHelper(NewObject<UITwinTrafficAnimPathHelper>(&Owner));
		TrafficAnimPaths.Add(TrafficPathHelper);
		return TrafficPathHelper.Get();
	}
	case EITwinAnimPathType::Crowd:
	{
		TStrongObjectPtr<UITwinCrowdAnimPathHelper> CrowdPathHelper(NewObject<UITwinCrowdAnimPathHelper>(&Owner));
		CrowdAnimPaths.Add(CrowdPathHelper);
		return CrowdPathHelper.Get();
	}
	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(case EITwinAnimPathType::Count:, nullptr);
	}
}

bool AITwinPathAnimTool::FImpl::RegisterAnimPathSpline(AITwinSplineHelper* SplineHelper)
{
	// This function is called when a new spline is created with the Spline Tool, and also when an existing spline
	// is loaded from the server. If we are loading an existing path animation spline from server, we should wait until
	// all the animation paths are loaded before registering them here
	if (SplineHelper->GetAVizSplineId().HasDBIdentifier())
		return false;

	FAnimPathIdentifier PathHandle;
	PathHandle.PathType = GetAnimPathTypeFromSplineUsage(SplineHelper->GetUsage());
	PathHandle.PathIndex = NumPaths(PathHandle.PathType);

	UITwinAnimPathHelper* PathHelper = CreatePath(PathHandle.PathType);
	if (!PathHelper)
		return false;

	//std::vector<std::string> assets;
	//auto PathPropPtr = PathAnimManager->FindAnimationPathInfoBySplineRefId(SplineHelper->GetAVizSplineId());
	//if (!PathPropPtr)
	//{
	// Creating new spline
	auto PathPropPtr = PathAnimManager->AddAnimationPathInfo();
	auto PathProp = PathPropPtr->GetAutoLock();
	PathProp->SetSplineId(SplineHelper->GetAVizSplineId());
	//if (DecorationHelper.IsValid())
	//	PathProp->SetInstGroupId(DecorationHelper->GetInstancesGroupIdForSpline(*SplineHelper));
	//}
	//else
	//{
	//	// Loading existing spline
	//	auto PathProp = PathPropPtr->GetAutoLock();
	//	PathProp->GetObjects(assets);
	//}
	PathHelper->Init(SplineHelper, PathPropPtr);
	//if (assets.size() > 0)
	//{
	//	TArray<FString> AssetPaths;
	//	for (const auto& asset : assets)
	//		AssetPaths.Add(FString(asset.c_str()));
	//	PathHelper->Set3DObjects(AssetPaths);
	//}

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

	auto PathHandle = GetPathIdentifierFromSpline(SplineBeingRemoved->GetAVizSplineId());
	if (ensure(PathHandle.IsValid(NumPaths(PathHandle.PathType))))
	{
		PathAnimManager->RemoveAnimationPathInfo(GetAnimPathHelper(PathHandle)->GetPathRefID());
		switch (PathHandle.PathType)
		{
		case EITwinAnimPathType::Object:
			ObjectAnimPaths.RemoveAt(PathHandle.PathIndex);
			break;
		case EITwinAnimPathType::Traffic:
			TrafficAnimPaths.RemoveAt(PathHandle.PathIndex);
			break;
		case EITwinAnimPathType::Crowd:
			CrowdAnimPaths.RemoveAt(PathHandle.PathIndex);
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
	auto PathHandle = GetPathIdentifierFromSpline(SplineTool->GetSelectedSpline()->GetAVizSplineId());
	if (auto PathHelper = GetMutableAnimPathHelper(PathHandle))
	{
		PathHelper->InvalidateBakedAnimation();
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
	case EITwinAnimPathType::Object:	return ObjectAnimPaths.Num();
	case EITwinAnimPathType::Traffic:	return TrafficAnimPaths.Num();
	case EITwinAnimPathType::Crowd:		return CrowdAnimPaths.Num();
	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(case EITwinAnimPathType::Count: , 0);
	}
}

int32 AITwinPathAnimTool::NumPaths(EITwinAnimPathType PathType) const
{
	return Impl->NumPaths(PathType);
}

inline UITwinAnimPathHelper* AITwinPathAnimTool::FImpl::GetMutableAnimPathHelper(FAnimPathIdentifier PathHandle)
{
	if (!PathHandle.IsValid(NumPaths(PathHandle.PathType)))
		return nullptr;
	switch (PathHandle.PathType)
	{
	BE_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH(case EITwinAnimPathType::Count: )
	case EITwinAnimPathType::Object:	return ObjectAnimPaths[PathHandle.PathIndex].Get();
	case EITwinAnimPathType::Traffic:	return TrafficAnimPaths[PathHandle.PathIndex].Get();
	case EITwinAnimPathType::Crowd:		return CrowdAnimPaths[PathHandle.PathIndex].Get();
	}
}

inline const UITwinAnimPathHelper* AITwinPathAnimTool::FImpl::GetAnimPathHelper(FAnimPathIdentifier PathHandle) const
{
	if (!PathHandle.IsValid(NumPaths(PathHandle.PathType)))
		return nullptr;
	switch (PathHandle.PathType)
	{
	BE_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH(case EITwinAnimPathType::Count:)
	case EITwinAnimPathType::Object:	return ObjectAnimPaths[PathHandle.PathIndex].Get();
	case EITwinAnimPathType::Traffic:	return TrafficAnimPaths[PathHandle.PathIndex].Get();
	case EITwinAnimPathType::Crowd:		return CrowdAnimPaths[PathHandle.PathIndex].Get();
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
	if (auto PathHelper = GetAnimPathHelper(PathHandle))
	{
		// Remove associated objects from the population tool, if any
		RemovePathObjects(PathHandle);
		// Remove spline and path animation info
		if (PathHelper->SplineHelper.IsValid() && ensure(SplineTool.IsValid()))
			SplineTool->DeleteSpline(PathHelper->SplineHelper.Get());
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
	auto PathHelper = GetAnimPathHelper(PathHandle);
	if (PathHelper && PathHelper->SplineHelper.IsValid())
	{
		SelectSpline(PathHelper->SplineHelper.Get(), Owner.GetWorld());

		bHasSetSelection = PathHelper->SplineHelper->IsSelected();
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
		return GetPathIdentifierFromSpline(SelectedSpline->GetAVizSplineId());
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
	if (auto PathHelper = GetMutableAnimPathHelper(PathHandle))
	{
		if (!PathHelper->HasBakedAnimation())
		{
			PathHelper->BakeAnimationIfNeeded();
			return;
		}

		for (auto Population : PathHelper->Populations)
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
						auto transform = animPathExt->GetTransform(DeltaTime, PathHelper->GetRepeatMode(), PathHelper->HasInvDirection());
						Population->SetInstanceTransformUEOnly(instIdx, transform);
					}
				}
			}
		}
	}
}

void AITwinPathAnimTool::FImpl::Tick(float DeltaTime)
{
	for (EITwinAnimPathType PathType : {
		EITwinAnimPathType::Object,
		EITwinAnimPathType::Traffic,
		EITwinAnimPathType::Crowd })
	{
		for (int32 Index(0); Index < NumPaths(PathType); ++Index)
		{
			UpdateAnimatedObjects(FAnimPathIdentifier(PathType, Index), DeltaTime);
		}
	}
}

void AITwinPathAnimTool::FImpl::ZoomOnPath(FAnimPathIdentifier PathHandle)
{
	auto PathHelper = GetAnimPathHelper(PathHandle);
	if (PathHelper && PathHelper->SplineHelper.IsValid())
	{
		// use overview camera for zoom
		Owner.OnOverviewCamera(PathHelper->SplineHelper.Get());
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

	auto PathHelper = GetAnimPathHelper(PathHandle);
	if (!PathHelper)
		return;
	for (auto Population : PathHelper->Populations)
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
					animPathExt->ResetAnimation(PathHelper->GetDelay());
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

	auto PathHelper = GetAnimPathHelper(PathHandle);
	if (!PathHelper)
		return;
	for (auto Population : PathHelper->Populations)
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
		auto PathHelper = GetAnimPathHelper(FAnimPathIdentifier(PathType, i));
		if (PathHelper && PathHelper->SplineHelper.IsValid())
		{
			const bool bShowSpline = bVisibleInGame
				&& (!bIsolationMode || PathHelper->SplineHelper->IsSelected());
			PathHelper->SplineHelper->SetActorHiddenInGame(!bShowSpline);
		}
	}
}

bool AITwinPathAnimTool::FImpl::IsAnimPathProxyVisible(EITwinAnimPathType PathType) const
{
	for (int32 i(0); i < NumPaths(PathType); i++)
	{
		auto PathHelper = GetAnimPathHelper(FAnimPathIdentifier(PathType, i));
		if (PathHelper && PathHelper->SplineHelper.IsValid())
		{
			return !PathHelper->SplineHelper->IsHidden();
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

	if (Impl->ObjectAnimPaths.Num() > 0) // TODO: add support for other path types
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
				&& OldSelection->PathIndex < Impl->ObjectAnimPaths.Num())
			{
				SplineTool->SetSelectedSpline(Impl->ObjectAnimPaths[OldSelection->PathIndex]->SplineHelper.Get());
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
//		for (auto PathHelper : ObjectAnimPaths)
//		{
//			Fn(PathHelper);
//		}
//		break;
//
//	case EITwinAnimPathType::Crowd:
//		for (auto PathHelper : CrowdAnimPaths)
//		{
//			Fn(PathHelper);
//		}
//		break;
//
//	case EITwinAnimPathType::Traffic:
//		for (auto PathHelper : TrafficAnimPaths)
//		{
//			Fn(PathHelper);
//		}
//		break;
//
//	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinAnimPathType::Count: );
//	}
//}

FAnimPathIdentifier AITwinPathAnimTool::FImpl::GetPathIdentifierFromSpline(AdvViz::SDK::RefID const& RefID) const
{
	int32 Index = ObjectAnimPaths.IndexOfByPredicate(
		[&RefID](TStrongObjectPtr<UITwinObjectAnimPathHelper> const InItem)
		{
			return InItem->SplineHelper.IsValid() && InItem->SplineHelper->GetAVizSplineId() == RefID;
		});
	if (Index != INDEX_NONE)
		return FAnimPathIdentifier(EITwinAnimPathType::Object, Index);

	Index = TrafficAnimPaths.IndexOfByPredicate(
		[&RefID](TStrongObjectPtr<UITwinTrafficAnimPathHelper> const InItem)
		{
			return InItem->SplineHelper.IsValid() && InItem->SplineHelper->GetAVizSplineId() == RefID;
		});
	if (Index != INDEX_NONE)
		return FAnimPathIdentifier(EITwinAnimPathType::Traffic, Index);

	Index = CrowdAnimPaths.IndexOfByPredicate(
		[&RefID](TStrongObjectPtr<UITwinCrowdAnimPathHelper> const InItem)
		{
			return InItem->SplineHelper.IsValid() && InItem->SplineHelper->GetAVizSplineId() == RefID;
		});
	if (Index != INDEX_NONE)
		return FAnimPathIdentifier(EITwinAnimPathType::Crowd, Index);

	return FAnimPathIdentifier(EITwinAnimPathType::Count, INDEX_NONE);
}

AdvViz::SDK::RefID AITwinPathAnimTool::FImpl::GetPathRefId(FAnimPathIdentifier PathHandle) const
{
	auto PathHelper = GetAnimPathHelper(PathHandle);
	return PathHelper ? PathHelper->GetPathRefID() : AdvViz::SDK::RefID::Invalid();
}

AdvViz::SDK::RefID AITwinPathAnimTool::GetPathRefId(FAnimPathIdentifier PathHandle) const
{
	return Impl->GetPathRefId(PathHandle);
}

FAnimPathIdentifier AITwinPathAnimTool::FImpl::GetPathIdentifier(AdvViz::SDK::RefID const& RefID) const
{
	int32 Index = ObjectAnimPaths.IndexOfByPredicate(
		[&RefID](TStrongObjectPtr<UITwinObjectAnimPathHelper> const InItem)
		{
			return InItem->GetPathRefID() == RefID;
		});
	if (Index != INDEX_NONE)
		return FAnimPathIdentifier(EITwinAnimPathType::Object, Index);

	Index = TrafficAnimPaths.IndexOfByPredicate(
		[&RefID](TStrongObjectPtr<UITwinTrafficAnimPathHelper> const InItem)
		{
			return InItem->GetPathRefID() == RefID;
		});
	if (Index != INDEX_NONE)
		return FAnimPathIdentifier(EITwinAnimPathType::Traffic, Index);

	Index = CrowdAnimPaths.IndexOfByPredicate(
		[&RefID](TStrongObjectPtr<UITwinCrowdAnimPathHelper> const InItem)
		{
			return InItem->GetPathRefID() == RefID;
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
