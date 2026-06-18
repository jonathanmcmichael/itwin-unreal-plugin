/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineWithPin2DWidgetImpl.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Misc/Attribute.h"

#include "ITwinSplineWithPin2DWidgetImpl.generated.h"

class UBorder;
class UButton;
class UImage;
class UITwinSpline2DWidget;
struct FITwinSplineChunk2DInfo;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSplinePointPickedEvent, int32, PickedPointIndex);


/// This widget is used to display a spline with a pin at its start, and optionally at its end as well.
/// It is used as a chunk in the UITwinSplineHelper2DWidgetImpl, which can display one or several of these
/// widgets to represent a whole spline.
UCLASS()
class ITWINRUNTIME_API UITwinSplineWithPin2DWidgetImpl : public UUserWidget
{
    GENERATED_BODY()
public:

	//-----------------------------------------------------------------------------------
	// Spline properties
	//-----------------------------------------------------------------------------------

	/// Set the index of the chunk represented by this widget.
	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	void SetSplineChunkIndex(int32 InIndex);

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	int32 GetSplineChunkIndex() const { return SplineChunkIndex; }

	/// Set the 2D start and end positions and tangents of the spline represented by this widget, in screen
	/// space.
	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	void SetStartAndEnd(const FITwinSplineChunk2DInfo& InChunk2DInfo);


	//-----------------------------------------------------------------------------------
	// UI properties
	//-----------------------------------------------------------------------------------
	struct FUIColors
	{
		FLinearColor LineColor = FLinearColor::White;
		FLinearColor PointInteriorColor = FLinearColor::Black;
		FLinearColor ButtonHoverColor = FLinearColor::Gray;
		FLinearColor ButtonPressedColor = FLinearColor::Green;
	};
	void SetUIColors(const FUIColors& InColors);

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	void SetPointInteriorColor(const FLinearColor& InColor);

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	FLinearColor GetPointInteriorColor() const;


	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	void SetSplineThickness(float InThickness);

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	float GetSplineThickness() const;


	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	void SetShowPins(bool bInShowPins);

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	void SetShowStartPin(bool bInShowStartPin);

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	void SetShowEndPin(bool bInShowEndPin);


	//-----------------------------------------------------------------------------------
	// Mouse interactions
	//-----------------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable)
	FOnSplinePointPickedEvent OnSplinePointPickedEvent;

	/// Enable or disable the possibility to interact with the buttons at extremities of the spline segment,
	/// (which are used to pick the corresponding spline points).
	/// The Start and End buttons can be activated independently.
	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	void EnableStartEndButtonInteractions(bool bEnableStartButton, bool bEnableEndButton);

	/// Enable or disable the possibility to interact with the buttons at the end of the spline chunk, which
	/// are used to pick the corresponding spline points.
	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	void EnableButtonInteractions(bool bEnable);

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	void OnStartPointButtonPressed();

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	void OnEndPointButtonPressed();

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	UITwinSpline2DWidget* GetSpline2DWidget() const;


protected:
	virtual void NativeConstruct() override;


private:
	void UpdateComponentsVisibility();


	UPROPERTY(Category = "iTwin Spline",
		EditAnywhere,
		BlueprintSetter = SetSplineChunkIndex)
	int32 SplineChunkIndex = INDEX_NONE;

	UPROPERTY(meta = (BindWidget))
	UITwinSpline2DWidget* Spline2DWidget = nullptr;

	UPROPERTY(Category = "iTwin Spline",
		EditAnywhere,
		BlueprintSetter = SetShowPins)
	bool bShowPins = true;

	UPROPERTY(Category = "iTwin Spline",
		EditAnywhere,
		BlueprintSetter = SetShowStartPin)
	bool bShowStartPin = true;

	UPROPERTY(Category = "iTwin Spline",
		EditAnywhere,
		BlueprintSetter = SetShowEndPin)
	bool bShowEndPin = false;

	UPROPERTY(Meta = (BindWidget))
	UBorder* Pin = nullptr;
	UPROPERTY(meta = (BindWidget))
	UButton* Button = nullptr;
	UPROPERTY(meta = (BindWidget))
	UImage* Image = nullptr;

	/// When a spline chunk is the last one of the spline, and the spline is not closed, we may want to show
	/// a pin at its end as well.
	UPROPERTY(Meta = (BindWidget))
	UBorder* EndPin = nullptr;
	UPROPERTY(meta = (BindWidget))
	UButton* EndButton = nullptr;
	UPROPERTY(meta = (BindWidget))
	UImage* EndImage = nullptr;
};
