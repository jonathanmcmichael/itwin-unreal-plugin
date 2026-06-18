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
#include "Math/BoxSphereBounds.h"
#include "Math/Box.h"
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
#	include "SDK/Core/Tools/DelayedCall.h"
#include <Compil/AfterNonUnrealIncludes.h>


namespace ITwinSpline
{
	extern bool IsPathAnim(const EITwinSplineUsage Usage);
}

namespace
{
	bool IsSplineUsedForPathAnim(AITwinSplineHelper* SplineHelper)
	{
		return SplineHelper && ITwinSpline::IsPathAnim(SplineHelper->GetUsage());
	}

	EITwinSplineUsage GetSplineUsageFromAnimPathType(EITwinAnimPathType PathType)
	{
		switch (PathType)
		{
		case EITwinAnimPathType::Object:
			return EITwinSplineUsage::AnimPathObject;
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
		case EITwinSplineUsage::AnimPathObject:
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
	InstanceWithAnimPathExt(FTransform StartTransform) : initTransform_(StartTransform), lastTransform_(StartTransform) {};

	using AdvViz::SDK::Tools::TypeId<InstanceWithAnimPathExt>::GetTypeId;

	FTransform GetTransform(float DeltaTime, EITwinAnimPathRepeatMode repeatMode, bool bReverse, float Delay, bool bTimelineMode)
	{
		if (!keyFrames_.IsValid() || keyFrames_->NeedsUpdate())
			return initTransform_;
		
		// When camera timeline is open, DeltaTime is actually current timeline time
		if (bTimelineMode)
			curTime_ = DeltaTime + startTime_ - Delay;
		else
			curTime_ += DeltaTime;

		if (curTime_ < 0)
			return initTransform_;

		float animTime(curTime_);
		float duration(keyFrames_->GetTotalTime());
		if (repeatMode == EITwinAnimPathRepeatMode::None)
		{
			if (curTime_ > duration)
			{
				animTime = duration;
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

	void ResetAnimation(float Delay, std::optional<float> StartTime = std::nullopt, std::optional<FTransform> StartTransform = std::nullopt)
	{
		if (StartTime.has_value())
			startTime_ = StartTime.value();
		if (StartTransform.has_value())
			initTransform_ = StartTransform.value();

		curTime_ = startTime_ - Delay;
		lastTransform_ = initTransform_;
	}

	int32 GetLaneIndex() const
	{
		if (keyFrames_.IsValid())
			return keyFrames_->GetLaneIndex();
		return 0;
	}

private:
	FTransform initTransform_;
	FTransform lastTransform_;
	TWeakObjectPtr<UBakedAnimKeyFrames> keyFrames_;
	float curTime_ = 0.f;
	float startTime_ = 0.f; // per instance start time (used to distribute objects on the path, for example)
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

	void PopulatePathObjects(UITwinAnimPathHelper* PathHelper, bool bOnSceneLoad, bool bClearPrevious = false);

	void RemovePathObjects(UITwinAnimPathHelper* PathHelper);
	void RemovePathObjects(FAnimPathIdentifier PathHandle);

	void HidePathAndObjects(UITwinAnimPathHelper* PathHelper, bool bHide);

	// Make the Spline Tool the active tool, with usage restricted to the creation of the given type of animation path
	TWeakObjectPtr<AITwinSplineTool> ActivateSplineTool(UWorld* World, EITwinAnimPathType PathType);

	// Select a spline, or reset selection if InSplineHelper is null
	void SelectSpline(AITwinSplineHelper* InSplineHelper, UWorld* World);

	void OnActivatePicking(bool bActivate);
	bool DoMouseClickPicking(bool& bOutSelectionGizmoNeeded);

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

	void UpdateAnimatedObjects(FAnimPathIdentifier PathHandle, float DeltaTime, bool bTimelineMode);
	void UpdateAllAnimatedObjects(float DeltaTime);

	// Verifies whether the populations for the path assets are fully loaded.
	// If called when loading a scene, it also checks whether the path asset instances have finished loading.
	bool ArePopulationsFullyLoaded(UITwinAnimPathHelper* PathHelper, bool bOnSceneLoad);

private:
	FAnimPathIdentifier GetPathIdentifierFromSpline(AdvViz::SDK::RefID const& RefID) const;
	UITwinAnimPathHelper* CreatePath(EITwinAnimPathType PathType);
	void DoPopulatePathObjects(UITwinAnimPathHelper* PathHelper);
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
	ensure(SplineTool.IsValid());
	return ITwin::ActivateSplineTool(World, GetSplineUsageFromAnimPathType(PathType), SplineTool);
}

void AITwinPathAnimTool::FImpl::SelectSpline(AITwinSplineHelper* SplineHelper, UWorld* World)
{
	ensure(SplineTool.IsValid());
	ITwin::SelectSpline(SplineHelper, INDEX_NONE, World, SplineTool);
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
		SplineTool->SplinePointMovedEvent.AddUniqueDynamic(this, &AITwinPathAnimTool::OnSplinePointMovedInTool);
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
					PopulatePathObjects(PathHelper, true/*bOnSceneLoad*/);
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

void AITwinPathAnimTool::FImpl::HidePathAndObjects(UITwinAnimPathHelper* PathHelper, bool bHide)
{
	if (!PathHelper)
		return;
	for (auto Population : PathHelper->Populations)
	{
		if (!Population.IsValid())
			continue;
		Population->SetHiddenInGame(bHide);
	}
	if (PathHelper->SplineHelper.IsValid())
		PathHelper->SplineHelper->SetActorHiddenInGame(bHide);
}

bool AITwinPathAnimTool::FImpl::ArePopulationsFullyLoaded(UITwinAnimPathHelper* PathHelper, bool bOnSceneLoad)
{
	auto InstGroupId = DecorationHelper->GetInstancesGroupIdForSpline(*(PathHelper->SplineHelper));
	auto Assets = PathHelper->Get3DObjectPaths();
	for (auto asset : Assets)
	{
		AITwinPopulation* Population = DecorationHelper->GetPopulation(asset, InstGroupId);
		if (!Population || Population->GetNumberOfInstances() == 0)
			return false;
	}
	return true;
}

namespace {
	float GetMinInterObjectDistance(float fSpeed, bool bDrive = false)
	{
		// Minimum allowed distance between objects is proportional to object speed;
		// it's set to 30cm for speed <=1km/h and to 10m (initial object placement)
		// or 5m (when driving a vehicle) for speed >=130km/h
		float speed = std::clamp(0.036f * fSpeed, 1.f, 130.f); // fSpeed is in cm/s
		float lowSpeedDistance(30.f);
		float highSpeedDistance(bDrive ? 500.f : 1000.f);
		return (highSpeedDistance * (speed - 1.f) + lowSpeedDistance * (130.f - speed)) / 129.f;
	}

	bool PopulationCanBeAnimated(AITwinPopulation* Population)
	{
		// TODO: move filter to UI and support other types of objects if needed
		return (Population->GetObjectType() == EITwinInstantiatedObjectType::Character
			|| Population->GetObjectType() == EITwinInstantiatedObjectType::Vehicle
			|| Population->GetObjectType() == EITwinInstantiatedObjectType::Crane);
	}

	class AssetSelector
	{
	public:
		AssetSelector(uintptr_t Seed)
		{
			RandomStream = FRandomStream(Seed);
		}

		void AddAsset(FString InAsset, FBox InBBox)
		{
			if (InBBox.GetSize().Y <= 0.f || InBBox.GetSize().Y > 5000.f)
			{
				UE_LOG(LogTemp, Warning, TEXT("Asset %s has invalid bounding box size. Please check that the asset's bounding box is correctly set up."), *InAsset);
				return;
			}

			if (AssetToSizeMap.Contains(InAsset))
				return;

			BE_LOGI("App", "Adding asset " << TCHAR_TO_UTF8(*InAsset) << ": x=" << InBBox.GetSize().X << ", y=" << InBBox.GetSize().Y << ", z=" << InBBox.GetSize().Z);

			AssetToSizeMap.Add(InAsset, InBBox);
			if (InBBox.GetSize().Y < 800.f)
				StandardAssetsOnly.Add(InAsset);
			else
				LongAssetsOnly.Add(InAsset);
		}

		FString GetRandomAsset(bool bIncludeLongAssets)
		{
			if (AssetToSizeMap.Num() == 0)
				return FString();
			if (bIncludeLongAssets && LongAssetsOnly.Num() > 0 && FMath::FRand() <= LongAssetFactor || StandardAssetsOnly.Num() == 0)
				return LongAssetsOnly.Array()[RandomStream.RandRange(0, LongAssetsOnly.Num() - 1)];
			else
				return StandardAssetsOnly.Array()[RandomStream.RandRange(0, StandardAssetsOnly.Num() - 1)];
		}

		float GetAssetLength(FString InAsset)
		{
			if (AssetToSizeMap.Contains(InAsset))
			{
				return AssetToSizeMap[InAsset].GetSize().Y;
			}
			return 0.f;
		}

		FVector2D GetAssetRange(FString InAsset)
		{
			if (AssetToSizeMap.Contains(InAsset))
			{
				return FVector2D(AssetToSizeMap[InAsset].Min.Y, AssetToSizeMap[InAsset].Max.Y);
			}
			return FVector2D::ZeroVector;
		}

	private:
		FRandomStream RandomStream;
		TMap<FString, FBox> AssetToSizeMap;
		TSet<FString> StandardAssetsOnly;
		TSet<FString> LongAssetsOnly;
		float LongAssetFactor = 0.3f; // TODO: add this parameter to UI?
	};
}

void AITwinPathAnimTool::FImpl::DoPopulatePathObjects(UITwinAnimPathHelper* PathHelper)
{
	// Let's keep track of the existing population instances created for this path and try to reuse them when possible.
	TMap<TWeakObjectPtr<AITwinPopulation>, int32> ReusedPopulationInstancesMap; // map population to number of instances that are reused in current path configuration
	for (auto Population : PathHelper->Populations)
	{
		if (Population.IsValid())
			ReusedPopulationInstancesMap.Add(Population, 0);
	}
	PathHelper->Populations.Empty();

	auto InstGroupId = DecorationHelper->GetInstancesGroupIdForSpline(*(PathHelper->SplineHelper));
	auto Assets = PathHelper->Get3DObjectPaths();

	if (!PathHelper->CanHaveMultipleObjects())
	{
		// Case of animation paths having only one object
		ensure(Assets.Num() == 1 && PathHelper->Populations.Num() <= 1);
		AITwinPopulation* Population = DecorationHelper->GetPopulation(Assets[0], InstGroupId);
		if (!ensure(Population))
			return;
		if (!PopulationCanBeAnimated(Population))
			return;
		// If path has already been populated with a different asset, remove corresponding instance
		if (PathHelper->Populations.Num() > 0 && PathHelper->Populations.Array()[0] != Population)
			RemovePathObjects(PathHelper); // note that it won't delete existing populations when loading the scene as PathHelper->Populations hasn't been initialized yet
		PathHelper->Populations.Add(Population);
		auto StartTransform = PathHelper->GetStartTransform(0);
		int32 instIdx(0);
		if (Population->GetNumberOfInstances() == 0)
			instIdx = Population->AddInstance(StartTransform);
		if (!ensure(instIdx != INDEX_NONE))
			return;
		if (auto InstancePtr = Population->GetAVizInstance(instIdx))
		{
			std::shared_ptr<InstanceWithAnimPathExt> animPathExt = std::make_shared<InstanceWithAnimPathExt>(StartTransform);
			animPathExt->SetKeyFrames(PathHelper->GetBakedFrames());
			auto Instance = InstancePtr->GetAutoLock();
			Instance->SetAnimPathId(PathHelper->GetPathRefID());
			Instance->AddExtension(animPathExt);
		}
	}
	else
	{
		// Case of animation paths that can have multiple objects (crowds, traffic)
		AssetSelector Selector(reinterpret_cast<uintptr_t>(PathHelper->SplineHelper.Get()));
		for (auto asset : Assets)
		{
			AITwinPopulation* Population = DecorationHelper->GetPopulation(asset, InstGroupId);
			if (!ensure(Population))
				continue;
			if (!PopulationCanBeAnimated(Population))
				continue;
			PathHelper->Populations.Add(Population);
			// Population instances related to the given assets can already exist in the scene even if PathHelper->Populations
			// was empty (in particular, when loading a scene), so we add them to the reused instances map as well.
			ReusedPopulationInstancesMap.Add(Population, 0);
			Selector.AddAsset(asset, Population->GetMasterMeshBoundingBox());
		}
		for (int32 lane(0); lane < PathHelper->GetFullLaneCount(); ++lane)
		{
			std::vector<AdvViz::SDK::IInstancePtr> laneInstances;
			int32 nbInstances(0);
			int32 nbAttempts(0);
			float laneOffset = PathHelper->GetLaneOffset(lane, false);
			float laneSpeed = PathHelper->GetLaneSpeed(lane);
			float laneLength = PathHelper->GetLaneLength(lane);
			float minInterObjectDist = GetMinInterObjectDistance(laneSpeed);
			float targetObjectLength = laneLength * PathHelper->GetLaneDensity(lane);
			float totalObjectLength = 0.f;
			auto startTransform = PathHelper->GetStartTransform(lane);
			// Create instances to populate the lane by randomly picking up assets
			// and taking into account their dimensions to approximately respect the lane density
			while(totalObjectLength < laneLength)
			{
				if (laneInstances.size() > laneLength * 0.03f) // normally more than 3 instances per 1m indicate some issue
				{
					// Triggered once due to 'nan' Y dimension of an asset when rebuilding traffic, needs to be investigated 
					UE_LOG(LogTemp, Warning, TEXT("Number of instances (%d) is too high for lane %d of path %s. Stopping population for this lane. Please check that the assets bounding boxes are correct."), laneInstances.size(), lane, *PathHelper->GetPathName());
					break;
				}
				if (nbInstances == laneInstances.size()) // avoid infinite loop in case of repeated failure to pick up a suitable asset
				{
					if (++nbAttempts > 100)
					{
						UE_LOG(LogTemp, Warning, TEXT("Too many attempts to populate lane %d of path %s. Stopping population for this lane. Please check that the assets bounding boxes are correct."), lane, *PathHelper->GetPathName());
						break;
					}
				}
				else
				{
					nbAttempts = 0;
					++nbInstances;
				}

				FString asset = Selector.GetRandomAsset(PathHelper->IsSlowLane(lane));
				if (asset.IsEmpty())
					continue;

				AITwinPopulation* Population = DecorationHelper->GetPopulation(asset, InstGroupId);
				float objectLength = Selector.GetAssetLength(asset) + minInterObjectDist;
				if (totalObjectLength + objectLength > targetObjectLength)
					break;
				int32 instIdx(INDEX_NONE);
				if (ReusedPopulationInstancesMap.Contains(Population) && Population->GetNumberOfInstances() > ReusedPopulationInstancesMap[Population])
				{
					instIdx = ReusedPopulationInstancesMap[Population];
					ReusedPopulationInstancesMap[Population]++;
				}
				else
					instIdx = Population->AddInstance(startTransform);

				if (!ensure(instIdx != INDEX_NONE))
					break;

				if (auto InstancePtr = Population->GetAVizInstance(instIdx))
				{
					std::shared_ptr<InstanceWithAnimPathExt> animPathExt = std::make_shared<InstanceWithAnimPathExt>(startTransform);
					animPathExt->SetKeyFrames(PathHelper->GetBakedFrames(lane));
					auto Instance = InstancePtr->GetAutoLock();
					Instance->SetAnimPathId(PathHelper->GetPathRefID());
					if (Instance->HasExtension<InstanceWithAnimPathExt>())
						Instance->RemoveExtension<InstanceWithAnimPathExt>();
					Instance->AddExtension(animPathExt);
					laneInstances.push_back(InstancePtr);
				}
				totalObjectLength += objectLength;
			}
			if (laneInstances.size() == 0)
				continue;

			// Once instances are created, compute the actual inter-object distance to distribute them evenly on the lane
			float interObjectDist = (laneLength - totalObjectLength) / laneInstances.size();			
			interObjectDist += minInterObjectDist; // since totalObjectLength incorporates min inter-object distance, we need to add it as well

			// Actually distribute objects on the lane by applying a time offset to each instance animation extension,
			// taking into account the object dimensions to avoid overlaps
			float initPosOffsetPrev = 0.f;
			FString assetPrev;
			for (auto InstancePtr : laneInstances)
			{
				auto Instance = InstancePtr->GetAutoLock();
				FString asset = UTF8_TO_TCHAR(Instance->GetObjectRef().c_str());
				float initPosOffset = 0.f;
				if (InstancePtr == laneInstances[0])
				{
					// Shift slightly the first object to avoid first row of objects on all the lanes being aligned
					// (the remaining objects will be distributed unevenly due to slight fluctuations of density between lanes)
					initPosOffset = std::min(0.5f * interObjectDist, 30.f + FMath::FRandRange(0.f, 200.f));
				}
				else
				{
					// Ignore the eventual shift of the first object
					initPosOffset = (InstancePtr == laneInstances[1]) ? 0.f : initPosOffsetPrev;
					// Add average inter-vehicle distance for this lane
					initPosOffset += interObjectDist;
					// Take into account vehicle dimensions
					auto assetRange = Selector.GetAssetRange(asset);
					auto assetRangePrev = Selector.GetAssetRange(assetPrev);
					initPosOffset += FMath::Abs(assetRange.Y) + FMath::Abs(assetRangePrev.X);
					ensure(initPosOffset < laneLength); // usually indicates a problem with a bounding box
				}
				//if (GetItemType() == BRW_LRTCharacters)
				//{
				//	//compute random variation
				//	float randomness = (float)std::fmod(randomGen.RandFloat() * 10.f, 1.f);
				//	randomOffset = (lane.interVehicleDist + vehicleInfoPrev.distToNextNoGap) * (0.4f * (float)glm::sin(randomness * 2 * PI) + 0.1f);
				//}
				// Compute final time shift that will be used by the animation system to actually distribute vehicles on the lane
				float initTimeOffset = (initPosOffset/* + randomOffset*/) / laneSpeed;
				if (auto animPathExt = Instance->GetExtension<InstanceWithAnimPathExt>())
				{
					auto startTransformWithOffset = animPathExt->GetTransform(initTimeOffset, PathHelper->GetRepeatMode(), PathHelper->IsInvDirLane(lane), PathHelper->GetDelay(), true/*bTimelineMode*/);
					animPathExt->ResetAnimation(PathHelper->GetDelay(), initTimeOffset, startTransformWithOffset);
				}
				assetPrev = asset;
				initPosOffsetPrev = initPosOffset;
			}
		} // for each lane

		// Now we should remove the excess instances that were potentially remaining from previous traffic and were not reused.
		for (auto& [Population, NextIdx] : ReusedPopulationInstancesMap)
		{
			while (Population->GetNumberOfInstances() > NextIdx)
			{
				Population->RemoveInstance(Population->GetNumberOfInstances() - 1);
			}
		}
	}
}

void AITwinPathAnimTool::FImpl::PopulatePathObjects(UITwinAnimPathHelper* PathHelper, bool bOnSceneLoad, bool bClearPrevious/* = false*/)
{
	if (!PathHelper || !PathHelper->SplineHelper.IsValid())
		return;
	if (!ensure(PopulationTool.IsValid() && DecorationHelper.IsValid()))
		return;

	if (bClearPrevious && PathHelper->Populations.Num() > 0)
	{
		RemovePathObjects(PathHelper);
	}

	auto InstGroupId = DecorationHelper->GetInstancesGroupIdForSpline(*(PathHelper->SplineHelper));
	auto Assets = PathHelper->Get3DObjectPaths();
	if (Assets.Num() > 0)
	{
		PathHelper->BakeAnimationIfNeeded();

		bool bAllAssetsLoaded = true;
		for (auto asset : Assets)
		{
			// We do not want to trigger new population creation when loading an existing scene, just wait for all populations to load and then create the link
			AITwinPopulation* Population = bOnSceneLoad ? DecorationHelper->GetPopulation(asset, InstGroupId) : DecorationHelper->GetOrCreatePopulation(asset, InstGroupId);
			if (!Population)
				bAllAssetsLoaded = false;
		}

		if (bAllAssetsLoaded)
			return DoPopulatePathObjects(PathHelper);

		// Delay adding objects to path until all items have been completely loaded from the component center.
		TWeakObjectPtr<AITwinPathAnimTool> weakOwner(&Owner);
		std::string const delayedCallId = "RetryPopulatePath_" + std::to_string(reinterpret_cast<uintptr_t>(PathHelper));
		AdvViz::SDK::UniqueDelayedCall(delayedCallId,
			[weakOwner, PathHelper, bOnSceneLoad]() -> AdvViz::SDK::DelayedCall::EReturnedValue
			{
				if (!weakOwner.IsValid())
					return AdvViz::SDK::DelayedCall::EReturnedValue::Done;

				if (weakOwner->Impl->ArePopulationsFullyLoaded(PathHelper, bOnSceneLoad))
				{
					weakOwner->Impl->DoPopulatePathObjects(PathHelper);
					return AdvViz::SDK::DelayedCall::EReturnedValue::Done;
				}

				return AdvViz::SDK::DelayedCall::EReturnedValue::Repeat;
			},
			0.25f);
	}
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
	Impl->PopulatePathObjects(PathHelper, false/*bOnSceneLoad*/);
}

bool AITwinPathAnimTool::IsVisible(FAnimPathIdentifier PathHandle) const
{
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->IsVisible();
	return false;
}

void AITwinPathAnimTool::SetVisible(FAnimPathIdentifier PathHandle, bool isVisible)
{
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
		PathHelper->SetVisible(isVisible);
		Impl->HidePathAndObjects(PathHelper, !isVisible);
	}
}

void AITwinPathAnimTool::SetAllVisible(bool isVisible)
{
	for (EITwinAnimPathType PathType : {
		EITwinAnimPathType::Object,
		EITwinAnimPathType::Traffic,
		EITwinAnimPathType::Crowd })
	{
		for (int32 Index(0); Index < NumPaths(PathType); ++Index)
		{
			SetVisible(FAnimPathIdentifier(PathType, Index), isVisible);
		}
	}
}

void AITwinPathAnimTool::TriggerRebake(UITwinAnimPathHelper* PathHelper, bool bMultiObjectOnly)
{
	if (!bMultiObjectOnly || PathHelper->CanHaveMultipleObjects())
		PathHelper->InvalidateBakedAnimation();
}

void AITwinPathAnimTool::TriggerRepopulate(UITwinAnimPathHelper* PathHelper, bool bMultiObjectOnly)
{
	if (!bMultiObjectOnly || PathHelper->CanHaveMultipleObjects())
		Impl->PopulatePathObjects(PathHelper, false/*bOnSceneLoad*/);
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
	{
		PathHelper->SetInvDirection(bInvDirection);
		TriggerRebake(PathHelper, true);
		TriggerRepopulate(PathHelper, true);
	}
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
		PathHelper->UpdateSpline();
		TriggerRebake(PathHelper, false);
		TriggerRepopulate(PathHelper, true);
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
	{
		PathHelper->SetSpeed(Speed);
		TriggerRepopulate(PathHelper, true);
	}
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
	{
		PathHelper->SetDelay(Delay);
		Impl->ResetAnimation(PathHandle);
	}
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
	{
		PathHelper->SetRepeatMode(RepeatMode);
		Impl->ResetAnimation(PathHandle);
	}
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
		TriggerRebake(PathHelper, true);
		TriggerRepopulate(PathHelper, true);
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
		TriggerRebake(PathHelper, true);
		TriggerRepopulate(PathHelper, true);
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
		TriggerRebake(PathHelper, true);
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
		TriggerRepopulate(PathHelper, true);
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
		TriggerRebake(PathHelper, true);
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
	{
		PathHelper->SetMinSpeed(MinSpeed);
		TriggerRepopulate(PathHelper, true);
	}
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
	{
		float prevMaxSpeed = PathHelper->GetMaxSpeed();
		PathHelper->SetMaxSpeed(MaxSpeed);
		// Avoid unnecessary repopulation (TODO: add min/max check directly in the UI)
		if (prevMaxSpeed != MaxSpeed && (MaxSpeed > PathHelper->GetMinSpeed() || prevMaxSpeed > PathHelper->GetMinSpeed()))
			TriggerRepopulate(PathHelper, true);
	}
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
	if (PathHandle.IsValid(NumPaths(PathHandle.PathType))) // path handle can be invalid if the spline creation was cancelled (and therefore the path isn't registered yet)
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
	if (SplineTool->IsInteractiveCreationMode())
	{
		// Path not yet created, no need to invalidate anything.
		return;
	}
	auto PathHandle = GetPathIdentifierFromSpline(SplineTool->GetSelectedSpline()->GetAVizSplineId());
	if (auto PathHelper = GetMutableAnimPathHelper(PathHandle))
	{
		PathHelper->InvalidateBakedAnimation();
	}
}

void AITwinPathAnimTool::OnSplineEditedInTool()
{
	// TODO: this is triggered even on spline point selection - find another solution to avoid unnecessary invalidation of baked animation 
	Impl->OnSplineEditedInTool();
}

void AITwinPathAnimTool::OnSplinePointMovedInTool(bool bTriggeredFromITS)
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

int32 AITwinPathAnimTool::NumPaths() const
{
	int32 totalPaths(0);
	for (EITwinAnimPathType PathType : {
		EITwinAnimPathType::Object,
		EITwinAnimPathType::Traffic,
		EITwinAnimPathType::Crowd })
	{
		totalPaths += NumPaths(PathType);
	}
	return totalPaths;
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

void AITwinPathAnimTool::FImpl::UpdateAnimatedObjects(FAnimPathIdentifier PathHandle, float DeltaTime, bool bTimelineMode)
{
	if (auto PathHelper = GetMutableAnimPathHelper(PathHandle))
	{
		if (!PathHelper->IsVisible() || PathHelper->IsPaused())
			return;
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
						auto transform = animPathExt->GetTransform(DeltaTime, PathHelper->GetRepeatMode(), PathHelper->IsInvDirLane(animPathExt->GetLaneIndex()), PathHelper->GetDelay(), bTimelineMode);
						Population->SetInstanceTransformUEOnly(instIdx, transform);
					}
				}
			}
		}
	}
}

void AITwinPathAnimTool::FImpl::UpdateAllAnimatedObjects(float DeltaTime)
{
	bool bTimelineMode(false);
	if (Owner.GetTimelineFixedTime.IsBound())
	{
		auto TimelineFixedTime = Owner.GetTimelineFixedTime.Execute();
		if (TimelineFixedTime.IsSet())
		{
			bTimelineMode = true;
			DeltaTime = TimelineFixedTime.GetValue();
		}
	}

	for (EITwinAnimPathType PathType : {
		EITwinAnimPathType::Object,
		EITwinAnimPathType::Traffic,
		EITwinAnimPathType::Crowd })
	{
		for (int32 Index(0); Index < NumPaths(PathType); ++Index)
		{
			UpdateAnimatedObjects(FAnimPathIdentifier(PathType, Index), DeltaTime, bTimelineMode);
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
	if (auto PathHelper = GetMutableAnimPathHelper(PathHandle))
		PathHelper->SetPaused(!bPlay);
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
				&& (!bIsolationMode || PathHelper->SplineHelper->IsSelected())
				&& PathHelper->IsVisible();
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

	Impl->UpdateAllAnimatedObjects(DeltaTime);
}

void AITwinPathAnimTool::FImpl::OnActivatePicking(bool bActivate)
{
	if (bActivate)
	{
		// Beware the tool can be activated *after* the user selects animation path from the list in the UI: in
		// such case, we should not make all proxies visible, but instead preserve the current isolation
		// mode.
		auto const CurrentSelection = GetSelectedPath();
		if (CurrentSelection)
			ShowOnlyAnimPathProxiesOfType(CurrentSelection->PathType, true);
		else
		{
			if (!SplineTool->IsEnabled() || !SplineTool->IsUsedForPathAnim())
				ITwin::ActivateSplineTool(Owner.GetWorld(), EITwinSplineUsage::AnimPath, SplineTool);
			SetAllAnimPathProxiesVisibility(true);
		}
	}
	else
	{
		HideAllAnimPathProxies();
	}
}

void AITwinPathAnimTool::OnActivatePicking(bool bActivate)
{
	Impl->OnActivatePicking(bActivate);
}

bool AITwinPathAnimTool::FImpl::DoMouseClickPicking(bool& bOutSelectionGizmoNeeded)
{
	bOutSelectionGizmoNeeded = false;

	if (ensure(SplineTool.IsValid()))
	{
		// Spline tool should already be activated when path selection mode is set (see AITwinPathAnimTool::FImpl::OnActivatePicking)
		ensure(SplineTool->IsEnabled() && SplineTool->IsUsedForPathAnim());

		if (SplineTool->DoMouseClickAction())
		{
			bOutSelectionGizmoNeeded = SplineTool->HasSelection();
			return true;
		}
	}

	return false;
}

bool AITwinPathAnimTool::DoMouseClickPicking(bool& bOutSelectionGizmoNeeded)
{
	bool bRelevantAction = false;
	if (NumPaths() > 0)
		bRelevantAction = Impl->DoMouseClickPicking(bOutSelectionGizmoNeeded);

	auto const NewSelection = GetSelectedPath();
	if (NewSelection)
	{
		// Isolation of the selected item, if any.
		Impl->ShowOnlyAnimPathProxiesOfType(NewSelection->PathType, true);
	}
	else
	{
		// End of isolation mode.
		Impl->SetAllAnimPathProxiesVisibility(true);
	}

	// Notify new selection. If nothing is selected, notify it as well
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
