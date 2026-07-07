/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingRenderer.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Clipping/ITwinClippingRenderer.h>

#include <CesiumGlobeAnchorComponent.h>
#include <CesiumPolygonRasterOverlay.h>
#include <Clipping/ITwinBoxTileExcluder.h>
#include <Clipping/ITwinClipping3DTilesetHelper.h>
#include <Clipping/ITwinClippingEffectManager.h>
#include <Clipping/ITwinClippingEffectManager.inl>
#include <Clipping/ITwinClippingMPCHolder.h>
#include <Clipping/ITwinPlaneTileExcluder.h>

#include <ITwinGoogle3DTileset.h>
#include <ITwinIModel.h>
#include <ITwinRealityData.h>
#include <ITwinTilesetAccess.h>
#include <Spline/ITwinSplineHelper.h>


#include <Materials/MaterialParameterCollection.h>
#include <Materials/MaterialParameterCollectionInstance.h>

#include <set>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#	include <Core/Tools/Log.h>
#	include <glm/matrix.hpp>
#	include <glm/gtc/matrix_access.hpp>
#	include <glm/gtx/compatibility.hpp>
#include <Compil/AfterNonUnrealIncludes.h>


UITwinClippingRenderer::UITwinClippingRenderer()
	: Super()
{
	this->ClippingMPCHolder = CreateDefaultSubobject<UITwinClippingMPCHolder>(TEXT("MPC_Holder"));
}

void UITwinClippingRenderer::SetEffectManager(UITwinClippingEffectManager* InManager)
{
	EffectManager = InManager;
}

UMaterialParameterCollection* UITwinClippingRenderer::GetMPCClipping()
{
	return ClippingMPCHolder->GetMPCClipping();
}

UMaterialParameterCollectionInstance* UITwinClippingRenderer::GetMPCClippingInstance()
{
	UWorld* World = GetWorld();
	if (ensure(World))
	{
		return World->GetParameterCollectionInstance(GetMPCClipping());
	}
	return nullptr;
}

namespace
{
	template <class TTilesetOwner>
	inline UITwinClipping3DTilesetHelper* TGetClippingHelper(AActor const* TilesetOwnerActor)
	{
		if (TilesetOwnerActor && ensure(TilesetOwnerActor->IsA(TTilesetOwner::StaticClass())))
		{
			return Cast<TTilesetOwner const>(TilesetOwnerActor)->GetClippingHelper();
		}
		return nullptr;
	}

	template <class TTilesetOwner>
	inline UITwinClipping3DTilesetHelper* TMakeClippingHelper(AActor* TilesetOwnerActor)
	{
		if (TilesetOwnerActor && ensure(TilesetOwnerActor->IsA(TTilesetOwner::StaticClass())))
		{
			TTilesetOwner* TilesetOwner = Cast<TTilesetOwner>(TilesetOwnerActor);
			TilesetOwner->MakeClippingHelper();
			return TilesetOwner->GetClippingHelper();
		}
		return nullptr;
	}


	UITwinClipping3DTilesetHelper* GetClippingHelper(ACesium3DTileset const& Tileset, ITwin::ModelLink const& ModelIdentifier)
	{
		// TODO_JDE: Possible coding improvement: share more code between AITwinIModel, AITwinRealityData and
		// AITwinGoogle3DTileset to avoid this kind of duplication...

		switch (ModelIdentifier.first)
		{
		case EITwinModelType::GlobalMapLayer:
			return TGetClippingHelper<AITwinGoogle3DTileset>(&Tileset);
		case EITwinModelType::IModel:
			return TGetClippingHelper<AITwinIModel>(Tileset.GetOwner());
		case EITwinModelType::RealityData:
			return TGetClippingHelper<AITwinRealityData>(Tileset.GetOwner());

		default:
			break;
		}
		// This tileset is not related to iTwin
		return nullptr;
	}

