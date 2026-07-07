/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingEffectManager.inl $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Clipping/ITwinClippingEffectManager.h>
#include <Population/ITwinPopulation.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#include <Compil/AfterNonUnrealIncludes.h>

inline
int32 UITwinClippingEffectManager::NumEffects(EITwinClippingPrimitiveType Type) const
{
	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:		return ClippingBoxInfos.Num();
	case EITwinClippingPrimitiveType::Plane:	return ClippingPlaneInfos.Num();
	case EITwinClippingPrimitiveType::Polygon:	return ClippingPolygonInfos.Num();

	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(case EITwinClippingPrimitiveType::Count:, 0);
	}
}

inline
FITwinClippingInfoBase& UITwinClippingEffectManager::GetMutableEffect(EITwinClippingPrimitiveType Type, int32 Index)
{
	BE_ASSERT(Index >= 0 && Index < NumEffects(Type));
	switch (Type)
	{
	BE_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH(case EITwinClippingPrimitiveType::Count:)
	case EITwinClippingPrimitiveType::Box:		return ClippingBoxInfos[Index];
	case EITwinClippingPrimitiveType::Plane:	return ClippingPlaneInfos[Index];
	case EITwinClippingPrimitiveType::Polygon:	return ClippingPolygonInfos[Index];
	}
}

inline
const FITwinClippingInfoBase& UITwinClippingEffectManager::GetEffect(EITwinClippingPrimitiveType Type, int32 Index) const
{
	BE_ASSERT(Index >= 0 && Index < NumEffects(Type));
	switch (Type)
	{
	BE_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH(case EITwinClippingPrimitiveType::Count:)
	case EITwinClippingPrimitiveType::Box:		return ClippingBoxInfos[Index];
	case EITwinClippingPrimitiveType::Plane:	return ClippingPlaneInfos[Index];
	case EITwinClippingPrimitiveType::Polygon:	return ClippingPolygonInfos[Index];
	}
}

inline
bool UITwinClippingEffectManager::IsValidEffectIndex(EITwinClippingPrimitiveType EffectType, int32 Index) const
{
	return Index >= 0 && Index < NumEffects(EffectType);
}

inline
const FITwinClippingBoxInfo& UITwinClippingEffectManager::GetBoxEffect(int32 Index) const
{
	BE_ASSERT(ClippingBoxInfos.IsValidIndex(Index));
	return ClippingBoxInfos[Index];
}

inline
const FITwinClippingPlaneInfo& UITwinClippingEffectManager::GetPlaneEffect(int32 Index) const
{
	BE_ASSERT(ClippingPlaneInfos.IsValidIndex(Index));
	return ClippingPlaneInfos[Index];
}
inline
const FITwinClippingCartographicPolygonInfo& UITwinClippingEffectManager::GetPolygonEffect(int32 Index) const
{
	BE_ASSERT(ClippingPolygonInfos.IsValidIndex(Index));
	return ClippingPolygonInfos[Index];
}


inline
TWeakObjectPtr<AITwinPopulation> UITwinClippingEffectManager::GetPopulation(EITwinClippingPrimitiveType Type) const
{
	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
		return BoxPopulation;
	case EITwinClippingPrimitiveType::Plane:
		return PlanePopulation;
		BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(
	case EITwinClippingPrimitiveType::Polygon:
	case EITwinClippingPrimitiveType::Count:,
		nullptr);
	}
}

inline
bool UITwinClippingEffectManager::IsValidPopulationIndex(EITwinClippingPrimitiveType EffectType, int32 Index) const
{
	TWeakObjectPtr<AITwinPopulation> Population = GetPopulation(EffectType);
	if (!ensure(Population.IsValid()))
	{
		return false;
	}
	return Index >= 0 && Index < Population->GetNumberOfInstances();
}


template <typename Func>
void UITwinClippingEffectManager::VisitClippingPrimitivesOfType(EITwinClippingPrimitiveType Type, Func const& Fun)
{
	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
		for (auto& BoxInfo : ClippingBoxInfos)
		{
			Fun(BoxInfo);
		}
		break;

	case EITwinClippingPrimitiveType::Plane:
		for (auto& PlaneInfo : ClippingPlaneInfos)
		{
			Fun(PlaneInfo);
		}
		break;

	case EITwinClippingPrimitiveType::Polygon:
		for (auto& PolygonInfo : ClippingPolygonInfos)
		{
			Fun(PolygonInfo);
		}
		break;

	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinClippingPrimitiveType::Count:);
	}
}
