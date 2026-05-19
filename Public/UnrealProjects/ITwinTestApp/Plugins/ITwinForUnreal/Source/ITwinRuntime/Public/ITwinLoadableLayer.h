/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinLoadableLayer.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <ITwinModelType.h>
#include <ITwinWebServices/ITwinWebServices_Info.h>

#include <ITwinLoadableLayer.generated.h>

class AITwinDigitalTwinManager;


UENUM()
enum class EITwinLayerLoadStatus : uint8
{
	NotStarted,
	InProgress,
	Complete,
	Failed
};


/// Base class to wrap loadable iTwin layer information, and a way to load it in the scene.
UCLASS()
class ITWINRUNTIME_API UITwinLoadableLayer : public UObject
{
	GENERATED_BODY()
public:
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void Load();

	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void Remove();

	virtual EITwinModelType GetModelType() const PURE_VIRTUAL(UITwinLoadableLayer::GetModelType, return EITwinModelType::Invalid;);
	virtual FString GetLayerId() const PURE_VIRTUAL(UITwinLoadableLayer::GetLayerId, return TEXT(""););

private:
	TObjectPtr<AITwinDigitalTwinManager> Owner = nullptr;

	friend class AITwinDigitalTwinManager;
};


/// Wraps a loadable iModel, and a way to load it in the scene.
UCLASS()
class ITWINRUNTIME_API UITwinLoadableIModel : public UITwinLoadableLayer
{
	GENERATED_BODY()
public:
	FIModelInfo const& GetInfo() const { return Info; }

	virtual EITwinModelType GetModelType() const override { return EITwinModelType::IModel; }
	virtual FString GetLayerId() const override { return Info.Id; }

private:
	UPROPERTY(Category = "iTwin",
		VisibleAnywhere)
	FIModelInfo Info;

	friend class AITwinDigitalTwinManager;
};


/// Wraps a loadable Reality Data, and a way to load it in the scene.
UCLASS()
class ITWINRUNTIME_API UITwinLoadableRealityData : public UITwinLoadableLayer
{
	GENERATED_BODY()
public:
	FITwinRealityData3DInfo const& GetInfo() const { return Info; }

	virtual EITwinModelType GetModelType() const override { return EITwinModelType::RealityData; }
	virtual FString GetLayerId() const override { return Info.Id; }

private:
	UPROPERTY(Category = "iTwin",
		VisibleAnywhere)
	FITwinRealityData3DInfo Info;

	friend class AITwinDigitalTwinManager;
};


/// Helper allowing to control the loading of a layer from a digital twin manager,
/// with the possibility to do it from the Editor.
USTRUCT()
struct FITwinLoadableLayerHelper
{
	GENERATED_BODY()

	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	bool bLoad = false;

	UPROPERTY(Category = "iTwin",
		VisibleAnywhere,
		Meta = (InvalidateWidgets))
	EITwinLayerLoadStatus LoadStatus = EITwinLayerLoadStatus::NotStarted;

	UPROPERTY(Category = "iTwin",
		VisibleAnywhere)
	TObjectPtr<UITwinLoadableLayer> LoadableLayer;
};
