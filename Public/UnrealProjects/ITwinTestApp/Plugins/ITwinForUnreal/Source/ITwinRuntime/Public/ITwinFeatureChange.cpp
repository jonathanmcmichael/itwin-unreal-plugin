/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinFeatureChange.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinFeatureChange.h>
#include <Containers/UnrealString.h>

void FFeatureEventProperties::AddProperty(const FString& key, const FString& value)
{
	Properties.Emplace(FFeatureEventProperty(key, value));
}