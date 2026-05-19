/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingBoxInfo.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Clipping/ITwinClippingBoxInfo.h>

#include <Clipping/ITwinBoxTileExcluder.h>
#include <Spline/ITwinSplineHelper.h>
#include <Spline/ITwinSplineTool.h>

#include <array>

//---------------------------------------------------------------------------------------
// struct FITwinClippingBoxInfo
//---------------------------------------------------------------------------------------

bool FITwinClippingBoxInfo::GetInvertEffect() const
{
	return BoxProperties->bInvertEffect;
}

void FITwinClippingBoxInfo::DoSetInvertEffect(bool bInvert)
{
	BoxProperties->bInvertEffect = bInvert;
}

void FITwinClippingBoxInfo::UpdateBoxProperties(glm::dmat3x3 const& BoxMatrix, glm::dvec3 const& BoxTranslation)
{
	// Store inverse matrix and translation
	BoxProperties->BoxInvMatrix = glm::inverse(BoxMatrix);
	BoxProperties->BoxTranslation = BoxTranslation;

	// Calculate bounds
	std::array<glm::dvec3, 8> BoxVertices;
	BoxVertices[0] = BoxTranslation + (BoxMatrix * glm::dvec3(-0.5, -0.5, -0.5));
	BoxVertices[1] = BoxTranslation + (BoxMatrix * glm::dvec3(-0.5, -0.5, 0.5));
	BoxVertices[2] = BoxTranslation + (BoxMatrix * glm::dvec3(-0.5, 0.5, -0.5));
	BoxVertices[3] = BoxTranslation + (BoxMatrix * glm::dvec3(-0.5, 0.5, 0.5));
	BoxVertices[4] = BoxTranslation + (BoxMatrix * glm::dvec3(0.5, -0.5, -0.5));
	BoxVertices[5] = BoxTranslation + (BoxMatrix * glm::dvec3(0.5, -0.5, 0.5));
	BoxVertices[6] = BoxTranslation + (BoxMatrix * glm::dvec3(0.5, 0.5, -0.5));
	BoxVertices[7] = BoxTranslation + (BoxMatrix * glm::dvec3(0.5, 0.5, 0.5));

	FBox3d Box;
	for (auto const& v : BoxVertices)
	{
		Box += FVector3d(v.x, v.y, v.z);
	}
	BoxProperties->BoxBounds = FBoxSphereBounds(Box);
}

void FITwinClippingBoxInfo::DeactivatePrimitiveInExcluder(UITwinTileExcluderBase& Excluder) const
{
	if (ensure(Excluder.IsA(UITwinBoxTileExcluder::StaticClass())))
	{
		Cast<UITwinBoxTileExcluder>(&Excluder)->RemoveBox(BoxProperties);
	}
}

void FITwinClippingBoxInfo::SetEdgeVisibility(bool bVisible)
{
	SetEdgeSplinesVisibility(bVisible);
}

void FITwinClippingBoxInfo::CreateEdgeSplines(AITwinSplineTool* SplineTool)
{
	if (!ensure(SplineTool != nullptr))
	{
		return;
	}
	if (BoxEdgeSplines.Num() == 6)
	{
		return; // Splines already created
	}
	auto const PreviousUsage = SplineTool->GetUsage();
	SplineTool->SetUsage(EITwinSplineUsage::EdgeDisplayHelper);

	static const TArray<FVector> CubePositions =
	{
		{ -0.5, -0.5, -0.5 },
		{ -0.5, -0.5,  0.5 },
		{ -0.5,  0.5, -0.5 },
		{ -0.5,  0.5,  0.5 },
		{  0.5, -0.5, -0.5 },
		{  0.5, -0.5,  0.5 },
		{  0.5,  0.5, -0.5 },
		{  0.5,  0.5,  0.5 }
	};
	static const TArray<TArray<FVector>> CubeFaces =
	{
		{ CubePositions[2], CubePositions[3], CubePositions[1], CubePositions[0] },
		{ CubePositions[6], CubePositions[7], CubePositions[3], CubePositions[2] },
		{ CubePositions[4], CubePositions[5], CubePositions[7], CubePositions[6] },
		{ CubePositions[0], CubePositions[1], CubePositions[5], CubePositions[4] },
		{ CubePositions[1], CubePositions[3], CubePositions[7], CubePositions[5] },
		{ CubePositions[0], CubePositions[4], CubePositions[6], CubePositions[2] }
	};
	for (int i(0); i < 6; ++i)
	{
		auto EdgeSpline = SplineTool->AddSpline(FVector::ZeroVector, CubeFaces[i]);
		BoxEdgeSplines.Add(EdgeSpline);
#if WITH_EDITOR
		EdgeSpline->SetActorLabel(FString::Printf(TEXT("BoxEdgeSpline_%d"), i));
#endif
		EdgeSpline->SetActorHiddenInGame(true);
	}
	SplineTool->SetUsage(PreviousUsage);
}

void FITwinClippingBoxInfo::UpdateEdgeSplinesTransform(FTransform const& InstanceTransform)
{
	for (auto& Spline : BoxEdgeSplines)
	{
		if (Spline)
		{
			Spline->SetActorTransform(InstanceTransform);
		}
	}
}

void FITwinClippingBoxInfo::SetEdgeSplinesSelected(bool bSelected)
{
	for (auto& Spline : BoxEdgeSplines)
	{
		if (Spline)
		{
			Spline->SetSelected(bSelected);
		}
	}

}

void FITwinClippingBoxInfo::SetEdgeSplinesVisibility(bool bVisible)
{
	for (auto& Spline : BoxEdgeSplines)
	{
		if (Spline)
		{
			Spline->SetActorHiddenInGame(!bVisible);
		}
	}
}

void FITwinClippingBoxInfo::BeforeDestroy()
{
	// Make sure to destroy the edge splines before the box is destroyed, to avoid keeping ghosts of the box
	// edges in the scene after the box has been removed.
	for (auto& Spline : BoxEdgeSplines)
	{
		if (Spline)
		{
			Spline->Destroy();
		}
	}
	BoxEdgeSplines.Empty();
}
