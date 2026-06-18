/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineHelper2DWidgetImpl.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include <Blueprint/UserWidget.h>
#include <Templates/PimplPtr.h>

#include "ITwinSplineHelper2DWidgetImpl.generated.h"

class AITwinSplineHelper;
class UCanvasPanel;
class UITwinSplineWithPin2DWidgetImpl;
struct FITwinSplineChunk2DInfo;
class USplineComponent;

/// This widget is used to display a 3D spline in 2D, on screen,  as a child of the main viewport widget.
/// It is linked to an AITwinSplineHelper, from which it fetches the data to display.
UCLASS()
class ITWINRUNTIME_API UITwinSplineHelper2DWidgetImpl : public UUserWidget
{
    GENERATED_BODY()
public:
	UITwinSplineHelper2DWidgetImpl(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	void SetTint(const FLinearColor& InTint);

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	const FLinearColor& GetTint() const { return Tint; }

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	void SetThickness(float InThickness);

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	float GetThickness() const { return Thickness; }

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	void SetSplineHelper(AITwinSplineHelper* InSplineHelper);

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	AITwinSplineHelper* GetSplineHelper() const { return SplineHelper.Get(); }

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	void SetShowPins(bool bInShowPins);

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	bool GetShowPins() const { return bShowPins; }

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	void OnSplinePointPicked(int32 PickedPointIndex);

	bool SampleSplineChunkWidget(const UITwinSplineWithPin2DWidgetImpl& ChunkWidget,
		int32 NumSubdivisions) const;

	bool SampleSplineChunk(TArray<FVector2D>& OutSampledPositions,
		int32 ChunkIndex,
		int32 NumSubdivisions) const;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FCursorReply NativeOnCursorQuery(const FGeometry& InGeometry, const FPointerEvent& InCursorEvent) override;

private:
	void UpdateComponentsVisibility();
	void UpdateSplineWidgets();
	void EnsureSplineChunkWidgetCount(int32 DesiredCount);
	void ClearSplineChunkWidgets();

	struct FScreenSpaceProjector;
	struct FScreenSpaceTangentComputer;
	bool BuildScreenSpaceSplineChunk(
		const USplineComponent& SplineComponent,
		const int32 StartIndex, const int32 EndIndex,
		const FScreenSpaceProjector& Projector,
		FITwinSplineChunk2DInfo& OutChunk2DInfo,
		const bool bLinearTangents) const;

	inline bool IsPointInsertionAllowed() const;

	const UITwinSplineWithPin2DWidgetImpl* FindSplineChunkUnderMouse(
		const FPointerEvent& InMouseEvent,
		FVector2D& OutClosestPoint2D,
		FVector::FReal ExtraTolerance = 0.) const;

	FVector GetClosestPointOnSplineMatching2D(const USplineComponent& SplineComponent,
		const FVector2D& SplinePoint2D, int32 ChunkIndex) const;


private:
	UPROPERTY(Category = "iTwin Spline",
		EditAnywhere,
		BlueprintSetter = SetTint)
	FLinearColor Tint = FLinearColor::White;

	UPROPERTY(Category = "iTwin Spline",
		EditAnywhere,
		BlueprintSetter = SetThickness)
	float Thickness = 2.0f;

	UPROPERTY(meta = (BindWidgetOptional))
	UCanvasPanel* RootCanvas = nullptr;

	UPROPERTY(EditAnywhere, Category = "iTwin Spline")
	TSubclassOf<UITwinSplineWithPin2DWidgetImpl> SplineChunkClass;

	UPROPERTY(Category = "iTwin Spline",
		EditAnywhere,
		BlueprintSetter = SetShowPins)
	bool bShowPins = true;

	// The source spline helper actor (3D spline) from which this widget fetches the data to display.
	TWeakObjectPtr<AITwinSplineHelper> SplineHelper;

	TArray<TObjectPtr<UITwinSplineWithPin2DWidgetImpl>> SplineChunkWidgets;

	struct FImpl;
	TPimplPtr<FImpl> Impl;
};
