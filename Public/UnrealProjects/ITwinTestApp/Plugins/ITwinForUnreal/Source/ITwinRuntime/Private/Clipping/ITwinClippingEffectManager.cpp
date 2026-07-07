/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingEffectManager.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Clipping/ITwinClippingEffectManager.h>
#include <Clipping/ITwinClippingEffectManager.inl>

#include <Population/ITwinPopulation.h>
#include <Population/ITwinPopulationTool.h>
#include <Spline/ITwinSplineHelper.h>
#include <Spline/ITwinSplineTool.h>

UITwinClippingEffectManager::UITwinClippingEffectManager()
{

}

void UITwinClippingEffectManager::SetPopulationTool(AITwinPopulationTool* InPopulationTool)
{
	PopulationTool = InPopulationTool;
}

TWeakObjectPtr<AITwinPopulationTool> UITwinClippingEffectManager::ActivatePopulationTool(
	bool /*bUpdateTransformationMode*/ /*= true*/)
{
	if (ensure(PopulationTool.IsValid()))
	{
		if (!PopulationTool->IsEnabled())
		{
			AITwinInteractiveTool::DisableAll(GetWorld());
			PopulationTool->SetEnabled(true);
		}
		PopulationTool->SetUsedOnCutout(true);
		PopulationTool->ResetToDefault();
	}
	return PopulationTool;
}

void UITwinClippingEffectManager::SetSplineTool(AITwinSplineTool* InSplineTool)
{
	SplineTool = InSplineTool;
}

TWeakObjectPtr<AITwinSplineTool> UITwinClippingEffectManager::ActivateSplineTool()
{
	ensure(SplineTool.IsValid());
	return ITwin::ActivateSplineTool(GetWorld(), EITwinSplineUsage::MapCutout, SplineTool);
}

AdvViz::SDK::RefID UITwinClippingEffectManager::GetEffectId(EITwinClippingPrimitiveType EffectType, int32 EffectIndex) const
{
	switch (EffectType)
	{
	case EITwinClippingPrimitiveType::Box:
	case EITwinClippingPrimitiveType::Plane:
	{
		auto const& Population = GetPopulation(EffectType);
		if (Population.IsValid())
		{
			return Population->GetInstanceRefId(EffectIndex);
		}
		break;
	}

	case EITwinClippingPrimitiveType::Polygon:
		if (EffectIndex >= 0 && EffectIndex < ClippingPolygonInfos.Num())
		{
			auto const& PolygonInfo = ClippingPolygonInfos[EffectIndex];
			return PolygonInfo.GetAVizSplineId();
		}
		break;

	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinClippingPrimitiveType::Count:);
	}
	return AdvViz::SDK::RefID::Invalid();
}

int32 UITwinClippingEffectManager::GetEffectIndex(EITwinClippingPrimitiveType EffectType, AdvViz::SDK::RefID const& RefID) const
{
	switch (EffectType)
	{
	case EITwinClippingPrimitiveType::Box:
	case EITwinClippingPrimitiveType::Plane:
	{
		auto const& Population = GetPopulation(EffectType);
		if (Population.IsValid())
		{
			return Population->GetInstanceIndexFromRefId(RefID);
		}
		break;
	}
	case EITwinClippingPrimitiveType::Polygon:
		return ClippingPolygonInfos.IndexOfByPredicate(
			[&RefID](FITwinClippingCartographicPolygonInfo const& InItem)
		{
			return InItem.GetAVizSplineId() == RefID;
		});

	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinClippingPrimitiveType::Count:);
	}
	return INDEX_NONE;
}

int32 UITwinClippingEffectManager::GetCutoutPolygonIndex(AITwinSplineHelper const* Spline) const
{
	return ClippingPolygonInfos.IndexOfByPredicate(
		[&Spline](FITwinClippingCartographicPolygonInfo const& InItem)
	{
		return InItem.GetSpline().Get() == Spline;
	});
}

void UITwinClippingEffectManager::RegisterCutoutPopulation(EITwinClippingPrimitiveType EffectType, AITwinPopulation* Population)
{
	switch (EffectType)
	{
	case EITwinClippingPrimitiveType::Box:
		BoxPopulation = Population;
		break;
	case EITwinClippingPrimitiveType::Plane:
		PlanePopulation = Population;
		break;
	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(
	case EITwinClippingPrimitiveType::Polygon:
	case EITwinClippingPrimitiveType::Count:);
	}
}

void UITwinClippingEffectManager::OnClippingInstancesLoaded(AITwinPopulation* Population, bool bUpdateEffectInfos)
{
	BE_ISSUE("not implemented in UITwinClippingEffectManager");
}


AITwinSplineHelper* UITwinClippingEffectManager::GetCutoutSpline(int32 Index) const
{
	if (!ClippingPolygonInfos.IsValidIndex(Index))
	{
		BE_ISSUE("Invalid polygon index");
		return nullptr;
	}
	auto const& PolygonInfo = ClippingPolygonInfos[Index];
	if (!PolygonInfo.GetSpline().IsValid())
	{
		BE_ISSUE("Failed to get spline");
		return nullptr;
	}
	return PolygonInfo.GetSpline().Get();
}

bool UITwinClippingEffectManager::ShouldEffectInfluenceModel(EITwinClippingPrimitiveType EffectType,
	int32 EffectIndex,
	const ITwin::ModelLink& ModelIdentifier) const
{
	if (ensure(EffectIndex < NumEffects(EffectType)))
	{
		return GetEffect(EffectType, EffectIndex).ShouldInfluenceModel(ModelIdentifier);
	}
	return false;
}
