/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingTool.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <Clipping/ITwinClippingEventHub.h>
#include <ITwinModelType.h>
#include <Containers/Array.h>
#include <Containers/Map.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
	#include <glm/ext/matrix_double3x3.hpp>
	#include <glm/ext/vector_double3.hpp>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <memory>
#include <optional>

#include "ITwinClippingTool.generated.h"


struct FITwinClippingInfoBase;
class FITwinTilesetAccess;
class UITwinClippingToolImpl;
class UITwinClippingPersistence;
class UITwinClippingRenderer;
class UCesiumPolygonRasterOverlay;

class AITwinInteractiveTool;
class AITwinPopulation;
class AITwinPopulationTool;
enum class EITwinInstantiatedObjectType : uint8;
enum class ETransformationMode : uint8;

namespace AdvViz::SDK
{
	class RefID;
}


/// Class managing Clipping Tools.
/// For this prototype, those tools are linked to the population tool, with dedicated objects.
UCLASS()
class ITWINRUNTIME_API AITwinClippingTool : public AITwinClippingEventHub
{
	GENERATED_BODY()
public:
	AITwinClippingTool();

	virtual void Tick(float DeltaTime) override;

	/// Connect the Population Tool (mandatory to manage box/plane effects).
	void ConnectPopulationTool(AITwinPopulationTool* PopulationTool);

	/// Connect the Spline Tool (mandatory to manage cutout polygon effects).
	void ConnectSplineTool(class AITwinSplineTool* SplineTool);

	/// Connect the scene persistence manager.
	void ConnectPersistenceManager(class AITwinDecorationHelper* DecorationHelper);

	/// Register tileset in the clipping system (for tile excluder mechanism).
	void RegisterTileset(const FITwinTilesetAccess& TilesetAccess);

	void OnModelRemoved(const ITwin::ModelLink& ModelIdentifier);

	/// Initiate the interactive creation of a new effect.
	bool StartInteractiveEffectCreation(EITwinClippingPrimitiveType Type);

	/// Abort current cutout effect creation, if any.
	void AbortInteractiveCreation(bool bTriggeredFromITS);

	/// Deactivate the cutout tool. This also aborts any cutout creation, if any.
	void Deactivate();

	/// The clipping cube and plane primitives are created/modified from the population tool, as it
	/// is already compatible with gizmo edition...
	void OnClippingInstanceAdded(AITwinPopulation* Population, EITwinInstantiatedObjectType ObjectType, int32 InstanceIndex);

	/// Update the clipping information in all tile excluders matching the modified instance, as well as in
	/// the material parameter collection.
	void OnClippingInstanceModified(EITwinInstantiatedObjectType ObjectType, int32 InstanceIndex, bool bTriggeredFromITS);

	/// Called before some clipping instances are actually removed.
	void BeforeRemoveClippingInstances(EITwinInstantiatedObjectType ObjectType, const TArray<int32>& InstanceIndices);

	/// Update the clipping information upon the removal of clipping primitives.
	void OnClippingInstancesRemoved(EITwinInstantiatedObjectType ObjectType, const TArray<int32>& InstanceIndices);

	/// Returns true if we are allowed to load legacy cutout instances, those retrieved from the Decoration
	/// Service.
	bool AllowLoadingLegacyInstances() const;

	/// Update the clipping information and/or rendering data upon the loading of clipping primitives.
	void OnClippingInstancesLoaded(AITwinPopulation* Population, bool bUpdateEffectInfos);

	/// Perform some automatic conversions when the loading is complete (such as migration to Scene API).
	void OnLoadComplete();

	/// Return the number of clipping effects for the given primitive type.
	int32 NumEffects(EITwinClippingPrimitiveType Type) const;

	/// Return a mutable reference to the clipping effect of given type and index, to modify it.
	FITwinClippingInfoBase& GetMutableEffect(EITwinClippingPrimitiveType Type, int32 Index);

	/// Return a const reference to the clipping effect of given type and index, to read its info.
	const FITwinClippingInfoBase& GetEffect(EITwinClippingPrimitiveType Type, int32 Index) const;

