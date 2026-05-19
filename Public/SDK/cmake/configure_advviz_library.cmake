# Should be synchronized with function get_cesium_glm_compile_definitions
# (in <cesium-native>/cmake/macros/configure_cesium_library.cmake)
# so that the same GLM options are used in the AdvViz SDK whatever the context is (either the Unreal plugin
# or another use case).
function(get_advviz_glm_compile_definitions)
	if (ADVVIZ_GLM_STRICT_ENABLED)
		list (APPEND glm_options GLM_FORCE_XYZW_ONLY) # Disable .rgba and .stpq to make it easier to view values from debugger
		list (APPEND glm_options GLM_FORCE_EXPLICIT_CTOR) # Disallow implicit conversions between dvec3 <-> dvec4, dvec3 <-> fvec3, etc
	endif()
	# GLM defines that should be enabled regardless of strict mode
	list (APPEND glm_options GLM_FORCE_INTRINSICS) # Force SIMD code paths
	list (APPEND glm_options GLM_ENABLE_EXPERIMENTAL) # Allow use of experimental extensions

	if (ARGV0)
		set(${ARGV0} ${glm_options} PARENT_SCOPE)
	else ()
		message(FATAL_ERROR "variable for GLM options name was not specified")
	endif ()
endfunction()

# Mostly useful in case the AdvViz SDK is used in standalone context (outside of Unreal + Cesium)
# For now it only deals with GLM options, so that the same options as in cesium-native are used.
function(configure_advviz_library targetName)

	# GLM defines
	get_advviz_glm_compile_definitions(glm_defs)
	target_compile_definitions(
		${targetName}
		PUBLIC
			${glm_defs}
	)

endfunction()