	UITwinClipping3DTilesetHelper* MakeClippingHelper(ACesium3DTileset& Tileset, ITwin::ModelLink const& ModelIdentifier)
	{
		// TODO_JDE: Possible coding improvement: share more code between AITwinIModel, AITwinRealityData and
		// AITwinGoogle3DTileset to avoid this kind of duplication...

		switch (ModelIdentifier.first)
		{
		case EITwinModelType::GlobalMapLayer:
			return TMakeClippingHelper<AITwinGoogle3DTileset>(&Tileset);
		case EITwinModelType::IModel:
			return TMakeClippingHelper<AITwinIModel>(Tileset.GetOwner());
		case EITwinModelType::RealityData:
			return TMakeClippingHelper<AITwinRealityData>(Tileset.GetOwner());

		default:
			break;
		}
		// This tileset is not related to iTwin
		return nullptr;
	}


	template <EITwinClippingPrimitiveType T>
	struct TClippingPrimitiveMPCTrait
	{

	};

	template <>
	struct TClippingPrimitiveMPCTrait<EITwinClippingPrimitiveType::Plane>
	{
		static constexpr const TCHAR* PrimitiveName = TEXT("Plane");
		static constexpr const TCHAR* PrimitiveNamePlural = TEXT("Planes");
		static constexpr const TCHAR* PrimitiveCountName = TEXT("PlaneCount");
	};

	template <>
	struct TClippingPrimitiveMPCTrait<EITwinClippingPrimitiveType::Box>
	{
		static constexpr const TCHAR* PrimitiveName = TEXT("Box");
		static constexpr const TCHAR* PrimitiveNamePlural = TEXT("Boxes");
		static constexpr const TCHAR* PrimitiveCountName = TEXT("BoxCount");
	};

}

struct UITwinClippingRenderer::FTilesetUpdateInfo
{
	uint32 AddedExcluders = 0;
	uint32 ActiveEffectsInTileset = 0;
};

void UITwinClippingRenderer::UpdateTileset_Boxes(ACesium3DTileset& Tileset, ITwin::ModelLink const& ModelIdentifier,
	FTilesetUpdateInfo& UpdateInfo)
{
	if (!ensure(EffectManager.IsValid()))
		return;

	uint32& AddedExcluders(UpdateInfo.AddedExcluders);
	uint32& ActiveEffectsInTileset(UpdateInfo.ActiveEffectsInTileset);

	// Handle clipping boxes.
	// Since we need to aggregate all boxes for the tile exclusion criteria, we just have one
	// excluder for all boxes.
	TArray<UActorComponent*> ExistingBoxTileExcluders = Tileset.K2_GetComponentsByClass(UITwinBoxTileExcluder::StaticClass());
	UITwinBoxTileExcluder* TileExcluderForBoxes = nullptr;
	for (UActorComponent* CandidateComponent : ExistingBoxTileExcluders)
	{
		UITwinBoxTileExcluder* TileExcluder = Cast<UITwinBoxTileExcluder>(CandidateComponent);
		if (TileExcluder)
		{
			TileExcluderForBoxes = TileExcluder;
			break;
		}
	}
	bool bUseBoxExcluder = false;
	bool bIsNewBoxExcluder = false;

	// Append clipping box information to the excluder, if needed.

	const int32 BoxCount = EffectManager->NumEffects(EITwinClippingPrimitiveType::Box);
	for (int32 BoxIndex = 0; BoxIndex < BoxCount; BoxIndex++)
	{
		FITwinClippingBoxInfo const& BoxInfo = EffectManager->GetBoxEffect(BoxIndex);
		if (!BoxInfo.ShouldInfluenceModel(ModelIdentifier))
			continue;
		ActiveEffectsInTileset++;
		bUseBoxExcluder = true;
		// Create one tile excluder for all active boxes if needed:
		if (TileExcluderForBoxes == nullptr)
		{
			TileExcluderForBoxes = Cast<UITwinBoxTileExcluder>(
				Tileset.AddComponentByClass(UITwinBoxTileExcluder::StaticClass(), true,
					FTransform::Identity, false));
			if (ensure(TileExcluderForBoxes))
			{
				bIsNewBoxExcluder = true;
				TileExcluderForBoxes->SetFlags(
					RF_Transient | RF_DuplicateTransient | RF_TextExportTransient);
			}
		}

		if (ensure(TileExcluderForBoxes)
			&& !TileExcluderForBoxes->ContainsBox(BoxInfo.GetBoxPropertiesPtr()))
		{
			TileExcluderForBoxes->BoxPropertiesArray.push_back(
				BoxInfo.GetBoxPropertiesPtr());

			auto& MutableBoxInfo = EffectManager->GetMutableEffect(EITwinClippingPrimitiveType::Box, BoxIndex);
			MutableBoxInfo.RecordTileExcluder(TileExcluderForBoxes);
		}
	}
	if (bIsNewBoxExcluder && ensure(TileExcluderForBoxes))
	{
		ensure(bUseBoxExcluder);
		Tileset.AddInstanceComponent(TileExcluderForBoxes);
		AddedExcluders++;
	}
	else if (!bUseBoxExcluder && TileExcluderForBoxes)
	{
		TileExcluderForBoxes->Deactivate();
	}
}


