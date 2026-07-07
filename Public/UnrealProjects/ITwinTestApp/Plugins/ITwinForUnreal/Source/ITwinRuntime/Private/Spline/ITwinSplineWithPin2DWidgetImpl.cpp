/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineWithPin2DWidgetImpl.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Spline/ITwinSplineWithPin2DWidgetImpl.h>
#include <Spline/ITwinSpline2DWidget.h>

#include <Components/Border.h>
#include <Components/Button.h>
#include <Components/CanvasPanel.h>
#include "Components/CanvasPanelSlot.h"
#include <Components/Image.h>


void UITwinSplineWithPin2DWidgetImpl::NativeConstruct()
{
	Super::NativeConstruct();

	if (ensure(Button && EndButton) && !Button->OnPressed.IsBound())
	{
		Button->OnPressed.AddDynamic(this, &UITwinSplineWithPin2DWidgetImpl::OnStartPointButtonPressed);
		EndButton->OnPressed.AddDynamic(this, &UITwinSplineWithPin2DWidgetImpl::OnEndPointButtonPressed);
	}
}

void UITwinSplineWithPin2DWidgetImpl::SetSplineChunkIndex(int32 InIndex)
{
	SplineChunkIndex = InIndex;
}

void UITwinSplineWithPin2DWidgetImpl::SetStartAndEnd(const FITwinSplineChunk2DInfo& InChunk2DInfo)
{
	if (ensure(Spline2DWidget))
	{
		Spline2DWidget->SetStartAndEnd(InChunk2DInfo);
	}
	if (InChunk2DInfo.bValidStart && ensure(Pin))
	{
		auto PinSlot = Cast<UCanvasPanelSlot>(Pin->Slot);
		PinSlot->SetPosition(InChunk2DInfo.Start);
	}
	SetShowStartPin(InChunk2DInfo.bValidStart);

	if (InChunk2DInfo.bValidEnd && ensure(EndPin))
	{
		auto EndPinSlot = Cast<UCanvasPanelSlot>(EndPin->Slot);
		EndPinSlot->SetPosition(InChunk2DInfo.End);
	}
	if (!InChunk2DInfo.bValidEnd)
	{
		SetShowEndPin(false);
	}
}

void UITwinSplineWithPin2DWidgetImpl::SetUIColors(const FUIColors& InColors)
{
	if (ensure(Spline2DWidget))
	{
		Spline2DWidget->SetTint(InColors.LineColor);
	}
	if (ensure(Pin))
	{
		Pin->SetBrushColor(InColors.LineColor);
	}
	if (ensure(EndPin))
	{
		EndPin->SetBrushColor(InColors.LineColor);
	}

	SetPointInteriorColor(InColors.PointInteriorColor);

	// Also adjust the hover/pressed color of the buttons for better contrast.
	auto const SetHoverPressedColor = [&](UButton* Button)
	{
		if (!ensure(Button))
			return;

		FButtonStyle ButtonStyle = Button->GetStyle();
		ButtonStyle.Hovered.TintColor = InColors.ButtonHoverColor;
		ButtonStyle.Pressed.TintColor = InColors.ButtonPressedColor;
		Button->SetStyle(ButtonStyle);
	};
	SetHoverPressedColor(Button);
	SetHoverPressedColor(EndButton);
}


void UITwinSplineWithPin2DWidgetImpl::SetPointInteriorColor(const FLinearColor& InColor)
{
	if (ensure(Image))
	{
		Image->SetColorAndOpacity(InColor);
	}
	if (ensure(EndImage))
	{
		EndImage->SetColorAndOpacity(InColor);
	}
}

FLinearColor UITwinSplineWithPin2DWidgetImpl::GetPointInteriorColor() const
{
	if (ensure(Image))
	{
		return Image->GetColorAndOpacity();
	}
	else
	{
		return FLinearColor::Black;
	}
}

void UITwinSplineWithPin2DWidgetImpl::SetSplineThickness(float InThickness)
{
	if (ensure(Spline2DWidget))
	{
		Spline2DWidget->SetThickness(InThickness);
	}
}

float UITwinSplineWithPin2DWidgetImpl::GetSplineThickness() const
{
	if (ensure(Spline2DWidget))
	{
		return Spline2DWidget->GetThickness();
	}
	else
	{
		return 1.0f;
	}
}

void UITwinSplineWithPin2DWidgetImpl::SetShowPins(bool bInShowPins)
{
	if (bShowPins == bInShowPins)
		return;
	bShowPins = bInShowPins;
	UpdateComponentsVisibility();
}

void UITwinSplineWithPin2DWidgetImpl::SetShowStartPin(bool bInShowStartPin)
{
	bShowStartPin = bInShowStartPin;
	UpdateComponentsVisibility();
}

void UITwinSplineWithPin2DWidgetImpl::SetShowEndPin(bool bInShowEndPin)
{
	bShowEndPin = bInShowEndPin;
	UpdateComponentsVisibility();
}


void UITwinSplineWithPin2DWidgetImpl::UpdateComponentsVisibility()
{
	if (Pin)
	{
		Pin->SetVisibility((bShowPins && bShowStartPin) ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (EndPin)
	{
		EndPin->SetVisibility((bShowPins && bShowEndPin) ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UITwinSplineWithPin2DWidgetImpl::EnableStartEndButtonInteractions(bool bEnableStartButton, bool bEnableEndButton)
{
	if (ensure(Button && EndButton))
	{
		Button->SetIsEnabled(bEnableStartButton);
		EndButton->SetIsEnabled(bEnableEndButton);
	}
}

void UITwinSplineWithPin2DWidgetImpl::EnableButtonInteractions(bool bEnable)
{
	EnableStartEndButtonInteractions(bEnable, bEnable);
}

void UITwinSplineWithPin2DWidgetImpl::OnStartPointButtonPressed()
{
	if (ensure(bShowPins && bShowStartPin && SplineChunkIndex != INDEX_NONE))
	{
		OnSplinePointPickedEvent.Broadcast(SplineChunkIndex);
	}
}

void UITwinSplineWithPin2DWidgetImpl::OnEndPointButtonPressed()
{
	if (ensure(bShowPins && bShowEndPin && SplineChunkIndex != INDEX_NONE))
	{
		OnSplinePointPickedEvent.Broadcast(SplineChunkIndex + 1);
	}
}

UITwinSpline2DWidget* UITwinSplineWithPin2DWidgetImpl::GetSpline2DWidget() const
{
	return Spline2DWidget;
}
