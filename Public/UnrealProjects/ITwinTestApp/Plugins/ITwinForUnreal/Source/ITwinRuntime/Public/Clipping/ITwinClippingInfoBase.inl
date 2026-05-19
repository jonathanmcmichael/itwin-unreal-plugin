/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingInfoBase.inl $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Clipping/ITwinClippingInfoBase.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#include <Compil/AfterNonUnrealIncludes.h>


inline FITwinClippingInfluenceInfo& FITwinClippingInfoBase::MutableInfluenceInfo(EITwinModelType ModelType)
{
	switch (ModelType)
	{
		BE_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH(
	case EITwinModelType::AnimationKeyframe:
	case EITwinModelType::Scene:
	case EITwinModelType::Invalid:)

	case EITwinModelType::GlobalMapLayer: return GlobalMapLayersInfluenceInfo;
	case EITwinModelType::IModel: return IModelInfluenceInfo;
	case EITwinModelType::RealityData: return RealityDataInfluenceInfo;
	}
}

inline FITwinClippingInfluenceInfo const& FITwinClippingInfoBase::GetInfluenceInfo(EITwinModelType ModelType) const
{
	return const_cast<FITwinClippingInfoBase*>(this)->MutableInfluenceInfo(ModelType);
}

inline bool FITwinClippingInfoBase::DoesInfluenceModel(const ITwin::ModelLink& ModelIdentifier) const
{
	FITwinClippingInfluenceInfo const& InfluenceInfo = GetInfluenceInfo(ModelIdentifier.first);
	return InfluenceInfo.bInfluenceAll || InfluenceInfo.SpecificIDs.Contains(ModelIdentifier.second);
}