void UITwinClippingRenderer::UpdateTileset_Planes(ACesium3DTileset& Tileset, ITwin::ModelLink const& ModelIdentifier,
	FTilesetUpdateInfo& UpdateInfo)
{
	if (!ensure(EffectManager.IsValid()))
		return;

	uint32& AddedExcluders(UpdateInfo.AddedExcluders);
	uint32& ActiveEffectsInTileset(UpdateInfo.ActiveEffectsInTileset);

	const auto DeactivateNotMatchedExcluders = [](TArray<UActorComponent*> const& ExistingExcluders,
		std::set<UActorComponent*> const& MatchedExcluders)
	{
		for (UActorComponent* Excluder : ExistingExcluders)
		{
			if (!MatchedExcluders.contains(Excluder))
			{
				UCesiumTileExcluder* TileExcluder = Cast<UCesiumTileExcluder>(Excluder);
				if (TileExcluder)
				{
					TileExcluder->Deactivate();
				}
			}
		}
	};

	// Handle clipping planes.
	TArray<UActorComponent*> ExistingPlaneTileExcluders = Tileset.K2_GetComponentsByClass(UITwinPlaneTileExcluder::StaticClass());
	std::set<UActorComponent*> MatchedPlaneExcluders;

	const int32 PlaneCount = EffectManager->NumEffects(EITwinClippingPrimitiveType::Plane);
	for (int32 PlaneIndex = 0; PlaneIndex < PlaneCount; PlaneIndex++)
	{
		FITwinClippingPlaneInfo const& PlaneInfo = EffectManager->GetPlaneEffect(PlaneIndex);
		if (!PlaneInfo.ShouldInfluenceModel(ModelIdentifier))
			continue;

		auto const& PlaneEquation = PlaneInfo.GetPlaneEquation();
		FVector3d PlaneOrientation = PlaneEquation.PlaneOrientation;
		double PlaneW = PlaneEquation.PlaneW;

		ActiveEffectsInTileset++;

		// Create a tile excluder for this plane if it does not exist.
		UITwinPlaneTileExcluder* TileExcluderForPlane = nullptr;
		for (UActorComponent* CandidateComponent : ExistingPlaneTileExcluders)
		{
			UITwinPlaneTileExcluder* TileExcluder = Cast<UITwinPlaneTileExcluder>(CandidateComponent);
			if (TileExcluder && TileExcluder->PlaneIndex == PlaneIndex)
			{
				TileExcluderForPlane = TileExcluder;
				MatchedPlaneExcluders.insert(CandidateComponent);
				break;
			}
		}

		if (TileExcluderForPlane == nullptr)
		{
			TileExcluderForPlane = Cast<UITwinPlaneTileExcluder>(
				Tileset.AddComponentByClass(UITwinPlaneTileExcluder::StaticClass(), true,
					FTransform::Identity, false));
			if (ensure(TileExcluderForPlane))
			{
				auto& MutablePlaneInfo = EffectManager->GetMutableEffect(EITwinClippingPrimitiveType::Plane, PlaneIndex);
				MutablePlaneInfo.RecordTileExcluder(TileExcluderForPlane);

				TileExcluderForPlane->PlaneIndex = PlaneIndex;
				TileExcluderForPlane->PlaneEquation.PlaneOrientation = PlaneOrientation;
				TileExcluderForPlane->PlaneEquation.PlaneW = PlaneW;
				TileExcluderForPlane->SetInvertEffect(PlaneInfo.GetInvertEffect());

				TileExcluderForPlane->SetFlags(
					RF_Transient | RF_DuplicateTransient | RF_TextExportTransient);

				Tileset.AddInstanceComponent(TileExcluderForPlane);
				AddedExcluders++;
			}
		}
	}
	// deactivate obsolete tile excluders
	DeactivateNotMatchedExcluders(ExistingPlaneTileExcluders, MatchedPlaneExcluders);
}