	/// Remove an individual clipping primitive. Returns true if the effect was actually removed.
	bool RemoveEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bTriggeredFromITS);

	/// Flip the effect of given type and index.
	void FlipEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex);

	/// Flip all effects of the given type.
	void FlipAllEffectsOfType(EITwinClippingPrimitiveType Type);

	bool GetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex) const;
	void SetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bInvert);

	/// Select the effect of given type and index.
	/// \param bEnterIsolationMode When true, we enter isolation mode, by hiding all the other proxies.
	/// \return True if the effect could be selected.
	bool SelectEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex,
		bool bEnterIsolationMode = true);

	/// Zoom in on the effect of given type and index.
	void ZoomOnEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex);

	/// Returns a pair identifying the selected effect, if any.
	using FEffectIdentifier = std::pair<EITwinClippingPrimitiveType, int32>;
	std::optional<FEffectIdentifier> GetSelectedEffect() const;
	/// Reset current selection to none.
	/// \param bExitIsolationMode When true, and if there was currently an isolation mode, we exit it by
	/// restoring the normal visibility of effect proxies.
	void DeSelectAll(bool bExitIsolationMode = true);

	virtual void BroadcastSelection() override;

	/// Return the index of the selected polygon point, if any (if a cutout polygon point is selected) and if
	/// yes, fills its coordinates (latitude and longitude).
	/// If no polygon is selected, or if none of its points is selected, INDEX_NONE is returned.
	int32 GetSelectedPolygonPointInfo(double& OutLatitude, double& OutLongitude) const;
	/// Modify the location of the selected cutout polygon point, if any.
	void SetPolygonPointLocation(int32 PolygonIndex, int32 PointIndex, double Latitude, double Longitude) const;

	bool GetEffectTransform(EITwinClippingPrimitiveType EffectType, int32 Index,
		FTransform& OutTransform, double& OutLatitude, double& OutLongitude, double& OutElevation) const;
	bool GetSelectedEffectTransform(FTransform& OutTransform, double& OutLatitude, double& OutLongitude, double& OutElevation) const;
	void SetEffectLocation(EITwinClippingPrimitiveType EffectType, int32 Index,
		double InLatitude, double InLongitude, double InElevation,
		bool bTriggeredFromITS) const;
	void SetEffectRotation(EITwinClippingPrimitiveType EffectType, int32 Index,
		double InRotX, double InRotY, double InRotZ,
		bool bTriggeredFromITS) const;

	/// Called when we activate/deactivate picking of clipping effects in the viewport.
	void OnActivatePicking(bool bActivate);
	/// Try to select a cut-out effect from a mouse click event.
	bool DoMouseClickPicking(bool& bOutSelectionGizmoNeeded);

	//! Change the view camera so that the cutout polygons can be edited from top.
	//! If SpecificSpline is provided, only the corresponding polygon will be framed.
	UFUNCTION(Category = "iTwinUX", BlueprintCallable)
	void OnOverviewCamera(AITwinSplineHelper const* SpecificSpline = nullptr);

	//! Set the transformation mode (for selection gizmo).
	void SetTransformationMode(ETransformationMode Mode);

	/// Return whether the given effect is enabled.
	bool IsEffectEnabled(EITwinClippingPrimitiveType EffectType, int32 Index) const;
	/// Switches the given effect on or off.
	void EnableEffect(EITwinClippingPrimitiveType EffectType, int32 Index, bool bInEnabled);
	/// Enable or disable all effects.
	void EnableAllEffects(bool bInEnabled);

	/// Return whether the given effect should influence the given model.
	bool ShouldEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		const ITwin::ModelLink& ModelIdentifier) const;

	/// Whether the influence of effects is defined by layer type (iModel, Reality data, etc.).
	bool IsUsingPerLayerTypeInfluence() const;
	/// Converts the influence of effects to be defined by layer instead of layer type (iModel, Reality data,
	/// etc.).
	/// \param InCurrentLayers The list of currently loaded layers, per model type, to initialize the new
	/// per-layer influence settings. This is required to avoid losing the current influence settings during
	/// the conversion.
	void ConvertToPerLayerInfluence(const TMap<EITwinModelType, TSet<FString>>& InCurrentLayers);

	/// Return whether the given effect should influence the given model type globally.
	bool ShouldEffectInfluenceFullModelType(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		EITwinModelType ModelType) const;
	void SetEffectInfluenceFullModelType(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		EITwinModelType ModelType, bool bAll);

	void SetEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		const ITwin::ModelLink& ModelIdentifier, bool bInfluence);

	bool DoesEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		const ITwin::ModelLink& ModelIdentifier) const;

	TSet<FString> GetInfluencedSpecificModels(EITwinClippingPrimitiveType EffectType,
		int32 EffectIndex,
		EITwinModelType LayerType) const;

	/// Returns the unique identifier of an effect from its index.
	AdvViz::SDK::RefID GetEffectId(EITwinClippingPrimitiveType EffectType, int32 EffectIndex) const;

	/// Returns the index of a given effect from its unique identifier.
	int32 GetEffectIndex(EITwinClippingPrimitiveType EffectType, AdvViz::SDK::RefID const& RefID) const;

	UFUNCTION()
	void OnSceneLoaded(bool bSuccess);

	UFUNCTION()
	void OnItemCreationAbortedInTool(const AITwinInteractiveTool* Tool, bool bTriggeredFromITS);

	UFUNCTION()
	void OnItemCreatedInTool(const AITwinInteractiveTool* Tool, bool bTriggeredFromITS);

	UFUNCTION()
	void OnCutoutPolygonSelected();

	/// Returns the renderer used to manage cutout effects in the scene.
	const UITwinClippingRenderer* GetRenderer() const;


#if WITH_EDITOR

	/// Globally activate/deactivate all effects of given type, at given level. This is for debugging, only
	/// possible in Editor.
	/// \param Type The cutout type which should be affected
	/// \param Level The effect level which should be affected (shader or tileset excluder)
	/// \param bActivate Whether the effect should be activated or not
	void ActivateEffects(EITwinClippingPrimitiveType Type, EITwinClippingEffectLevel Level, bool bActivate);

	/// Globally activate/deactivate all effects of given type, at all levels.
	void ActivateEffectsAllLevels(EITwinClippingPrimitiveType Type, bool bActivate);

#endif // WITH_EDITOR


private:
	UPROPERTY()
	TObjectPtr<UITwinClippingToolImpl> Impl;
};
