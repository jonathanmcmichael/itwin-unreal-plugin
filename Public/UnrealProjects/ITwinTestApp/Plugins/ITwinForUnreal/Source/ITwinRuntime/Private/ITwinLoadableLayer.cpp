/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinLoadableLayer.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "ITwinLoadableLayer.h"

#include <ITwinDigitalTwinManager.h>

void UITwinLoadableLayer::Load()
{
	if (ensure(Owner))
	{
		Owner->LoadComponent(GetLayerId(), EITwinLoadContext::Single);
	}
}

void UITwinLoadableLayer::Remove()
{
	if (ensure(Owner))
	{
		Owner->RemoveComponent(GetLayerId());
	}
}