void UITwinClippingRenderer::UpdateTileset_Polygons(FITwinTilesetAccess const& TilesetAccess,
	FTilesetUpdateInfo& UpdateInfo)
{
	if (!ensure(EffectManager.IsValid()))
		return;

	uint32& ActiveEffectsInTileset(UpdateInfo.ActiveEffectsInTileset);
	auto const ModelIdentifier = TilesetAccess.GetDecorationKey();

	const int32 PolygonCount = EffectManager->NumEffects(EITwinClippingPrimitiveType::Polygon);
	for (int32 Index = 0; Index < PolygonCount; Index++)
	{
		FITwinClippingCartographicPolygonInfo const& PolygonInfo = EffectManager->GetPolygonEffect(Index);
		if (!PolygonInfo.GetSpline().IsValid())
			continue;
		const bool bActivate = PolygonInfo.ShouldInfluenceModel(ModelIdentifier);
		PolygonInfo.GetSpline()->ActivateCutoutEffect(TilesetAccess, bActivate);
		if (bActivate)
		{
			PolygonInfo.GetSpline()->InvertCutoutEffect(TilesetAccess, PolygonInfo.GetInvertEffect());
			ActiveEffectsInTileset++;
		}
	}
}

void UITwinClippingRenderer::UpdateTileset(FITwinTilesetAccess const& TilesetAccess,
	std::optional<EITwinClippingPrimitiveType> const& SpecificPrimitiveType /*= std::nullopt*/)
{
	ITwin::ModelLink const ModelIdentifier = TilesetAccess.GetDecorationKey();
	if (ModelIdentifier.first == EITwinModelType::Invalid)
		return; // not something we handle through the iTwin plugin

	ACesium3DTileset* TilesetPtr = TilesetAccess.GetMutableTileset();
	if (!TilesetPtr)
		return;
	ACesium3DTileset& Tileset(*TilesetPtr);

	FTilesetUpdateInfo UpdateInfo;

	// 3. Handle clipping boxes if needed.
	if (!SpecificPrimitiveType || *SpecificPrimitiveType == EITwinClippingPrimitiveType::Box)
	{
		UpdateTileset_Boxes(Tileset, ModelIdentifier, UpdateInfo);
	}

	// 2. Handle clipping planes if needed.
	if (!SpecificPrimitiveType || *SpecificPrimitiveType == EITwinClippingPrimitiveType::Plane)
	{
		UpdateTileset_Planes(Tileset, ModelIdentifier, UpdateInfo);
	}

	// 2. Handle cartographic polygons if needed.
	if (!SpecificPrimitiveType || *SpecificPrimitiveType == EITwinClippingPrimitiveType::Polygon)
	{
		UpdateTileset_Polygons(TilesetAccess, UpdateInfo);
	}

	if (UpdateInfo.AddedExcluders > 0)
	{
		BE_LOGI("ITwinAdvViz", "[Clipping] Added " << UpdateInfo.AddedExcluders << " Tile Excluder(s) for tileset "
			<< TCHAR_TO_UTF8(*Tileset.GetActorNameOrLabel()));
	}

	UITwinClipping3DTilesetHelper* ClippingHelper = GetClippingHelper(Tileset, ModelIdentifier);
	if (!ClippingHelper && UpdateInfo.ActiveEffectsInTileset > 0)
	{
		// Create the helper which will be responsible for updating the Custom Primitive Data in the Unreal
		// meshes, depending on the influences. It will also be used to filter impacts in ray-tracing of the
		// collision meshes.
		ClippingHelper = MakeClippingHelper(Tileset, ModelIdentifier);
	}
	if (ClippingHelper && ensure(EffectManager.IsValid()))
	{
		if (ClippingHelper->UpdateCPDFlagsFromClippingSelection(*EffectManager))
		{
			// Update existing meshes, if any.
			ClippingHelper->ApplyCPDFlagsToAllMeshComponentsInTileset(Tileset);
			// Future meshes created when a new tile is loaded will be automatically modified through the
			// Cesium lifecycle mesh creation callback.
		}
	}
}


