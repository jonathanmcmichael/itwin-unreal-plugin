/*--------------------------------------------------------------------------------------+
|
|     $Source: TimelineBaseJson.inl $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "TimelineBase.h"

#include <Dom/JsonObject.h>
#include <Dom/JsonValue.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <boost/fusion/include/for_each.hpp>
	#include <boost/fusion/include/zip.hpp>
#include <Compil/AfterNonUnrealIncludes.h>

#include <type_traits>

namespace ITwin::Timeline
{

namespace {
	static const std::vector<FString> HardcodedNames(
		{ TEXT("Visibility"), TEXT("Color"), TEXT("Transform"), TEXT("CuttingPlane") });
}

template<class _ObjectTimeline>
void MainTimelineBase<_ObjectTimeline>::SetJsonPrintingWithHumanReadableTimes(bool bHumanReadableTimes) const
{
	for (auto&& ObjectTimeline : Container)
		ObjectTimeline->SetJsonPrintingWithHumanReadableTimes(bHumanReadableTimes);
}

template<class _ObjectTimeline>
void MainTimelineBase<_ObjectTimeline>::SetJsonPrintingNumberOfDecimals(int NumberOfDecimals) const
{
	for (auto&& ObjectTimeline : Container)
		ObjectTimeline->SetJsonPrintingNumberOfDecimals(NumberOfDecimals);
}

template<class _Metadata>
void ObjectTimeline<_Metadata>::ToJson(TSharedRef<FJsonObject>& JsonObj) const
{
	FInternationalization& I18N = FInternationalization::Get();
	FDateRange const TimeRange = GetDateRange();
	// Those are indicative, always print as human-readable and round them to the nearest second
	if (TimeRange.HasLowerBound())
	{
		double RoundedToNearestSecond = std::round(ITwin::Time::FromDateTime(TimeRange.GetLowerBound().GetValue()));
		JsonObj->SetStringField(TEXT("startTime"),
			ITwin::Time::UTCDateTimeToString(ITwin::Time::ToDateTime(RoundedToNearestSecond)));
	}
	else
		JsonObj->SetStringField(TEXT("startTime"), TEXT("<wrong startTime?!>"));
	if (TimeRange.HasUpperBound())
	{
		double RoundedToNearestSecond = std::round(ITwin::Time::FromDateTime(TimeRange.GetUpperBound().GetValue()));
		JsonObj->SetStringField(TEXT("endTime"),
			ITwin::Time::UTCDateTimeToString(ITwin::Time::ToDateTime(RoundedToNearestSecond)));
	}
	else
		JsonObj->SetStringField(TEXT("endTime"), TEXT("<wrong endTime?!>"));
	int HardcodedIndex = -1;
	boost::fusion::for_each(*this, [this, &HardcodedIndex, &JsonObj](const auto& propertyTimeline)
		{
			++HardcodedIndex;
			if (propertyTimeline.HasNoEffect())
				return;
			TArray<TSharedPtr<FJsonValue>> Keys, Values;
			Keys.Reserve(static_cast<int32>(propertyTimeline.Values.size()));
			Values.Reserve(static_cast<int32>(propertyTimeline.Values.size()));
			bool bHasInterp = false;
			for (auto&& Keyframe : propertyTimeline.Values)
			{
				if (bHumanReadableTimes)
				{
					Keys.Add(MakeShared<FJsonValueString>(
						ITwin::Time::UTCDateTimeToString(ITwin::Time::ToDateTime(Keyframe.Time))));
				}
				else
				{
					Keys.Add(MakeShared<FJsonValueString>(
						FString::Printf(TEXT("0x%I64x"), ITwin::Time::ToDateTime(Keyframe.Time).GetTicks())));
				}
				auto&& Val = ToJsonValue(Keyframe, NumberOfDecimals);
				Values.Add(std::move(Val));
				bHasInterp |= (EInterpolation::Step != Keyframe.Interpolation);
			}
			//using TimelineType =
			//	std::remove_reference_t<std::remove_const_t<decltype(propertyTimeline)>>;
			JsonObj->SetArrayField(
				// Can't make that work because of unhelpful compile errors... maybe with std::decay_t above?
				//ITwin::Timeline::_iTwinTimelineGetPropertyName<TimelineType::PropertyValues>()
				HardcodedNames[HardcodedIndex] + (bHumanReadableTimes ? TEXT("Times") : TEXT("Ticks")),
				Keys);
			JsonObj->SetArrayField(
				HardcodedNames[HardcodedIndex] + TEXT("Values"),
				Values);
			if (bHasInterp)
			{
				TArray<TSharedPtr<FJsonValue>> Interps;
				Interps.Reserve(Keys.Num());
				for (auto&& Keyframe : propertyTimeline.Values)
				{
					Interps.Add(MakeShared<FJsonValueNumber>((int)Keyframe.Interpolation));
				}
				JsonObj->SetArrayField(
					HardcodedNames[HardcodedIndex] + TEXT("Interps"),
					Interps);
			}
		});
}

template<class _Metadata>
bool ObjectTimeline<_Metadata>::FromJson(TSharedPtr<FJsonObject> const& JsonObj)
{
	int HardcodedIndex = -1;
	bool bError = false;
	boost::fusion::for_each(*this, [&HardcodedIndex, &JsonObj, &bError](auto& propertyTimeline)
		{
			if (bError)
				return;
			++HardcodedIndex;
			TArray<TSharedPtr<FJsonValue>> const* PropTimes;
			bool bFromJsonHumanReadableTimes = true;
			bool bHasTimes = JsonObj->TryGetArrayField(HardcodedNames[HardcodedIndex] + TEXT("Times"), PropTimes);
			if (!bHasTimes)
			{
				bFromJsonHumanReadableTimes = false;
				bHasTimes = JsonObj->TryGetArrayField(HardcodedNames[HardcodedIndex] + TEXT("Ticks"), PropTimes);
			}
			TArray<TSharedPtr<FJsonValue>> const* PropValues;
			bool bHasValues = JsonObj->TryGetArrayField(HardcodedNames[HardcodedIndex] + TEXT("Values"), PropValues);
			if (bHasTimes != bHasValues || (bHasTimes && bHasValues && PropTimes->Num() != PropValues->Num()))
			{
				bError = true;
				return;
			}
			if (!bHasTimes) // not an error, simply no keyframe for this property
				return;
			TArray<TSharedPtr<FJsonValue>> const* PropInterps = nullptr;
			if (JsonObj->TryGetArrayField(HardcodedNames[HardcodedIndex] + TEXT("Interps"), PropInterps)
				&& PropInterps->Num() != PropTimes->Num())
			{
				bError = true;
				return;
			}
			TArray<double> Times;
			Times.Reserve(PropTimes->Num());
			if (bFromJsonHumanReadableTimes)
			{
				for (auto const& TimeEntry : (*PropTimes))
				{
					FDateTime Time;
					if (!ITwin::Time::FromUTCDateTimeString(TimeEntry->AsString(), Time))
					{
						bError = true;
						return;
					}
					Times.Add(ITwin::Time::FromDateTime(Time));
				}
			}
			else
			{
				for (auto const& TimeEntry : (*PropTimes))
				{
					errno = 0;
					uint64 const Ticks = FCString::Strtoui64(*TimeEntry->AsString(), nullptr, /*base*/16);
					if (errno != 0)
					{
						bError = true;
						return;
					}
					Times.Add(ITwin::Time::FromDateTime(FDateTime(Ticks)));
				}
			}
			if (PropTimes->Num() == PropValues->Num())
			{
				int32 i = 0;
				for (auto const& Time : Times)
				{
					EInterpolation Interp = EInterpolation::Step;
					if (PropInterps)
					{

						int32 InterpolationInt;
						if (!(*PropInterps)[i]->TryGetNumber(InterpolationInt))
						{
							bError = true;
							return;
						}
						Interp = static_cast<EInterpolation>(InterpolationInt);
					}
					using PropSetType = decltype(propertyTimeline.Values);
					using PropType = typename PropSetType::value_type;
					PropType Prop;
					if (!FromJsonValue(Time, Interp, (*PropValues)[i], Prop))
					{
						bError = true;
						return;
					}
					propertyTimeline.Values.insert(std::move(Prop));
					++i;
				}
			}
		});
	return !bError;
}

} // namespace ITwin::Timeline
