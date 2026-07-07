/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingBoxInfo.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Clipping/ITwinClippingInfoBase.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
	#include <glm/ext/matrix_double3x3.hpp>
	#include <glm/ext/vector_double3.hpp>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <memory>

#include <ITwinClippingBoxInfo.generated.h>


USTRUCT()
struct FITwinClippingBoxInfo final : public FITwinClippingInfoBase
{
	GENERATED_USTRUCT_BODY()

	virtual bool GetInvertEffect() const override;
	virtual void DeactivatePrimitiveInExcluder(UITwinTileExcluderBase& Excluder) const override;

	void UpdateBoxProperties(glm::dmat3x3 const& BoxMatrix, glm::dvec3 const& BoxTranslation);


	struct FBoxProperties
	{
		glm::dmat3x3 BoxInvMatrix = glm::dmat3x3(1.0); // For performance reasons, we store the inverse matrix.
		glm::dvec3 BoxTranslation = glm::dvec3(0.0);
		FBoxSphereBounds BoxBounds;

		/// Whether the box is subtractive (i.e. it creates a hole in the scene), or additive (i.e. only the
		/// content inside the box is visible).
		bool bIsSubtractive = true;
	};

	FBoxProperties const& GetBoxProperties() const { return *BoxProperties; }

	const std::shared_ptr<FBoxProperties>& GetBoxPropertiesPtr() const { return BoxProperties; }

protected:
	virtual void DoSetInvertEffect(bool bInvert);

	virtual int32 CountRequiredEdgeSplines() const override;
	virtual void DoCreateEdgeSplines(TArray<TObjectPtr<AITwinSplineHelper>>& OutEdgeSplines, AITwinSplineTool& SplineTool) override;


private:
	// Will be shared by all tile excluders including this box.
	std::shared_ptr<FBoxProperties> BoxProperties = std::make_shared<FBoxProperties>();
};