void UITwinClippingRenderer::UpdateAllTilesets(std::optional<EITwinClippingPrimitiveType> const& SpecificPrimitiveType /*= std::nullopt*/)
{
	ITwin::IterateAllITwinTilesets([this, SpecificPrimitiveType](FITwinTilesetAccess const& TilesetAccess)
	{
		UpdateTileset(TilesetAccess, SpecificPrimitiveType);
	}, GetWorld());
}

namespace
{
	template <EITwinClippingPrimitiveType T>
	struct TClippingPrimitiveShaderAdapter
	{
	};

	template <>
	struct TClippingPrimitiveShaderAdapter<EITwinClippingPrimitiveType::Plane>
	{
		inline static bool GetFlipFlag(bool bInvert)
		{
			return bInvert;
		}
	};

	template <>
	struct TClippingPrimitiveShaderAdapter<EITwinClippingPrimitiveType::Box>
	{
		inline static bool GetFlipFlag(bool bInvert)
		{
			// The shader GetBoxClipping.ush was written with the convention that the flipping of a box makes
			// it subtractive, whereas we now changed the logic. To avoid changing the shader and potentially
			// introducing bugs, we just invert the flipping flag for boxes when sending it to the shader.
			return !bInvert;
		}
	};
}

template <typename PrimitiveInfo, EITwinClippingPrimitiveType PrimitiveType>
bool UITwinClippingRenderer::TEncodeFlippingInMPC()
{

	if (!ensure(EffectManager.IsValid()))
		return false;

	using ClippingShaderAdapter = TClippingPrimitiveShaderAdapter<PrimitiveType>;

	const int32 NumEffects = EffectManager->NumEffects(PrimitiveType);

	// We encode the inversion of primitives on float, per groups of 16.
	// inspired by https://theinstructionlimit.com/encoding-boolean-flags-into-a-float-in-hlsl

	int FlipFlags_0_15 = 0;
	for (int32 i = 0; i < std::min(16, NumEffects); i++)
	{
		const bool bInvert = EffectManager->GetEffect(PrimitiveType, i).GetInvertEffect();
		if (ClippingShaderAdapter::GetFlipFlag(bInvert))
			FlipFlags_0_15 |= (1 << i);
	}
	int FlipFlags_16_31 = 0;
	for (int32 i = 0; i < std::min(16, NumEffects - 16); i++)
	{
		const bool bInvert = EffectManager->GetEffect(PrimitiveType, 16 + i).GetInvertEffect();
		if (ClippingShaderAdapter::GetFlipFlag(bInvert))
			FlipFlags_16_31 |= (1 << i);
	}

	using PrimitiveTraits = TClippingPrimitiveMPCTrait<PrimitiveType>;

	bool bStoredInMPC = false;
	UMaterialParameterCollectionInstance* MPCInstance = GetMPCClippingInstance();
	if (ensure(MPCInstance))
	{
		bStoredInMPC = MPCInstance->SetScalarParameterValue(
			FName(*FString::Printf(TEXT("Flip%s_0_15"), PrimitiveTraits::PrimitiveNamePlural)),
			static_cast<float>(FlipFlags_0_15));

		bStoredInMPC &= MPCInstance->SetScalarParameterValue(
			FName(*FString::Printf(TEXT("Flip%s_16_31"), PrimitiveTraits::PrimitiveNamePlural)),
			static_cast<float>(FlipFlags_16_31));
		ensure(bStoredInMPC);
	}
	return bStoredInMPC;
}

