/*--------------------------------------------------------------------------------------+
|
|     $Source: ExtensionITwinMaterialID.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <CesiumUtility/ExtensibleObject.h>

#include <cstdint>

namespace BeUtils
{
	/**
	 * @brief glTF extension to specify iTwin Material Identifier.
	 */
	struct ExtensionITwinMaterialID final : public CesiumUtility::ExtensibleObject
	{
		static inline constexpr const char* TypeName = "ExtensionITwinMaterialID";
		static inline constexpr const char* ExtensionName = "ITWIN_material_identifier";

		/**
		 * @brief The material identifier.
		 *
		 * @warning May differ from the iModel Material ID found in the original model (exported as meta-data
		 * by the Mesh-Export Service).
		 */
		uint64_t materialId = 0;
	};
} // namespace BeUtils