bool UITwinClippingRenderer::EncodeFlippingInMPC(EITwinClippingPrimitiveType Type)
{
	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
		return TEncodeFlippingInMPC<FITwinClippingBoxInfo, EITwinClippingPrimitiveType::Box>();
	case EITwinClippingPrimitiveType::Plane:
		return TEncodeFlippingInMPC<FITwinClippingPlaneInfo, EITwinClippingPrimitiveType::Plane>();
	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(
	case EITwinClippingPrimitiveType::Polygon:
	case EITwinClippingPrimitiveType::Count:
		, false);
	}
}

template <EITwinClippingPrimitiveType PrimitiveType>
bool UITwinClippingRenderer::TUpdatePrimitiveCountInMPC()
{
	bool bSuccess = false;

	UMaterialParameterCollectionInstance* MPCInstance = GetMPCClippingInstance();
	if (ensure(MPCInstance && EffectManager.IsValid()))
	{
		using PrimitiveTraits = TClippingPrimitiveMPCTrait<PrimitiveType>;

		const bool bFound = MPCInstance->SetScalarParameterValue(
			PrimitiveTraits::PrimitiveCountName,
			static_cast<float>(EffectManager->NumEffects(PrimitiveType)));
		ensure(bFound);
		bSuccess = bFound;

		BE_LOGI("ITwinAdvViz", "[Clipping] "
			<< TCHAR_TO_UTF8(PrimitiveTraits::PrimitiveCountName)
			<< ": " << EffectManager->NumEffects(PrimitiveType)
			<< " - set parameter result: " << (bFound ? 1 : 0));

	}
	return bSuccess;
}

void UITwinClippingRenderer::OnClippingInstanceArrayResized(EITwinClippingPrimitiveType PrimitiveType)
{
	bool bSuccess = false;
	switch (PrimitiveType)
	{
	case EITwinClippingPrimitiveType::Box:
		bSuccess = TUpdatePrimitiveCountInMPC<EITwinClippingPrimitiveType::Box>();
		break;
	case EITwinClippingPrimitiveType::Plane:
		bSuccess = TUpdatePrimitiveCountInMPC<EITwinClippingPrimitiveType::Plane>();
		break;
	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(
	case EITwinClippingPrimitiveType::Polygon:
	case EITwinClippingPrimitiveType::Count:
		, );
	}

	// Update Material Parameter Collection for cutout, as well as 3D tilesets.
	EncodeFlippingInMPC(PrimitiveType);
	UpdateAllTilesets(PrimitiveType);
}

bool UITwinClippingRenderer::UpdateBoxPropertiesInMPC(FITwinClippingBoxInfo const& BoxInfo, int32 BoxIndex)
{
	bool bIsClippingReady = false;
	UMaterialParameterCollectionInstance* MPCInstance = GetMPCClippingInstance();
	if (ensure(MPCInstance))
	{
		// For performance reasons, we store the inverse matrix.
		auto const& BoxProperties = BoxInfo.GetBoxProperties();
		glm::dmat3x3 const& InverseMatrix = BoxProperties.BoxInvMatrix;
		glm::dvec3 const col0 = glm::column(InverseMatrix, 0);
		glm::dvec3 const col1 = glm::column(InverseMatrix, 1);
		glm::dvec3 const col2 = glm::column(InverseMatrix, 2);
		glm::dvec3 const& BoxTranslation = BoxProperties.BoxTranslation;
		bIsClippingReady =
			MPCInstance->SetVectorParameterValue(
				FName(fmt::format("BoxInvMatrix_col0_{}", BoxIndex).c_str()),
				FLinearColor(col0.x, col0.y, col0.z))
			&& MPCInstance->SetVectorParameterValue(
				FName(fmt::format("BoxInvMatrix_col1_{}", BoxIndex).c_str()),
				FLinearColor(col1.x, col1.y, col1.z))
			&& MPCInstance->SetVectorParameterValue(
				FName(fmt::format("BoxInvMatrix_col2_{}", BoxIndex).c_str()),
				FLinearColor(col2.x, col2.y, col2.z))
			&& MPCInstance->SetVectorParameterValue(
				FName(fmt::format("BoxTranslation_{}", BoxIndex).c_str()),
				FLinearColor(BoxTranslation.x, BoxTranslation.y, BoxTranslation.z));
		ensure(bIsClippingReady);
	}
	return bIsClippingReady;
}

bool UITwinClippingRenderer::UpdatePlanePropertiesInMPC(FITwinClippingPlaneInfo const& PlaneInfo, int32 PlaneIndex)
{
	bool bIsClippingReady = false;
	UMaterialParameterCollectionInstance* MPCInstance = GetMPCClippingInstance();
	if (ensure(MPCInstance))
	{
		auto const& PlaneEquation = PlaneInfo.GetPlaneEquation();
		auto const& PlaneOrientation = PlaneEquation.PlaneOrientation;
		double const PlaneW = PlaneEquation.PlaneW;

		const FLinearColor PlaneEquationAsColor =
		{
			static_cast<float>(PlaneOrientation.X),
			static_cast<float>(PlaneOrientation.Y),
			static_cast<float>(PlaneOrientation.Z),
			static_cast<float>(PlaneW)
		};
		bIsClippingReady = MPCInstance->SetVectorParameterValue(
			FName(fmt::format("PlaneEquation_{}", PlaneIndex).c_str()),
			PlaneEquationAsColor);

		ensure(bIsClippingReady);
	}
	return bIsClippingReady;
}

void UITwinClippingRenderer::OnEffectPropertiesModified(EITwinClippingPrimitiveType EffectType, int32 Index)
{
	if (!ensure(EffectManager.IsValid()))
		return;
	switch (EffectType)
	{
	case EITwinClippingPrimitiveType::Box:
	{
		UpdateBoxPropertiesInMPC(EffectManager->GetBoxEffect(Index), Index);
		break;
	}

	case EITwinClippingPrimitiveType::Plane:
	{
		UpdatePlanePropertiesInMPC(EffectManager->GetPlaneEffect(Index), Index);
		break;
	}
	case EITwinClippingPrimitiveType::Polygon:
		break;
	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(
	case EITwinClippingPrimitiveType::Count:
	)
	}
}


double UITwinClippingRenderer::GetClippingValue_Boxes(FVector const& AbsoluteWorldPosition, ITwin::ModelLink const& ModelIdentifier) const
{
	if (!ensure(EffectManager.IsValid()))
		return 1.0;

	// Equivalent of shader GetBoxClipping.ush
	double clippingValue = 1.0;

	uint32 NumActiveBoxes = 0;
	uint32 NumActiveAdditiveBoxes = 0;
	double AdditiveBoxValue = 0.0;
	double SubtractiveBoxValue = 0.0;

	// test inclusion in a box without branching (from https://stackoverflow.com/questions/12751080/glsl-point-inside-box-test)
	static const glm::double3 bottomLeft = glm::double3(-0.5, -0.5, -0.5);
	static const glm::double3 topRight = glm::double3(0.5, 0.5, 0.5);

	const glm::double3 WorldPosition(
		AbsoluteWorldPosition.X,
		AbsoluteWorldPosition.Y,
		AbsoluteWorldPosition.Z);

	const int32 BoxCount = EffectManager->NumEffects(EITwinClippingPrimitiveType::Box);
	for (int32 BoxIndex = 0; BoxIndex < BoxCount; BoxIndex++)
	{
		FITwinClippingBoxInfo const& BoxInfo = EffectManager->GetBoxEffect(BoxIndex);
		if (BoxInfo.ShouldInfluenceModel(ModelIdentifier))
		{
			auto const& BoxProperties = BoxInfo.GetBoxProperties();
			glm::double3 pos_BoxCoords = BoxProperties.BoxInvMatrix * (WorldPosition - BoxProperties.BoxTranslation);
			glm::double3 s = glm::step(bottomLeft, pos_BoxCoords) - glm::step(topRight, pos_BoxCoords);
			double isInsideValueForBox = s.x * s.y * s.z;
			if (BoxProperties.bIsSubtractive)
			{
				SubtractiveBoxValue += isInsideValueForBox;
			}
			else
			{
				NumActiveAdditiveBoxes++;
				AdditiveBoxValue += isInsideValueForBox;
			}
			NumActiveBoxes++;
		}
	}

	if (NumActiveBoxes > 0)
	{
		if (SubtractiveBoxValue > 0.0)
			clippingValue = 0.0;
		else if (NumActiveAdditiveBoxes > 0)
			clippingValue = AdditiveBoxValue;
	}

	return glm::step(1.0, clippingValue);
}

double UITwinClippingRenderer::GetClippingValue_Planes(FVector const& WorldPosition, ITwin::ModelLink const& ModelIdentifier) const
{
	if (!ensure(EffectManager.IsValid()))
		return 1.0;

	// Equivalent of shader GetPlanesClipping.ush

	const int32 PlaneCount = EffectManager->NumEffects(EITwinClippingPrimitiveType::Plane);
	for (int32 PlaneIndex = 0; PlaneIndex < PlaneCount; PlaneIndex++)
	{
		FITwinClippingPlaneInfo const& PlaneInfo = EffectManager->GetPlaneEffect(PlaneIndex);
		if (PlaneInfo.ShouldInfluenceModel(ModelIdentifier))
		{
			auto const& PlaneEquation = PlaneInfo.GetPlaneEquation();
			double Value = glm::step(PlaneEquation.PlaneOrientation.Dot(WorldPosition) - PlaneEquation.PlaneW, 0.);
			if (PlaneInfo.GetInvertEffect())
				Value = 1.0 - Value;
			if (Value < 0.5)
				return 0.;
		}
	}
	return 1.;
}

bool UITwinClippingRenderer::ShouldCutOut(FVector const& AbsoluteWorldPosition, ITwin::ModelLink const& ModelIdentifier,
	UCesiumPolygonRasterOverlay const* RasterOverlay) const
{
	double ClippingValue = 1.0;
	ClippingValue = GetClippingValue_Boxes(AbsoluteWorldPosition, ModelIdentifier);
	if (ClippingValue < 0.5)
		return true;
	ClippingValue = GetClippingValue_Planes(AbsoluteWorldPosition, ModelIdentifier);
	if (ClippingValue < 0.5)
		return true;
	// Cutout polygons are tested through the Cesium raster overlay.
	if (RasterOverlay && RasterOverlay->ShouldExcludePoint(AbsoluteWorldPosition))
		return true;
	return false;
}



#if WITH_EDITOR

void UITwinClippingRenderer::ActivateEffects(EITwinClippingPrimitiveType Type, EITwinClippingEffectLevel Level, bool bActivate)
{
	if (!ensure(EffectManager.IsValid()))
		return;

	if (Level == EITwinClippingEffectLevel::Tileset)
	{
		EffectManager->VisitClippingPrimitivesOfType(Type, [bActivate](FITwinClippingInfoBase& PrimitiveInfo)
		{
			PrimitiveInfo.ActivateEffectAtTilesetLevel(bActivate);
		});
	}

	if (Level == EITwinClippingEffectLevel::Shader
		&& Type != EITwinClippingPrimitiveType::Polygon)
	{
		UMaterialParameterCollectionInstance* MPCInstance = GetMPCClippingInstance();
		if (ensure(MPCInstance))
		{
			switch (Type)
			{
			case EITwinClippingPrimitiveType::Box:
				MPCInstance->SetScalarParameterValue(
					TClippingPrimitiveMPCTrait<EITwinClippingPrimitiveType::Box>::PrimitiveCountName,
					static_cast<float>(bActivate ? EffectManager->NumEffects(Type) : 0));
				break;

			case EITwinClippingPrimitiveType::Plane:
				MPCInstance->SetScalarParameterValue(
					TClippingPrimitiveMPCTrait<EITwinClippingPrimitiveType::Plane>::PrimitiveCountName,
					static_cast<float>(bActivate ? EffectManager->NumEffects(Type) : 0));
				break;

			BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(
			case EITwinClippingPrimitiveType::Polygon:
			case EITwinClippingPrimitiveType::Count:);
			}
		}
	}
}

#endif // WITH_EDITOR
